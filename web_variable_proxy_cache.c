#include <stdio.h>

#include "csapp.h"

static const char *user_agent_hdr = "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 Firefox/10.0.3\r\n";

void doit(int connfd);

void parse_uri(char *uri, char *hostname, char *port, char *path);

void build_http_header(char *http_header, char *method, char *path, char *user_agent_hdr);

int connect_endServer(char *hostname, int port, char *http_header);

void *thread(void *vargp);

/* Recommended max cache and object sizes */
#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400
#define DEFAULT_CACHE_SIZE 1024;
#define HASH_TABLE_SIZE 100
unsigned int currentCacheSize = 0; // 현재 캐시 크기

typedef struct CacheEntry {
    char *key; // path 문자열
    char *value; // 캐시된 내용
    ssize_t size;
    struct CacheEntry *prev; // 충돌 발생 시 다음 엔트리를 가리킴
    struct CacheEntry *next; // 충돌 발생 시 다음 엔트리를 가리킴
} CacheEntry;

typedef struct Cache {
    ssize_t capacity;
    CacheEntry *head;
    CacheEntry *next;
} Cache;

// 전역 캐시풀
static Cache *cache_pool;

unsigned int currentCacheSize;

// 캐시 항목을 생성시킨다.
CacheEntry *putCache(char *key, char *value, ssize_t cacheSize) {
    unsigned int newEntrySize = strlen(newEntry->value);
    if (currentCacheSize + newEntrySize > MAX_CACHE_SIZE) {
        printf("캐시 크기 초과: 캐시에서 항목을 제거해야 합니다.\n");
        return NULL;
    }

    CacheEntry *newEntry = (CacheEntry *) malloc(sizeof(CacheEntry));
    newEntry->key = strdup(key); //문자열의 복사본을 새로운 메모리 공간에 할당
    newEntry->value = (char *) malloc(size);

    memccpy(newEntry->value, value, cacheSize);
    newEntry->size = cacheSize;
    newEntry->next = NULL;
    newEntry->prev = NULL;
    currentCacheSize += newEntrySize;
    return newEntry;
}

int is_available_cache(char *data) {
    char *content_length_start = strcasestr(data, "Content-Length: "); // "Content-Length: " 문자열 찾기
    // "Content-Length: " 다음의 문자열에서 숫자를 읽어옴
    int content_length;
    sscanf(content_length_start + strlen("Content-Length: "), "%d", &content_length);

    // content_length와 MAX_OBJECT_SIZE를 비교하여 캐시의 크기 제한을 확인
    if (content_length <= MAX_OBJECT_SIZE) {
        return 1;
    } else {
        return 0;
    }
}

// 캐시에서 데이터 가져오기
void moveToHead(Cache *cache, CacheEntry *curr) {
    if (curr == cache->head) return; // 이미 head인 경우는 처리하지 않음

    // 현재 항목을 연결 리스트에서 제거
    curr->prev->next = curr->next;
    if (curr->next != NULL) {
        curr->next->prev = curr->prev;
    } else {
        cache->next = curr->prev;
    }

    // 현재 항목을 head로 옮김
    curr->next = cache->head;
    curr->prev = NULL;
    cache->head->prev = curr;
    cache->head = curr;
}

// 캐시에서 데이터를 가져오는 함수
char *getCache(Cache *cache, char *key) {
    CacheEntry *curr = cache->head;
    while (curr != NULL) {
        if (strcmp(curr->key, key) == 0) {
            moveToHead(cache, curr);
            return curr->value;
        }
        curr = curr->next;
    }
    return NULL;
}

Cache *initCache() {
    Cache *cache = (Cache *) malloc(sizeof(Cache));
    cache->capacity = MAX_CACHE_SIZE;
    cache->head = NULL;
    cache->next = NULL;
    return cache;
}

//                    MAIN
int main(int argc, char **argv) {
    int listenfd, *connfd;
    char hostname[MAXLINE], port[MAXLINE];
    socklen_t clientlen; //주소의 길이
    struct sockaddr_storage clientaddr;
    pthread_t tid; //스레드의 식별자(identifier)를 정의
    /* Check command line args */

    cache_pool = cachinitCacge();

    if (argc != 2) {
        fprintf(stderr, "usage: %s <port>\n", argv[0]);
        exit(1);
    }

    listenfd = Open_listenfd(argv[1]); // 지정된 포트에서 수신 소켓 생성
    while (1) {
        clientlen = sizeof(clientaddr);

        connfd = Malloc(sizeof(int));
        *connfd = Accept(listenfd, (SA *) &clientaddr, &clientlen); // 클라이언트 연결 요청의 수
        printf("Original Socket Number : %d", connfd);
        Getnameinfo((SA *) &clientaddr, clientlen, hostname, MAXLINE, port, MAXLINE,
                    0); // 클라이언트 호스트 이름, 포트 번호
        printf("Accepted connection from (%s, %s)\n", hostname, port);
        /*doit(connfd); // sequential handle
        Close(connfd);*/
        Pthread_create(&tid, NULL, doit, connfd);
    }
}

/*void *thread(void *vargp) {
    int connfd = *((int *) vargp);
    doit(connfd);
    Close(connfd);
    return NULL;
}*/

