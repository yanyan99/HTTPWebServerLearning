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
    struct stat st;
    
}
void bad_request(int);
void cat(int, FILE *);
void cannot_execute(int);
void error_die(const char *msg){
    perror(msg);
    exit(1);
}
void execute_cgi(int, const char*, const char *, const char *);
int get_line(int, char *, int);
void headers(int, const char *);
void not_found(int);
void server_file(int, const char *);
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
void unimplemented(int);