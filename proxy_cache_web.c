#include <stdio.h>

#include "csapp.h"

static const char *user_agent_hdr = "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 Firefox/10.0.3\r\n";

/* Recommended max cache and object sizes */
#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400

typedef struct Node {
    char url[MAXLINE];
    char *data;
    size_t data_length;
    struct Node *prev, *next;
} Node;

typedef struct {
    Node *head, *tail;
    int capacity, size;
} LRU_Cache;


static const char *request_hdr_format = "%s %s HTTP/1.0\r\n";
static const char *host_hdr_format = "Host: %s\r\n";
static const char *connection_hdr = "Connection: close\r\n";
static const char *proxy_connection_hdr = "Proxy-Connection: close\r\n";
static const char *EOL = "\r\n";
LRU_Cache *cache;

void doit(int connfd);

void parse_uri(char *uri, char *hostname, char *port, char *path);

int build_http_header(rio_t *client_rio, char *hostname, char *port, char *path, char *method, char *hdr);

int connect_endServer(char *hostname, int port, char *http_header);

void *thread(void *vargp);

static Node *createNode(char *url, char *data, size_t data_length);

LRU_Cache *createCache(int capacity);

void moveToHead(LRU_Cache *cache, Node *node);

Node *find_cache(LRU_Cache *cache, char *url);

void send_cache(int fd, Node *node);

void add_cache(LRU_Cache *cache, char *url, char *data, size_t data_length);

void freeCache(LRU_Cache *cache);

// 캐시 노드단위 생성
static Node *createNode(char *url, char *data, size_t data_length) {
    Node *newNode = (Node *) malloc(sizeof(Node));
    if (!newNode)
        return NULL;
    strcpy(newNode->url, url);
    newNode->data = (char *) malloc(data_length);
    if (!newNode->data) {
        free(newNode);
        return NULL;
    }
    memcpy(newNode->data, data, data_length);
    newNode->data_length = data_length;
    newNode->prev = newNode->next = NULL;
    return newNode;
}

// 캐시풀 전체 사용 생성
LRU_Cache *createCache(int capacity) {
    LRU_Cache *cache = (LRU_Cache *) malloc(sizeof(LRU_Cache));
    if (!cache)
        return NULL;
    cache->capacity = capacity;
    cache->size = 0;
    cache->head = cache->tail = NULL;
    return cache;
}

// LRU Algorithm
void moveToHead(LRU_Cache *cache, Node *node) {
    if (node == cache->head)
        return;
    if (node == cache->tail) {
        cache->tail = node->prev;
        cache->tail->next = NULL;
    } else if (node->prev) {
        node->prev->next = node->next;
        node->next->prev = node->prev;
    }
    node->next = cache->head;
    node->prev = NULL;
    cache->head->prev = node;
    cache->head = node;
}

// find to path uri
Node *find_cache(LRU_Cache *cache, char *url) {
    Node *node = cache->head;
    while (node) {
        if (strcmp(node->url, url) == 0) {
            return node;
        }
        node = node->next;
    }
    return NULL;
}

// 캐시된 데이터를 클라이언트에게 전송
void send_cache(int fd, Node *node) {
    Rio_writen(fd, node->data, node->data_length);
}

// 캐시에 새로운 데이터를 추가
void add_cache(LRU_Cache *cache, char *url, char *data, size_t data_length) {
    if (cache->size == cache->capacity) {
        Node *tail = cache->tail;
        if (cache->head == cache->tail) {
            cache->head = cache->tail = NULL;
        } else {
            cache->tail = tail->prev;
            cache->tail->next = NULL;
        }
        free(tail->data);
        free(tail);
        cache->size--;
    }
    Node *newNode = createNode(url, data, data_length);
    if (!newNode)
        return;
    if (cache->head == NULL) {
        cache->head = cache->tail = newNode;
    } else {
        newNode->next = cache->head;
        cache->head->prev = newNode;
        cache->head = newNode;
    }
    cache->size++;
}

// 캐시 해제
void freeCache(LRU_Cache *cache) {
    Node *current = cache->head;
    while (current != NULL) {
        Node *next = current->next;
        free(current->data); // 데이터 메모리 해제
        free(current);       // 노드 메모리 해제
        current = next;
    }
    free(cache);
}

void client_signal_connection(int sig) {
    freeCache(cache);
    exit(0);
}


