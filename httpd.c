#include<stdio.h>
#include<sys/socket.h>
#include<sys/types.h>
#include<netinet/in.h>
#include<arpa/inet.h>
#include<unistd.h>
#include<ctype.h>
#include<string.h>
#include<strings.h>
#include<sys/stat.h>
#include<pthread.h>
#include<sys/wait.h>
#include<stdlib.h>

#define ISspace(x) isspace((int)(x))

#define SERVER_STRING "Server: G's http/0.1.0\r\n"//定义个人server名称

void *accept_request(void *client);
void bad_request(int);
void cat(int, FILE *);
void cannot_execute(int);
void error_die(const char*);
void execute_cgi(int, const char*, const char *, const char *);
int get_line(int, char *, int);
void headers(int, const char *);
void not_found(int);
void server_file(int, const char *);
int startup(u_short *);
void unimplemented(int);


int main(){
    int serv_sock, clnt_sock;
    u_short port = 9090;
    struct sockaddr_in clnt_adr;
    socklen_t clnt_adr_sz;
    pthread_t newthread;

    serv_sock=startup(&port);

    printf("http server sock is %d\n", serv_sock);
    printf("http running on port %d\n", port);

    while(1){
        clnt_adr_sz=sizeof(clnt_adr);
        clnt_sock=accept(serv_sock, (struct sockaddr*)&clnt_adr, &clnt_adr_sz);
        if(clnt_sock==-1)error_die("accept");

        printf("New connection... IP: %s , Port: %d\n", inet_ntoa(clnt_adr.sin_addr), ntohs(clnt_adr.sin_port));

        if(pthread_create(&newthread, NULL, accept_request, (void *)&clnt_sock)!=0)error_die("pthread_create");

    }
    close(serv_sock);

    return 0;

}






void *accept_request(void *client){
    int clnt_sock=*((int*)client);
    char buf[1024];
    int numchars;
    char method[255];
    char url[255];
    char path[512];
    size_t i, j;
    // stat
    struct stat st;
    int cgi=0;
    char *query_string=NULL;

    // 解析一行报文
    numchars=get_line(client, buf, sizeof(buf));

    i=0;
    j=0;

    while(!ISspace(buf[j])&&(i<sizeof(method)-1)){
        //提取其中的请求方式
        method[i]=buf[j];
        i++;
        j++;
    }
    method[i]='\0';

    // int strcasecmp(const char *s1, const char *s2);
    // 比较s1和s2字符串，相同返回0
    if(strcasecmp(method, "GET")&&strcasecmp(method, "POST")){
        // 不是get也不是post
        unimplemented(client);
        return NULL;
    }

    if(strcasecmp(method, "POST")==0)cgi=1;

    i=0;
    while(ISspace(buf[j])&&(i<sizeof(url)-1)&&(j<siseof(buf))){
        url[i]=buf[j];
        i++;j++;
    }
    url[i]='\0';

    //GET请求url可能会带有？，有查询参数
    if(strcasecmp(method, "GET")==0){
        query_string=url;
        while((*query_string!='?')&&(*query_string!='\0'))
            query_string++;
        
        // 如果有？表明是动态请求， 开启cgi
        if(*query_string=='?'){
            cgi=1;
            *query_string='\0';
            query_string++;
        }

    }

    sprintf(path, "httpdocs%s", url);

    if(path[strlen(path)-1]=='/'){
        strcat(path, "test,html");
    }

    if(stat(path, &st)==-1){
        while((numchars>0)&&strcmp("\n",buf))
            numchars=get_line(client, buf, sizeof(buf));
        
        not_found(client);
    }
    else{
        if((st.st_mode&S_IFMT)==S_IFDIR)//S_IFDIR代表目录
        //如果请求参数是目录，自动打开test.html
            strcat(path, "/test.html");
        
        //文件可执行
        if((st.st_mode&S_IXUSR)||
            (st.st_mode&S_IXGRP)||
            (st.st_mode&S_IXOTH))
            //S_IXUSR文件所有者具有可执行权限
            //S_IXGRP用户组具有可执行权限
            //S_IXOTH其他用户具有可读取权限
            cgi=1;
        
        if(!cgi)server_file(client, path);
        else execute_cgi(client, path, method, query_string);

    }
    close(client);
    return NULL;
    
}

void bad_request(int client)
{
	 char buf[1024];
	//发送400
	 sprintf(buf, "HTTP/1.0 400 BAD REQUEST\r\n");
	 send(client, buf, sizeof(buf), 0);
	 sprintf(buf, "Content-type: text/html\r\n");
	 send(client, buf, sizeof(buf), 0);
	 sprintf(buf, "\r\n");
	 send(client, buf, sizeof(buf), 0);
	 sprintf(buf, "<P>Your browser sent a bad request, ");
	 send(client, buf, sizeof(buf), 0);
	 sprintf(buf, "such as a POST without a Content-Length.\r\n");
	 send(client, buf, sizeof(buf), 0);
}


