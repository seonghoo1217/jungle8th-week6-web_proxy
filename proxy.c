#include <stdio.h>

#include "csapp.h"

static const char *user_agent_hdr = "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 Firefox/10.0.3\r\n";

void doit(int connfd);

void parse_uri(char *uri, char *hostname, char *port, char *path);

void build_http_header(char *http_header, char *method, char *path, char *user_agent_hdr);

int connect_endServer(char *hostname, int port, char *http_header);

/* Recommended max cache and object sizes */
#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400

int main(int argc, char **argv) {
    int listenfd, connfd;
    char hostname[MAXLINE], port[MAXLINE];
    socklen_t clientlen;
    struct sockaddr_storage clientaddr;

    /* Check command line args */
    if (argc != 2) {
        fprintf(stderr, "usage: %s <port>\n", argv[0]);
        exit(1);
    }

    listenfd = Open_listenfd(argv[1]); // 지정된 포트에서 수신 소켓 생성
    while (1) {
        clientlen = sizeof(clientaddr);
        connfd = Accept(listenfd, (SA *) &clientaddr,
                        &clientlen); // 클라이언트 Request Accept
        Getnameinfo((SA *) &clientaddr, clientlen, hostname, MAXLINE, port, MAXLINE,
                    0); // 클라이언트 호스트 이름, 포트 번호
        printf("Accepted connection from (%s, %s)\n", hostname, port);
        doit(connfd); // sequential handle
        Close(connfd);
    }
}


void doit(int clientfd) {
    int serverfd;
    char request_buf[MAXLINE], response_buf[MAX_OBJECT_SIZE];
    char method[MAXLINE], uri[MAXLINE], path[MAXLINE];
    char hostname[MAXLINE], port[MAXLINE];
    rio_t request_rio, response_rio;

    /* 클라이언트의 요청 읽기 */
    Rio_readinitb(&request_rio, clientfd); // 클라이언트 소켓 디스크립터를 리오 버퍼에 연결
    Rio_readlineb(&request_rio, request_buf, MAXLINE); // 클라이언트로부터 요청 라인을 읽음
    printf("Request header: %s\n", request_buf);
    /* 요청 메소드, URI 읽기 */
    sscanf(request_buf, "%s %s", method, uri);

    /* URI가 "/favicon.ico"인 경우에는 더 이상의 처리를 수행하지 않고 함수를 종료 */
    if (!strcasecmp(uri, "/favicon.ico"))
        return;

    /* URI 파싱하여 호스트명, 포트, 경로 추출 */
    parse_uri(uri, hostname, port, path);

    printf("uri: %s\n", uri); // 디버깅용 URI 출력

    /* 새로운 요청 구성 */
    sprintf(request_buf, "%s /%s %s\r\n", method, path, "HTTP/1.0");
    printf("%s\n", request_buf);
    sprintf(request_buf, "%sConnection: close\r\n", request_buf);
    sprintf(request_buf, "%sProxy-Connection: close\r\n", request_buf);
    sprintf(request_buf, "%s%s\r\n", request_buf, user_agent_hdr);

    /* 요청 메소드가 GET 또는 HEAD가 아닌 경우 오류 응답 전송 */
    if (strcasecmp(method, "GET") && strcasecmp(method, "HEAD")) {
        printf("Proxy does not implement this method");
        // clienterror(clientfd, method, "501", "Not Implemented", "Proxy does not implement this method");
        return;
    }

    /* 원격 서버에 클라이언트의 요청 전송 */
    serverfd = Open_clientfd(hostname, port); // 서버로의 연결 생성
    if (serverfd < 0) { // 연결 실패 시
        printf("Proxy couldn't connect to the server");
        // clienterror(clientfd, hostname, "404", "Not found", "Proxy couldn't connect to the server");
        return;
    }

    printf("%s\n", request_buf);
    Rio_writen(serverfd, request_buf, strlen(request_buf)); // 서버에 요청 전송

    /* 서버로부터 응답 받아 클라이언트에 전송 */
    ssize_t n;
    n = Rio_readn(serverfd, response_buf, MAX_OBJECT_SIZE); // 서버로부터 OBJECT_SIZE 만큼 응답을 읽음
    Rio_writen(clientfd, response_buf, n); // 클라이언트에게 응답을 전송

    Close(serverfd); // 서버 연결 종료
}

// void build_http_header(char *http_header, char *method, char *path, char *user_agent_hdr) {
//     // 기본적인 GET 요청 라인
//     sprintf(http_header, "%s %s HTTP/1.0\r\n", method, path);
//     // 연결을 계속 유지하지 않고 종료함을 명시 (HTTP/1.0 기준으로 Keep-Alive는 기본적으로 활성화되지 않음)
//     sprintf(http_header + strlen(http_header), "Connection: close\r\n");
//     // 프록시 연결을 종료함을 명시
//     sprintf(http_header + strlen(http_header), "Proxy-Connection: close\r\n");
//     // User-Agent 헤더 추가
//     sprintf(http_header + strlen(http_header), "%s", user_agent_hdr);
// }

void parse_uri(char *uri, char *hostname, char *port, char *path) {
    char *hostname_ptr = strstr(uri, "//") ? strstr(uri, "//") + 2 : uri + 1;
    char *port_ptr = strstr(hostname_ptr, ":");
    char *path_ptr = strstr(hostname_ptr, "/");

    // 경로 처리
    if (path_ptr) {
        *path_ptr = '\0'; // 경로 시작 부분을 NULL로 변경하여 호스트 이름과 포트 번호를 분리
        strcpy(path, path_ptr + 1); // 경로 복사
    } else {
        strcpy(path, ""); // 경로가 없는 경우 빈 문자열로 설정
    }

    // 포트 번호 처리
    if (port_ptr && port_ptr < path_ptr) { // 포트 번호가 있고, 경로 시작 전에 위치하는 경우
        *port_ptr = '\0'; // 포트 시작 부분을 NULL로 변경하여 호스트 이름을 분리
        strcpy(port, port_ptr + 1); // 포트 번호 복사
    } else {
        strcpy(port, "80"); // 포트 번호가 명시되지 않은 경우 기본값으로 "80" 설정
    }

    // 호스트 이름 처리
    strcpy(hostname, hostname_ptr); // 호스트 이름 복사
}
