/*
 * proxy.c - ICS Web proxy
 *
 *
 */

#include "csapp.h"
#include <stdarg.h>
#include <sys/select.h>

/*
 * Function prototypes
 */
int parse_uri(char *uri, char *target_addr, char *path, char *port);
void format_log_entry(char *logstring, struct sockaddr_in *sockaddr, char *uri, size_t size);
void doit(int fd,struct sockaddr_in * clientaddr);
ssize_t Rio_writen_w(int fd, void *usrbuf, size_t n);
ssize_t Rio_readnb_w(rio_t *rp, void *usrbuf, size_t n);
ssize_t Rio_readlineb_w(rio_t *rp, void *usrbuf, size_t maxlen);
void* thread(void*);

sem_t mutex;

/*
 * main - Main routine for the proxy program
 */
int main(int argc, char **argv)
{
    int listenfd;
    socklen_t clientlen;
    struct sockaddr_storage clientaddr;
    char hostname[MAXLINE],port[MAXLINE];
    char* connfdp_addr;
    pthread_t tid;

    Signal(SIGPIPE,SIG_IGN);

    /* Check arguments */
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <port number>\n", argv[0]);
        exit(0);
    }
    Sem_init(&mutex,0,1);

    listenfd = Open_listenfd(argv[1]);

    while(1){
        clientlen = sizeof(clientaddr);
        connfdp_addr = Malloc(sizeof(int)+sizeof(uint32_t));
        *((int*)connfdp_addr) = Accept(listenfd,(SA *)&clientaddr,&clientlen);
        Getnameinfo((SA*)&clientaddr,clientlen,hostname,MAXLINE,port,MAXLINE,NI_NUMERICHOST);
        inet_pton(AF_INET,hostname,connfdp_addr+sizeof(int));
        Pthread_create(&tid,NULL,thread,connfdp_addr);
    }

    exit(0);
}

void* thread(void* vargp){
    int connfd = *((int*)vargp);
    Pthread_detach(pthread_self());
    struct in_addr addr;
    addr.s_addr = *((uint32_t*)(vargp+sizeof(int)));
    struct sockaddr_in addr_in;
    addr_in.sin_family = AF_INET;
    addr_in.sin_addr = addr;
    Free(vargp);
    doit(connfd,&addr_in);
    Close(connfd);
    return NULL;
}

void doit(int fd,struct sockaddr_in * clientaddr){
    //struct stat sbuf;
    char client_buf[MAXLINE],server_buf[MAXLINE], method[MAXLINE], uri[MAXLINE], version[MAXLINE];
    char target_addr[MAXLINE], path[MAXLINE], port[MAXLINE];
    rio_t client_rio,server_rio;
    int req_body_size = 0,temp,write_result,read_result;

    /*Read request line and headers */ 
    Rio_readinitb(&client_rio, fd);
    read_result = Rio_readlineb_w(&client_rio, client_buf, MAXLINE);
    if(read_result == 0){
        printf("1\n");
        return;
    }
    sscanf(client_buf,"%s %s %s",method,uri,version);
    if(parse_uri(uri,target_addr,path,port) != 0){
        printf("Wrong request!\n");
        return; /*There should be call of client_error function */
    }


    // Build a request to target server
    char request[MAXLINE];
    sprintf(request,"%s /%s %s\r\n",method,path,version);
    while((read_result = Rio_readlineb_w(&client_rio,client_buf,MAXLINE)) > 2){
        if(!strncasecmp(client_buf,"Content-Length",14)){
            req_body_size = atoi(client_buf + 15);
        }
        sprintf(request,"%s%s",request,client_buf);
    }
    if(read_result == 0){
        printf("2\n");
        return;
    }
    sprintf(request,"%s\r\n",request);

    int clientfd;

    clientfd = Open_clientfd(target_addr,port);
    Rio_readinitb(&server_rio,clientfd);
    // Send the request we just built to target server.
    write_result = Rio_writen_w(clientfd,request,strlen(request));
    if(write_result!=strlen(request)){
        Close(clientfd);
        return;
    }
    // Send request body
    if(req_body_size != 0){
        temp = req_body_size; 
        while(temp > 0){
            read_result = Rio_readnb_w(&client_rio,client_buf,1);
            if(read_result != 1){
                printf("3\n");
                Close(clientfd); 
                return;
            }
            write_result = Rio_writen_w(clientfd,client_buf,1);
            if(write_result!=1){
                Close(clientfd);
                return;
            }
            temp -= 1;
        }
    }
    
    //we receive response header
    int res_body_size = 0,res_header_size = 0,res_header_temp; 
    while((res_header_temp = Rio_readlineb_w(&server_rio,server_buf,MAXLINE)) > 2){
        if(!strncasecmp(server_buf,"Content-Length",14)){
            res_body_size = atoi(server_buf + 15);
        }
        res_header_size += res_header_temp;
        write_result = Rio_writen_w(fd,server_buf,strlen(server_buf));
        if(write_result!= strlen(server_buf)){
            Close(clientfd);
            return;
        }
    }
    if(res_header_temp == 0){
        printf("4\n");
        Close(clientfd);
        return;
    }

    //write '\r\n'
    res_header_size += res_header_temp;
    write_result = Rio_writen_w(fd,server_buf,strlen(server_buf));
    if(write_result!= strlen(server_buf)){
        Close(clientfd);
        return;
    }

    // We forward the response body to client
    temp = res_body_size; 
    while(temp > 0){
        read_result = Rio_readnb_w(&server_rio,server_buf,1);
        if(read_result == 0){
            printf("5\n");
            Close(clientfd);
            return;
        }
        write_result = Rio_writen_w(fd,server_buf,1);
        if(write_result!= 1){
            Close(clientfd);
            return;
        }
        temp -= 1;
    }

    Close(clientfd);

    // Print log
    char logstring[MAXLINE];
    format_log_entry(logstring,clientaddr,uri,res_body_size + res_header_size);
    P(&mutex);
    printf("%s\n", logstring);
    V(&mutex);
}

