#include <stdio.h>
#include <time.h>
#include "csapp.h"  // 기본적인 시스템 호출 및 소켓 관련 함수들

/* 최대 캐시 및 객체 크기 정의 */
#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400

// 캐시 구조체
typedef struct {
    char *request;     // 요청의 복사본을 가리키는 포인터
    char *response;    // 응답 본체를 가리키는 포인터
    size_t size;       // 응답 본체의 크기
    time_t timestamp;  // 최근 접근 시간
} CacheEntry;

typedef struct {
    CacheEntry *entries;
    int count;
    int capacity;
    size_t current_size;
    pthread_mutex_t lock;
} Cache;

// 캐시 선언
Cache cache;
int capacity = 10000;  // 캐시에 저장될 객체 수. 검색시간이 너무 늘어나지않게 혹은 캐시 삭제 확인해보기 위해 추가함.

// function prototype
int doit(int fd);

void *thread(void *vargp);

void forward_request(int clientfd, int serverfd, char *request);

int parse_uri(char *uri, char *hostname, char *port, char *path);

void clienterror(int fd, char *cause, char *errnum, char *shortmsg, char *longmsg);

// cache function prototype
void cache_init(Cache *cache, int capacity);

void cache_free(Cache *cache);

int cache_find(Cache *cache, char *request);

void cache_evict(Cache *cache);

void cache_add(Cache *cache, char *request, char *response, size_t size);

void cache_retrieve(Cache *cache, int index, char *buf, size_t buf_size);

/* You won't lose style points for including this long line in your code */
static const char *user_agent_hdr =
        "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 "
        "Firefox/10.0.3\r\n";

int main(int argc, char **argv) {
    int listenfd, *connfdp;                               // listenfd는 리스팅 소켓의 fd, connfd는 연결 소켓의 fd
    socklen_t clientlen;                                  // clientlen은 클라이언트 주소 구조체의 크기를 저장. Accept 함수에서 사용
    struct sockaddr_storage clientaddr;                   // client 주소 정보를 저장하기 위한 큰 구조체. ipv4 와 ipv6 모두 지원
    char client_hostname[MAXLINE], client_port[MAXLINE];  // 클라이언트의 호스트 이름과 포트 번호를 저장하는 배열
    pthread_t tid;

    if (argc != 2) {
        fprintf(stderr, "usage: %s <port>\n", argv[0]);
        exit(0);
    }

    cache_init(&cache, capacity);  // 캐시 초기화
    signal(SIGPIPE, SIG_IGN);      // SIGPIPE 시그널을 무시한다. (연결이 끊긴 파일디스크립터에 write를 하려고할때 생기는 시그널)

    listenfd = Open_listenfd(argv[1]);                               // 지정된 포트로 리스닝 소켓을 열고, 해당 소켓의 fd를 listenfd에 저장
    while (1) {                                                      // 무한 루프를 시작합니다. 서버는 계속해서 클라이언트의 연결 요청을 기다립니다.
        clientlen = sizeof(struct sockaddr_storage);                 // 클라이언트 주소 구조체의 크기를 clientlen에 저장합니다.
        connfdp = Malloc(
                sizeof(int));                               // 쓰레드 할당문이 accept 후에 완료된다면, 쓰레드의 지역 connfd 변수가 달라질수있어서 말록으로 저장한다.
        *connfdp = Accept(listenfd, (SA *) &clientaddr,
                          &clientlen);  // 클라이언트의 연결 요청을 수락합니다. 수락된 연결의 소켓 디스크립터를 connfd에 저장합니다.
        Getnameinfo((SA *) &clientaddr, clientlen,                    // 클라이언트의 주소 정보를 사용하여,
                    client_hostname, MAXLINE,                        // 호스트 이름과
                    client_port, MAXLINE, 0);                        // 포트 번호를 문자열로 변환합니다.
        printf("\n######### New Connection #########\n");
        printf("Connected to (%s, %s)\n", client_hostname, client_port);  // 클라이언트의 호스트 이름과 포트 번호를 출력합니다.
        printf("%s\n", user_agent_hdr);
        Pthread_create(&tid, NULL, thread, connfdp);
    }
    cache_free(&cache);
    return 0;  // 메인 함수 종료
}

// Thread routine
void *thread(void *vargp) {
    int connfd = *((int *) vargp);
    Pthread_detach(pthread_self());
    Free(vargp);
    doit(connfd);
    Close(connfd);
    return NULL;
}