int main(int argc, char **argv) {
    signal(SIGINT, client_signal_connection);

    int listenfd, *connfd;
    char hostname[MAXLINE], port[MAXLINE];
    socklen_t clientlen; //주소의 길이
    struct sockaddr_storage clientaddr;
    pthread_t tid; //스레드의 식별자(identifier)를 정의
    /* Check command line args */
    if (argc != 2) {
        fprintf(stderr, "usage: %s <port>\n", argv[0]);
        exit(1);
    }
    cache = createCache(MAX_CACHE_SIZE);
    listenfd = Open_listenfd(argv[1]); // 지정된 포트에서 수신 소켓 생성
    while (1) {
        clientlen = sizeof(clientaddr);

        connfd = Malloc(sizeof(int));
        *connfd = Accept(listenfd, (SA *) &clientaddr, &clientlen); // 클라이언트 연결 요청의 수
        printf("Original Socket Number : %d", connfd);
        Getnameinfo((SA *) &clientaddr, clientlen, hostname, MAXLINE, port, MAXLINE,
                    0); // 클라이언트 호스트 이름, 포트 번호
        /*doit(connfd); // sequential handle
        Close(connfd);*/
        Pthread_create(&tid, NULL, thread, connfd);
    }
    freeCache(cache);
}

void *thread(void *vargp) {
    int connfd = *((int *) vargp);
    Pthread_detach(pthread_self()); //리소스 반환을 자동으로해주는 분리상태
    Free(vargp);
    doit(connfd);
    Close(connfd);
    return NULL;
}

void doit(int clientfd) {
    int serverfd;
    char buf[MAXLINE], method[MAXLINE], uri[MAXLINE], version[MAXLINE], server_hdr[MAXLINE];
    char host[MAXLINE], port[MAXLINE], path[MAXLINE];
    rio_t request_rio, response_rio;

    /* 클라이언트의 요청 읽기 */
    Rio_readinitb(&request_rio, clientfd); // 클라이언트 소켓 디스크립터를 리오 버퍼에 연결
    Rio_readlineb(&request_rio, buf, MAXLINE); // 클라이언트로부터 요청 라인을 읽음

    /* 요청 메소드, URI 읽기 */
    sscanf(buf, "%s %s %s", method, uri, version);

    /* URI가 "/favicon.ico"인 경우에는 더 이상의 처리를 수행하지 않고 함수를 종료 */
    if (!strcasecmp(uri, "/favicon.ico"))
        return;
    /* 요청 메소드가 GET 또는 HEAD가 아닌 경우 오류 응답 전송 */
    if (strcasecmp(method, "GET") && strcasecmp(method, "HEAD")) {
        printf("Proxy does not implement this method");
        // clienterror(clientfd, method, "501", "Not Implemented", "Proxy does not implement this method");
        return;
    }
    /* URI 파싱하여 호스트명, 포트, 경로 추출 */
    parse_uri(uri, host, port, path);

    /*캐싱영역 ==============================*/
    Node *cache_node = find_cache(cache, uri);
    if (cache_node) {
        send_cache(clientfd, cache_node);
        moveToHead(cache, cache_node);//사용한 캐싱 순서를 맨 앞으로 갱신 후 return
        return;
    }

    /*캐싱영역 ==============================*/


//    printf("uri: %s\n", uri); // 디버깅용 URI 출력
    serverfd = Open_clientfd(host, port);

    if (!build_http_header(&request_rio, host, port, path, method, server_hdr)) {
        printf("Client Connection Error");
    }
    Rio_readinitb(&response_rio, serverfd);
    Rio_writen(serverfd, server_hdr, strlen(server_hdr));

    char cache_buf[MAX_OBJECT_SIZE];
    size_t size = 0;
    size_t n;
    while ((n = Rio_readlineb(&response_rio, buf, MAXLINE)) > 0) {
        size += n;
        if (size < MAX_OBJECT_SIZE)
            strcat(cache_buf, buf);
        Rio_writen(clientfd, buf, n);
    }
    Close(serverfd);

    if (size < MAX_OBJECT_SIZE)
        add_cache(cache, uri, cache_buf, size);
}

int build_http_header(rio_t *client_rio, char *hostname, char *port, char *path,
                      char *method, char *hdr) {
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


void parse_uri(char *uri, char *hostname, char *port, char *filename) {
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