void doit(void *vargp) {
    int connfd = *((int *) vargp);
    Pthread_detach(pthread_self()); //리소스 반환을 자동으로해주는 분리상태


    char buf[MAXLINE], method[MAXLINE], uri[MAXLINE], data_buf[MAX_OBJECT_SIZE], version[MAX_OBJECT_SIZE];
    char filename[MAXLINE], hostname[MAXLINE], port[MAXLINE], key[MAXLINE], head_header[MAXLINE];
    char *caching;
    ssize_t cache_size;

    struct sockaddr_in socketaddr;
    int clientfd;
    rio_t rio;

    /* 클라이언트의 요청 읽기 */
    Rio_readinitb(&rio, connfd);
    Rio_readlineb(&rio, buf, MAXLINE); // 클라이언트로부터 요청 라인을 읽음

    printf("Request header: %s\n", buf);
    /* 요청 메소드, URI 읽기 */
    sscanf(request_buf, "%s %s %s", method, uri, version);

    /* URI가 "/favicon.ico"인 경우에는 더 이상의 처리를 수행하지 않고 함수를 종료 */
    if (!strcasecmp(uri, "/favicon.ico"))
        return;

    /* URI 파싱하여 호스트명, 포트, 경로 추출 */
    parse_uri(uri, hostname, port, path);

    /*캐싱영역 ==============================*/

    printf("hostname: %s", hostname);
    printf("port: %s", port);
    clientfd = socket(AF_INET, SOCK_STREAM, 0);
    if (clientfd == -1) {
        printf("Socke Connection Reject");
        return NULL;
    }
    build_http_header(data_buf,method,hostname,filename,&rio,head_header);
    strcat(key, hostname);
    strcat(key, filename);

    caching = getCache(cache_pool,key);

    //cache Hit
    if (caching != NULL){
        Rio_writen(connfd,caching,MAX_OBJECT_SIZE);

        free(vargp);
        Close(clientfd);
        Close(connfd);
        return NULL;
    }

    //Cache Miss
    if (strcmp(hostname,"localhost") == 0){
        strcpy(hostname,"127.0.0.1");
    }

    memset(&socketaddr,0,sizeof(socketaddr));


    //socket 구조체 IP,PORT, 소켓 구성 설정
    socketaddr.sin_family=AF_INET;
    socketaddr.sin_addr.s_addr= inet_addr(hostname);
    socketaddr.sin_port= htons(atoi(port));

    if (connect(clientfd,(SA *)&socketaddr,sizeof(socketaddr))!=0){
        printf("conenction fial");
        return NULL;
    }

    char server_header[MAXLINE];
    Rio_writen(clientfd, head_header, MAXLINE);

    Rio_readn(clientfd, server_header, MAX_OBJECT_SIZE);

    Close(clientfd);

    clientfd = socket(AF_INET, SOCK_STREAM, 0);

    if (connect(clientfd, (SA *) &servaddr, sizeof(servaddr)) != 0) {
        return NULL;
    }

    if (is_available_cache(server_header) == 0) {
        Rio_writen(clientfd, data_buf, MAXLINE);

        memset(data_buf, 0, MAXLINE);

        while (Rio_readn(clientfd, data_buf, MAXLINE) > 0) {
            Rio_writen(connfd, data_buf, MAXLINE);
            memset(data_buf, 0, MAXLINE);
        }

        free(vargp);
        // 요청 및 데이터 전달 완료 후 clientfd, connfd close
        Close(clientfd);
        Close(connfd);

        return NULL;
    }

    // 서버로 요청 전송 및 응답 데이터 저장
    request_to_server(clientfd, data_buf, &cache_size);

    // 제대로 된 데이터가 들어왔는지 확인
    if (0 < cache_size) {
        put_cache(cache_pool, key, data_buf, cache_size);
    }

    // 서버로부터 받은 data 를 client 에 전송
    Rio_writen(connfd, data_buf, MAX_OBJECT_SIZE);

    free(vargp);
    // 요청 및 데이터 전달 완료 후 clientfd, connfd close
    Close(clientfd);
    Close(connfd);

    return NULL;

    /*캐싱영역 ==============================*/


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

    Rio_writen(serverfd, buf, strlen(buf)); // 서버에 요청 전송

    /* 서버로부터 응답 받아 클라이언트에 전송 */
    ssize_t n;
    n = Rio_readn(serverfd, response_buf, MAX_OBJECT_SIZE); // 서버로부터 OBJECT_SIZE 만큼 응답을 읽음
    Rio_writen(clientfd, response_buf, n); // 클라이언트에게 응답을 전송

    Close(serverfd); // 서버 연결 종료
}

void build_http_header(char *buf, char *method, char *hostname, char *path, rio_t *rp, char *head_header) {
    char result[MAXLINE];

    memset(result,0,MAXLINE)
    sprintf(buf, "%s /%s %s\r\n", method, path, "HTTP/1.0");
    sprintf(buf, "%s%s\r\n", buf, user_agent_hdr);
    sprintf(buf, "%sConnection: close\r\n", buf);
    sprintf(buf, "%sProxy-Connection: close\r\n", buf);

    sprintf(head_header, "%s %s %s\r\n", "HEAD", path, "HTTP/1.0");
    sprintf(head_header, "%s%s\r\n", head_header, user_agent_hdr);
    sprintf(head_header, "%sConnection: close\r\n", head_header);
    sprintf(head_header, "%sProxy-Connection: close\r\n", head_header);

    int host_flag = 0;

    while (strcmp(result, "\r\n")) {
        Rio_readlineb(rp, result, MAXLINE);
        if (strcasestr(result, "GET") || strcasestr(result, "HEAD") || strcasestr(result, "User-Agent") ||
            strcasestr(result, "Connection") || strcasestr(result, "Proxy-Connection")) {
            continue;
        } else if (strcasestr(result, "HOST: ")) {
            host_flag++;
        }
        strcat(buf, result);
        strcat(head_header, result);
    }

    if (!host_flag) {
        sprintf(buf, "%sHost: %s", buf, hostname);
        sprintf(buf, "%sHost: %s", head_header, hostname);
    }

    strcat(buf, "\r\n");
}

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