int doit(int fd) {
    char buf[MAXLINE], method[MAXLINE], uri[MAXLINE], version[MAXLINE];
    char hostname[MAXLINE], path[MAXLINE], port[MAXLINE];
    rio_t rio, server_rio;
    int serverfd;
    char request[MAXLINE];

    // Read request line and headers
    Rio_readinitb(&rio, fd);                 // rio 구조체 초기화 및 fd와 연결
    if (!Rio_readlineb(&rio, buf, MAXLINE))  // 클라이언트로 부터 요청라인 첫줄 읽기
        return;

    printf("Request: %s\n", buf);
    strcpy(request, buf);

    sscanf(buf, "%s %s %s", method, uri, version);  // 요청 라인 파싱하여 메소드 uri, 버전 정보 추출
    if (strcasecmp(method, "GET") && strcasecmp(method, "HEAD")) {
        clienterror(fd, method, "501", "Not Implemented", "Proxy does not implement this method");
        return;
    }

    // Parse URI from GET request
    if (!parse_uri(uri, hostname, port, path)) {
        clienterror(fd, uri, "400", "Bad Request", "Proxy received a malformed request");
        return;
    }

    /* Check if the request is for favicon.ico and ignore it */
    if (strstr(uri, "favicon.ico")) {
        printf("Ignoring favicon.ico request\n\n");
        return;  // Just return without sending any response
    }

    // 캐시 검색
    int cache_index = cache_find(&cache, buf);
    if (cache_index != -1) {
        // 캐시 히트 했다면 캐시 전달.
        printf("Cache hit. Retrieving from cache...\n");
        char cached_response[MAX_OBJECT_SIZE];
        size_t response_size = cache.entries[cache_index].size;
        cache_retrieve(&cache, cache_index, cached_response, response_size);
        rio_writen(fd, cached_response, response_size);
        return;
    }
    // 없다면(캐시 미스) 아래 실행
    printf("Cache miss. Fetching from server ...\n");

    // Connect to the target server
    printf("Connect to {method: %s, hostname: %s, port: %s, path: %s}\n", method, hostname, port, path);
    serverfd = open_clientfd(hostname, port);  // 대상 서버에 연결

    if (serverfd < 0) {
        fprintf(stderr, "Connection to %s on port %s failed.\n\n", hostname, port);
        clienterror(fd, "Connection Failed", "503", "Service Unavailable",
                    "The proxy server could not retrieve the resource.");
        return;
    }

    // Write the HTTP headers to the server
    sprintf(buf, "GET %s HTTP/1.0\r\n", path);
    sprintf(buf, "%sHost: %s\r\n", buf, hostname);
    sprintf(buf, "%s%s\r\n", buf, user_agent_hdr);
    sprintf(buf, "%sConnection: close\r\n", buf);
    rio_writen(serverfd, buf, strlen(buf));

    // Transfer the response back to the client
    // and save in to cache
    Rio_readinitb(&server_rio, serverfd);
    forward_request(fd, serverfd, request);
    Close(serverfd);
    return 0;
}

// Forward the HTTP response from server to client
void forward_request(int clientfd, int serverfd, char *request) {
    char buf[MAXLINE];
    char *response = Malloc(MAX_OBJECT_SIZE);  // heap 영역에 저장하기위해.

    ssize_t n;
    ssize_t received_size = 0;

    while ((n = Rio_readn(serverfd, buf, MAXLINE)) > 0) {
        rio_writen(clientfd, buf, n);
        // printf("Proxy received %zd bytes, now sending...\n", n);
        if (received_size + n <= MAX_OBJECT_SIZE) {
            memcpy(response + received_size, buf, n);
        }
        received_size += n;
    }
    printf("Proxy received %zd bytes, and sended to client\n", received_size);

    if (received_size <= MAX_OBJECT_SIZE) {
        cache_add(&cache, request, response, received_size);
    }

    Free(response);
}

// parse uri
int parse_uri(char *uri, char *hostname, char *port, char *pathname) {
    char *pos;
    char *start;

    if (uri == NULL) return 0;

    // skip protocol (http:// or https://)
    start = strstr(uri, "://");
    if (start != NULL) {
        start += 3;
    } else {
        start = uri;
    }

    // find port num pos
    pos = strchr(start, ':');
    if (pos != NULL) {
        *pos = '\0';
        sscanf(start, "%s", hostname);
        sscanf(pos + 1, "%[^/]", port);
        /*'/' 문자가 나타나기 전까지의 모든 문자를 읽음.
         * % 형식 지정자의 시작. '[', ']'는 문자 집합의 시작과 끝. '^'는 이 기호 바로 뒤 문자를 제외한 문자를 읽어라.
         */
        start = pos + strlen(port) + 1;
    } else {
        // 포트가 명시되지 않았다면 기본 포트를 사용.
        pos = strchr(start, '/');
        if (pos != NULL) {
            *pos = '\0';
            sscanf(start, "%s", hostname);
            *pos = '/';
            strcpy(port, "8080");
        } else {
            sscanf(start, "%s", hostname);
            strcpy(port, "8080");
            strcpy(pathname, "/");
            return 1;
        }
        start = pos;
    }

    // 경로 추출
    if (*start == '/') {
        sscanf(start, "%s", pathname);
    } else {
        return -1;
    }

    return 1;
}