void cat(int client, FILE *resource){
    // 发送文件内容
    char buf[1024];
    fgets(buf, sizeof(buf), resource);
    while(!feof(resource)){
        send(client, buf, strlen(buf), 0);
        fgets(buf, sizeof(buf), resource);
    }
}


void cannot_execute(int client)
{
	 char buf[1024];
	//发送500
	 sprintf(buf, "HTTP/1.0 500 Internal Server Error\r\n");
	 send(client, buf, strlen(buf), 0);
	 sprintf(buf, "Content-type: text/html\r\n");
	 send(client, buf, strlen(buf), 0);
	 sprintf(buf, "\r\n");
	 send(client, buf, strlen(buf), 0);
	 sprintf(buf, "<P>Error prohibited CGI execution.\r\n");
	 send(client, buf, strlen(buf), 0);
}


void error_die(const char *msg){
    perror(msg);
    exit(1);
}

// 动态解析
void execute_cgi(int clnt, const char*path, const char *method, const char *query_string){
    char buf[1024];
    int cgi_output[2];
    int cgi_input[2];

    pid_t pid;
    int status;

    int i;
    char c;

    int numchars=1;
    int content_length=-1;
    //默认字符
    buf[0]='A';
    buf[1]='\0';
    if(strcasecmp(method, "GET")==0)
        while((numchars>0)&&strcmp("\n", buf))
            numchars=get_line(clnt, buf, sizeof(buf));
    else{
        numchars=get_line(clnt, buf, sizeof(buf));
        while((numchars>0)&&strcmp("\n", buf)){
            buf[15]='\0';
            if(strcasecmp(buf, "Content-Length:")==0)
                content_length=atoi(&(buf[16]));
            numchars=get_line(clnt, buf, sizeof(buf));
        }

        if(content_length==-1){
            bad_request(clnt);
            return ;
        }
    }
    sprintf(buf, "HTTP/1.0 200 OK\r\n");
    send(clnt, buf, strlen(buf),0);
    if(pipe(cgi_output)<0){
        cannot_execute(clnt);
        return ;
    }
    if(pipe(cgi_input)<0){
        cannot_execute(clnt);
        return ;
    }

    if((pid=fork())<0){
        cannot_execute(clnt);
        return ;
    }

    if(pid==0){
        // 子进程 运行CGI脚本
        char meth_env[255];
        char query_env[255];
        char length_env[255];

        dup2(cgi_output[1],1);
        dup2(cgi_input[0], 0);

        close(cgi_output[0]);
        close(cgi_input[1]);//关闭写通道

        // int putenv(const char *string);
        // 
        sprintf(meth_env, "REQUEST_METHOD=%s", method);
        put_env(meth_env);

        if(strcasecmp(method, "GET")==0){
            //存储QUERY——STRING
            sprintf(query_env, "QUERY_STRING=%s", query_string);
            put_env(query_env);
        }
        else{
            //存储CONTENT——LENGTH
            sprintf(length_env, "CONTENT_LENGTH=%d", content_length);
            put_env(length_env);
        }

        execl(path, path, NULL);
        exit(0);
    }
    else{
        close(cgi_output[1]);
        close(cgi_input[0]);
        if(strcasecmp(method, "POST")==0)
            for(i=0;i<content_length;i++){
                recv(clnt, &c, 1, 0);
                write(cgi_input[1], &c, 1);
            }

            //读取cgi脚本返回数据
            while(read(cgi_output[0],&c,1)>0){
                //发送给浏览器
                send(clnt, &c, 1,0);
            }
            //运行结束关闭
            close(cgi_output[0]);
            close(cgi_input[1]);

            waitpid(pid, &status, 0);
    }
}

// 解析一行http报文
int get_line(int sock, char *buf, int size){
    int i=0;
    char c='\0';
    int n;

    while((i<size-1)&&(c!='\n')){
        n=recv(sock, &c, 1, 0);
        if(n>0){
            if(c=='\r'){
                n=recv(sock, &c, 1, MSG_PEEK);
                if((n>0)&&(c=='\n'))
                    recv(sock, &c, 1, 0);
                else
                    c='\n';
            }
            buf[i]=c;
            i++;
        }
        else
            c='\n';
    }
    buf[i]='\0';
    return i;

}


void headers(int client, const char *filename){
    char buf[1024];

    (void)filename;

    strcpy(buf, "HTTP/1.0 200 OK\r\n");
    send(client, buf, strlen(buf), 0);
    strcpy(buf, SERVER_STRING);
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "Content-Type: text/html\r\n");
    send(client, buf, strlen(buf), 0);
    strcpy(buf, "\r\n");
    send(client, buf, strlen(buf), 0);

}