/*
 * parse_uri - URI parser
 *
 * Given a URI from an HTTP proxy GET request (i.e., a URL), extract
 * the host name, path name, and port.  The memory for hostname and
 * pathname must already be allocated and should be at least MAXLINE
 * bytes. Return -1 if there are any problems.
 */
int parse_uri(char *uri, char *hostname, char *pathname, char *port)
{
    char *hostbegin;
    char *hostend;
    char *pathbegin;
    int len;

    if (strncasecmp(uri, "http://", 7) != 0) {
        hostname[0] = '\0';
        return -1;
    }

    /* Extract the host name */
    hostbegin = uri + 7;
    hostend = strpbrk(hostbegin, " :/\r\n\0");
    if (hostend == NULL)
        return -1;
    len = hostend - hostbegin;
    strncpy(hostname, hostbegin, len);
    hostname[len] = '\0';

    /* Extract the port number */
    if (*hostend == ':') {
        char *p = hostend + 1;
        while (isdigit(*p))
            *port++ = *p++;
        *port = '\0';
    } else {
        strcpy(port, "80");
    }

    /* Extract the path */
    pathbegin = strchr(hostbegin, '/');
    if (pathbegin == NULL) {
        pathname[0] = '\0';
    }
    else {
        pathbegin++;
        strcpy(pathname, pathbegin);
    }

    return 0;
}

/*
 * format_log_entry - Create a formatted log entry in logstring.
 *
 * The inputs are the socket address of the requesting client
 * (sockaddr), the URI from the request (uri), the number of bytes
 * from the server (size).
 */
void format_log_entry(char *logstring, struct sockaddr_in *sockaddr,
                      char *uri, size_t size)
{
    time_t now;
    char time_str[MAXLINE];
    unsigned long host;
    unsigned char a, b, c, d;

    /* Get a formatted time string */
    now = time(NULL);
    strftime(time_str, MAXLINE, "%a %d %b %Y %H:%M:%S %Z", localtime(&now));

    /*
     * Convert the IP address in network byte order to dotted decimal
     * form. Note that we could have used inet_ntoa, but chose not to
     * because inet_ntoa is a Class 3 thread unsafe function that
     * returns a pointer to a static variable (Ch 12, CS:APP).
     */
    host = ntohl(sockaddr->sin_addr.s_addr);
    a = host >> 24;
    b = (host >> 16) & 0xff;
    c = (host >> 8) & 0xff;
    d = host & 0xff;

    /* Return the formatted log entry string */
    sprintf(logstring, "%s: %d.%d.%d.%d %s %zu", time_str, a, b, c, d, uri, size);
}

ssize_t Rio_writen_w(int fd, void *usrbuf, size_t n) 
{
    ssize_t rc;

    if ((rc = rio_writen(fd, usrbuf, n)) != n){
        fprintf(stderr, "%s: %s\n", "Rio_writen error", strerror(errno));
        return 0;
    }
    return rc;
}


ssize_t Rio_readnb_w(rio_t *rp, void *usrbuf, size_t n) 
{
    ssize_t rc;

    if ((rc = rio_readnb(rp, usrbuf, n)) < 0){
        fprintf(stderr, "%s: %s\n", "Rio_readnb error", strerror(errno));
        return 0;
    }
    return rc;
}

ssize_t Rio_readlineb_w(rio_t *rp, void *usrbuf, size_t maxlen) 
{
    ssize_t rc;

    if ((rc = rio_readlineb(rp, usrbuf, maxlen)) < 0){
        fprintf(stderr, "%s: %s\n", "Rio_readlineb error", strerror(errno));
        return 0;
    }
    return rc;
} 