void clienterror(int fd, char *cause, char *errnum, char *shortmsg, char *longmsg) {
    char buf[MAXLINE], body[MAXBUF];

    /* Build the HTTP response body */
    sprintf(body, "<html><title>Proxy Error</title>");
    sprintf(body,
            "%s<body bgcolor="
            "ffffff"
            ">\r\n",
            body);
    sprintf(body, "%s%s: %s\r\n", body, errnum, shortmsg);
    sprintf(body, "%s<p>%s: %s\r\n", body, longmsg, cause);
    sprintf(body, "%s<hr><em>The Proxy server</em>\r\n", body);

    /* Print the HTTP response */
    sprintf(buf, "HTTP/1.0 %s %s\r\n", errnum, shortmsg);
    rio_writen(fd, buf, strlen(buf));
    sprintf(buf, "Content-type: text/html\r\n");
    rio_writen(fd, buf, strlen(buf));
    sprintf(buf, "Content-length: %d\r\n\r\n", (int) strlen(body));
    rio_writen(fd, buf, strlen(buf));
    rio_writen(fd, body, strlen(body));
}

/* cache 함수들 */

// 캐시 초기화
void cache_init(Cache *cache, int capacity) {
    cache->entries = Malloc(sizeof(CacheEntry) * capacity);
    cache->count = 0;
    cache->capacity = capacity;
    cache->current_size = 0;
    pthread_mutex_init(&cache->lock, NULL);
}

// 캐시 전체 삭제
void cache_free(Cache *cache) {
    for (int i = 0; i < cache->count; i++) {
        Free(cache->entries[i].request);
        Free(cache->entries[i].response);
    }
    Free(cache->entries);
    pthread_mutex_destroy(&cache->lock);
    printf("cache freed");
}

int cache_find(Cache *cache, char *request) {
    for (int i = 0; i < cache->count; i++) {
        if (strcmp(cache->entries[i].request, request) == 0) {
            return i;
        }
    }
    return -1;
}

// 캐시에서 데이터 제거
void cache_evict(Cache *cache) {
    int oldest_index = 0;
    time_t oldest = cache->entries[0].timestamp;
    for (int i = 1; i < cache->count; i++) {
        if (cache->entries[i].timestamp < oldest) {
            oldest = cache->entries[i].timestamp;
            oldest_index = i;
        }
    }
    Free(cache->entries[oldest_index].request);
    Free(cache->entries[oldest_index].response);
    cache->current_size -= cache->entries[oldest_index].size;
    cache->entries[oldest_index] = cache->entries[cache->count - 1];  // 방금 free한 빈공간에 맨뒤 말록을 끼워넣기 위해.
    cache->count--;

    printf("cache deleted\n");
}

// 데이터를 캐시에 추가
void cache_add(Cache *cache, char *request, char *response, size_t size) {
    pthread_mutex_lock(&cache->lock);
    // 캐시 용량을 초과시 가장 오래된 항목 제거.
    if (cache->current_size + size > MAX_CACHE_SIZE || cache->count == cache->capacity) {
        cache_evict(cache);
    }
    cache->entries[cache->count].request = strdup(
            request);         // 요청 캐시 본체가 실질적으로 저장되는 함수. strdup 문자열 복사본을 동적으로 할당후 주소 반환.
    cache->entries[cache->count].response = Malloc(size);           // 응답을 위한 메모리 할당.
    memcpy(cache->entries[cache->count].response, response, size);  // 응답 복사

    cache->entries[cache->count].size = size;
    cache->entries[cache->count].timestamp = time(NULL);
    cache->current_size += size;
    cache->count++;

    pthread_mutex_unlock(&cache->lock);
    printf("cache added, request: %s, bytes: %d\n", request, size);
}

/* 클라이언트에게 캐시 반환하는 함수.
 * 쓰기 도중 읽기를 하면 문제가 생겨서 뮤텍스로 관리.
 */
void cache_retrieve(Cache *cache, int index, char *buf, size_t buf_size) {
    pthread_mutex_lock(&cache->lock);
    if (index >= 0 && index < cache->count) {
        memcpy(buf, cache->entries[index].response, buf_size);  // memcpy로 이진 데이터 안전 복사
    }
    pthread_mutex_unlock(&cache->lock);
}