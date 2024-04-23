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
    char *key; // socketId와 path를 조합한 문자열
    char *value; // 캐시된 내용
    struct CacheEntry *next; // 충돌 발생 시 다음 엔트리를 가리킴
} CacheEntry;

CacheEntry *cacheTable[HASH_TABLE_SIZE];

// 간단한 해시 함수
unsigned int hash(char *key) {
    unsigned long int value = 0;
    while (*key != '\0') {
        value = value * 37 + *key++;
    }
    return value % HASH_TABLE_SIZE;
}

// 캐시에 데이터 저장
void putCache(char *key, char *value) {
    unsigned int index = hash(key);
    CacheEntry *newEntry = malloc(sizeof(CacheEntry));
    newEntry->key = strdup(key);
    newEntry->value = strdup(value);
    newEntry->next = cacheTable[index];
    cacheTable[index] = newEntry;

    unsigned int newEntrySize = strlen(newEntry->value);
    // 새로운 항목을 추가하기 전에 MAX_CACHE_SIZE를 초과하는지 확인
    if (currentCacheSize + newEntrySize > MAX_CACHE_SIZE) {
        // 여기서는 단순한 예시를 위해 구체적인 항목 제거 로직을 구현하지 않음
        printf("캐시 크기 초과: 캐시에서 항목을 제거해야 합니다.\n");
        // 캐시에서 항목을 제거하는 로직 구현 필요
    } else {
        currentCacheSize += newEntrySize; // 캐시 크기 갱신
    }
}

// 캐시에서 데이터 가져오기
char *getCache(char *key) {
    unsigned int index = hash(key);
    CacheEntry *entry = cacheTable[index];
    while (entry != NULL) {
        if (strcmp(entry->key, key) == 0) {
            return entry->value;
        }
        entry = entry->next;
    }
    return NULL; // 키가 존재하지 않음
}

// 캐시 키 생성
char *createCacheKey(int socketId, char *path) {
    // socketId를 문자열로 변환하기 위해 충분한 크기의 배열을 선언합니다.
    char strSocketId[20]; // int 최대 크기를 고려한 충분한 크기
    sprintf(strSocketId, "%d", socketId); // int를 문자열로 변환

    //key에 메모리 할당
    char *key = (char *) malloc(strlen(strSocketId) + strlen(path) + 1);
    if (!key) {
        return NULL; // 메모리 할당 실패 시 NULL 반환
    }

    // key를 초기화하고, socketId 문자열과 path를 합칩니다.
    strcpy(key, strSocketId); // 첫 번째 문자열을 key에 복사
    strcat(key, path); // 두 번째 문자열을 key에 이어 붙임

    return key;
}


int main(int argc, char **argv) {
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
        Pthread_create(&tid, NULL, thread, connfd);
    }
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
    char request_buf[MAXLINE], response_buf[MAX_OBJECT_SIZE];
    char method[MAXLINE], uri[MAXLINE], path[MAXLINE];
    char hostname[MAXLINE], port[MAXLINE];
    rio_t request_rio, response_rio;

    /*캐싱영역 ==============================*/
    int socketId = clientfd;


    /*캐싱영역 ==============================*/

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

    printf("hostname: %s", hostname);
    printf("port: %s", port);

//    printf("uri: %s\n", uri); // 디버깅용 URI 출력

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
