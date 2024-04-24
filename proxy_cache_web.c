#include "cache.h"
#include "csapp.h"
#include <stdio.h>

// /* Recommended max cache and object sizes */
// #define MAX_CACHE_SIZE 1049000
// #define MAX_OBJECT_SIZE 102400

/* You won't lose style points for including this long line in your code */
static const char *request_hdr_format = "%s %s HTTP/1.0\r\n";
static const char *host_hdr_format = "Host: %s\r\n";
static const char *user_agent_hdr =
        "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 "
        "Firefox/10.0.3\r\n";
static const char *connection_hdr = "Connection: close\r\n";
static const char *proxy_connection_hdr = "Proxy-Connection: close\r\n";
static const char *EOL = "\r\n";

/* For cache */
LRU_Cache *cache;

void doit(int fd);

void read_requesthdrs(rio_t *rp);

void parse_uri(char *uri, char *hostname, char *port, char *filename);

int server_request(rio_t *client_rio, char *hostname, char *port, char *path,
                   char *method, char *hdr);

void clienterror(int fd, char *cause, char *errnum, char *shortmsg,
                 char *longmsg);

void *thread(void *vargp);

void sigint_handler(int sig) {
    printf("Exiting and freeing resources...\n");
    freeCache(cache);
    exit(0);
}

int main(int argc, char **argv) {
    signal(SIGINT, sigint_handler);
    int listenfd, *connfdp;
    char hostname[MAXLINE], port[MAXLINE];
    socklen_t clientlen;
    struct sockaddr_storage clientaddr;
    pthread_t tid;

    /* Check command line args */
    if (argc != 2) {
        fprintf(stderr, "usage: %s <port>\n", argv[0]);
        exit(1);
    }
    cache = createCache(MAX_CACHE_SIZE);

    listenfd = Open_listenfd(argv[1]);
    while (1) {
        clientlen = sizeof(clientaddr);
        connfdp = Malloc(sizeof(int));
        *connfdp = Accept(listenfd, (SA *) &clientaddr,
                          &clientlen); // line:netp:tiny:accept
        /*connfd = Accept(listenfd, (SA *)&clientaddr,
                        &clientlen); // line:netp:tiny:accept */
        Getnameinfo((SA *) &clientaddr, clientlen, hostname, MAXLINE, port, MAXLINE,
                    0);
        printf("Accepted connection from (%s, %s)\n", hostname, port);
        Pthread_create(&tid, NULL, thread, connfdp);
    }
    freeCache(cache);
}

void *thread(void *vargp) {
    int connfd = *((int *) vargp);
    Pthread_detach(pthread_self());
    Free(vargp);
    doit(connfd);
    Close(connfd);
    return NULL;
}

void doit(int fd) {
    int serverfd;
    char buf[MAXLINE], method[MAXLINE], uri[MAXLINE], version[MAXLINE],
            new_hdr[MAXLINE];
    char server_host[MAXLINE], server_port[MAXLINE], filename[MAXLINE];
    rio_t client_rio, server_rio;

    // 클라이언트와의 fd를 클라이언트용 client_rio에 연결
    Rio_readinitb(&client_rio, fd);
    Rio_readlineb(&client_rio, buf, MAXLINE);

    printf("Request headers:\n");
    printf("%s", buf);
    sscanf(buf, "%s %s %s", method, uri, version);
    if (strcasecmp(method, "GET")) {
        clienterror(fd, method, "501", "Not implemented",
                    "Tiny does not implement this method");
        return;
    }

    if (strstr(uri, "favicon"))
        return;

    parse_uri(uri, server_host, server_port, filename);
    if (!strlen(server_host)) {
        clienterror(fd, method, "501", "잘못된 요청",
                    "501 에러. 올바른 요청이 아닙니다.");
    }

    // 캐시 검사
    Node *cache_node = find_cache(cache, uri);
    if (cache_node) {             // 캐시 된 웹 객체가 있으면
        send_cache(fd, cache_node); // 캐싱된 웹 객체를 Client에 바로 전송
        moveToHead(cache,
                   cache_node); // 사용한 웹 객체의 순서를 맨 앞으로 갱신 후 return
        return;
    }

    // 원 서버에 연결
    serverfd = Open_clientfd(server_host, server_port);

    // 서버에 보낼 HTTP 요청메시지를 새로 생성
    if (!server_request(&client_rio, server_host, server_port, filename, method,
                        new_hdr))
        clienterror(fd, method, "501", "잘못된 요청",
                    "501 에러. 올바른 요청이 아닙니다.");

    // 서버에 요청메시지를 보냄
    Rio_readinitb(&server_rio, serverfd);
    Rio_writen(serverfd, new_hdr, strlen(new_hdr));

    // 서버 응답이 오면 클라이언트에게 전달
    char cache_buf[MAX_OBJECT_SIZE]; // 캐시 buf생성
    size_t size = 0;                 // 캐시 객체 사이즈 측정 변수
    size_t n;
    while ((n = Rio_readlineb(&server_rio, buf, MAXLINE)) > 0) {
        size += n;
        if (size < MAX_OBJECT_SIZE)
            strcat(cache_buf, buf);
        Rio_writen(fd, buf, n);
    }
    Close(serverfd);

    if (size < MAX_OBJECT_SIZE) // 캐시 가능한 크기면 캐시 추가
        add_cache(cache, uri, cache_buf, size);
}