void not_found(int client){
    char buf[1024];
    // 返回404
    sprintf(buf, "HTTP/1.0 404 Not Found\r\n");
    /*
    #include<sys/socket.h>
    返回发送的字节数，失败=-1
    ssize_t send(int sockfd, const void *buf, size_t nbytes, int flags);
    返回接收的字节数，EOF=0,失败=-1
    ssize_t recv(int sockfd, void *buf, size_t nbytes, int flags);
    */
    send(client, buf, strlen(buf), 0);
    sprintf(buf, SERVER_STRING);
    send(client, buf, sizeof(buf), 0);
    sprintf(buf, "Content-Type: text/html\r\n");
    send(client, buf, sizeof(buf), 0);
    sprintf(buf, "\r\n");
    send(client, buf, sizeof(buf), 0);
    sprintf(buf, "<HTML><HEAD><title>Not Found\r\n");
    send(client, buf, sizeof(buf), 0);
    sprintf(buf, "</title></head>\r\n");
    send(client, buf, sizeof(buf), 0);
    sprintf(buf, "<body><p>The Server counld not fulfill\r\n");
    send(client, buf, sizeof(buf), 0);
    sprintf(buf, "</body></html>\r\n");
    send(client, buf, sizeof(buf), 0);
}

// 静态文件，直接读取文件返回给请求的http客户端
void server_file(int client, const char * filename){
    FILE *resource=NULL;
    int numchars=1;
    char buf[1024];
    buf[0]='A';
    buf[1]='\0';
    while((numchars>0)&&strcmp("\n", buf))
        numchars=get_line(client, buf, sizeof(buf));
    
    // 打开文件
    resource=fopen(filename, "r");
    if(resource==NULL)not_found(client);
    else{
        headers(client, filename);
        cat(client, resource);
    }
    fclose(resource);
}


//启动服务端
int startup(u_short * port){
    int httpd=0,option;
    struct sockaddr_in serv_adr;

    httpd=socket(PF_INET, SOCK_STREAM, 0);
    if(httpd==-1)error_die("socket");

    /*
    设置套接字选项
    level:协议层
    optname:可选项名
    optval:保存要更改的选项信息的缓冲地址值
    int setsocket(int sock, int level, int optname, const void *optval, socklen_t optlen);
    */
   /*
   SO_REUSEADDR 端口释放后立即可以被再次使用
   */
    socklen_t optlen;
    optlen = sizeof(option);
    option=1;
    setsockopt(httpd, SOL_SOCKET, SO_REUSEADDR, (void*)&option, optlen);
    
    memset(&serv_adr, 0, sizeof(serv_adr));
    serv_adr.sin_addr.s_addr=htonl(INADDR_ANY);
    serv_adr.sin_family=AF_INET;
    serv_adr.sin_port=htons(*port);

    if(bind(httpd,(struct sockaddr*)&serv_adr, sizeof(serv_adr))==-1)error_die("bind");
    
    if(*port==0){
        socklen_t namelen=sizeof(serv_adr);
        if(getsockname(httpd, (struct aockaddr*)&serv_adr, &namelen)==-1)error_die("getsockname");
        *port=ntohs(serv_adr.sin_port);
    }
    if(listen(httpd, 20)==-1)error_die("listen");
    return httpd;
    }


void unimplemented(int client){
    char buf[1024];
    // 发送501说明相应方法没有实现
    sprintf(buf, "HTTP/1.0 501 Method Not Implemented\r\n");
    /*
    #include<sys/socket.h>
    返回发送的字节数，失败=-1
    ssize_t send(int sockfd, const void *buf, size_t nbytes, int flags);
    返回接收的字节数，EOF=0,失败=-1
    ssize_t recv(int sockfd, void *buf, size_t nbytes, int flags);
    */
    send(client, buf, strlen(buf), 0);
    sprintf(buf, SERVER_STRING);
    send(client, buf, sizeof(buf), 0);
    sprintf(buf, "Content-Type: text/html\r\n");
    send(client, buf, sizeof(buf), 0);
    sprintf(buf, "\r\n");
    send(client, buf, sizeof(buf), 0);
    sprintf(buf, "<HTML><HEAD><title>Method Not Implemented\r\n");
    send(client, buf, sizeof(buf), 0);
    sprintf(buf, "</title></head>\r\n");
    send(client, buf, sizeof(buf), 0);
    sprintf(buf, "<body><p>HTTP request method not supported.\r\n");
    send(client, buf, sizeof(buf), 0);
    sprintf(buf, "</body></html>\r\n");
    send(client, buf, sizeof(buf), 0);
    

}