int server_request(rio_t *client_rio, char *hostname, char *port, char *path,
                   char *method, char *hdr) {
    // 프록시서버로 들어온 요청을 서버에 전달하기 위해 HTTP 헤더 생성
    char req_hdr[MAXLINE], additional_hdr[MAXLINE], host_hdr[MAXLINE];
    char buf[MAXLINE];
    char *HOST = "Host";
    char *CONN = "Connection";
    char *UA = "User-Agent";
    char *P_CONN = "Proxy-Connection";
    sprintf(req_hdr, request_hdr_format, method, path); // method url version

    while (1) {
        if (Rio_readlineb(client_rio, buf, MAXLINE) == 0)
            break;
        if (!strcmp(buf, EOL))
            break; // buf == EOL => EOF

        if (!strncasecmp(buf, HOST, strlen(HOST))) {
            // 호스트 헤더 지정
            strcpy(host_hdr, buf);
            continue;
        }

        if (strncasecmp(buf, CONN, strlen(CONN)) &&
            strncasecmp(buf, UA, strlen(UA)) &&
            strncasecmp(buf, P_CONN, strlen(P_CONN))) {
            // 미리 준비된 헤더가 아니면 추가 헤더에 추가
            strcat(additional_hdr, buf);
            strcat(additional_hdr, "\r\n");
        }
    }

    if (!strlen(host_hdr)) {
        sprintf(host_hdr, host_hdr_format, hostname);
    }

    sprintf(hdr, "%s%s%s%s%s%s",
            req_hdr,  // METHOD URL VERSION
            host_hdr, // Host header
            user_agent_hdr, connection_hdr, proxy_connection_hdr, EOL);
    if (strlen(hdr))
        return 1;
    return 0;
}

void clienterror(int fd, char *cause, char *errnum, char *shortmsg,
                 char *longmsg) {
    char buf[MAXLINE], body[MAXBUF];

    /* Build the HTTP response body */
    sprintf(body, "<html><title>Tiny Error</title>");
    sprintf(body,
            "%s<body bgcolor="
            "ffffff"
            ">\r\n",
            body);
    sprintf(body, "%s%s: %s\r\n", body, errnum, shortmsg);
    sprintf(body, "%s<p>%s: %s\r\n", body, longmsg, cause);
    sprintf(body, "%s<hr><em>The Tiny Web server</em>\r\n", body);

    /* Print the HTTP response */
    sprintf(buf, "HTTP/1.0 %s %s\r\n", errnum, shortmsg);
    Rio_writen(fd, buf, strlen(buf));
    sprintf(buf, "Content-type: text/html\r\n");
    Rio_writen(fd, buf, strlen(buf));
    sprintf(buf, "Content-length: %d\r\n\r\n", (int) strlen(body));
    Rio_writen(fd, buf, strlen(buf));
    Rio_writen(fd, body, strlen(body));
}

void parse_uri(char *uri, char *hostname, char *port, char *filename) {
    char *ptr;
    char *hoststart, *first_hoststart;
    char *portstart;
    char *pathstart;
    int len;

    if (strncasecmp(uri, "http://", 7) == 0) {
        hoststart = uri + 7; /* 호스트 이름 시작 위치 */
    } else {
        hoststart = uri + 1; /* http://가 없는 경우 */
    }

    portstart = strchr(hoststart, ':');
    pathstart = strchr(hoststart, '/');

    if (pathstart) {
        if (portstart && portstart < pathstart) {
            /* 포트 번호가 있는 경우 */
            len = portstart - hoststart;
            strncpy(hostname, hoststart, len);
            hostname[len] = '\0';

            len = pathstart - portstart - 1;
            strncpy(port, portstart + 1, len);
            port[len] = '\0';
        } else {
            /* 포트 번호가 없는 경우 */
            len = pathstart - hoststart;
            strncpy(hostname, hoststart, len);
            hostname[len] = '\0';

            strcpy(port, "8080");
        }
        strcpy(filename, pathstart);
    } else {
        /* 경로가 명시되지 않은 경우 */
        if (portstart) {
            len = portstart - hoststart;
            strncpy(hostname, hoststart, len);
            hostname[len] = '\0';

            strcpy(port, portstart + 1);
        } else {
            strcpy(hostname, hoststart);
            strcpy(port, "8080"); // 기본 포트 설정
        }
        strcpy(filename, "/"); // 기본 경로
    }
}