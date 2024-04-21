/* $begin tinymain */
/*
 * tiny.c - A simple, iterative HTTP/1.0 Web server that uses the
 *     GET method to serve static and dynamic content.
 *
 * Updated 11/2019 droh
 *   - Fixed sprintf() aliasing issue in serve_static(), and clienterror().
 */
#include "csapp.h"

void doit(int fd);

void read_requesthdrs(rio_t *rp);

int parse_uri(char *uri, char *filename, char *cgiargs);

void serve_static(int fd, char *filename, int filesize, char *method);

void get_filetype(char *filename, char *filetype);

void serve_dynamic(int fd, char *filename, char *cgiargs, char *method);

void clienterror(int fd, char *cause, char *errnum, char *shortmsg,
                 char *longmsg);

// 포트번호 인자로 받기
int main(int argc, char **argv) {
    int listenfd, connfd;
    char hostname[MAXLINE], port[MAXLINE];
    socklen_t clientlen;
    struct sockaddr_storage clientaddr; // 클라이언트에서 연결 요청을 보내면 알 수 있는 클라이언트 연결 소켓 주소

    /* Check command line args */
    if (argc != 2) {
        fprintf(stderr, "usage: %s <port>\n", argv[0]);
        exit(1);
    }

    // 해당 포트 번호에 해당하는 listen 소켓 식별자 열어주기
    listenfd = Open_listenfd(argv[1]);
    // 요청이 들어 올 때 마다 새로운 연결 소켓을 만들어 doit() 호출
    while (1) {
        clientlen = sizeof(clientaddr);
        // 서버 연결 식별자 -> connfd
        connfd = Accept(listenfd, (SA *) &clientaddr,
                        &clientlen);  // line:netp:tiny:accept
        // 연결 성공 메세지를 위해서 Getnameinfo를 호출하면서 hostname, portrk 채워짐
        Getnameinfo((SA *) &clientaddr, clientlen, hostname, MAXLINE, port, MAXLINE,
                    0);
        printf("Accepted connection from (%s, %s)\n", hostname, port);
        doit(connfd);   // line:netp:tiny:doit
        // 서버 연결 식별자 닫아주면 하나의 트랜잭션 끝.
        Close(connfd);  // line:netp:tiny:close
    }
}

// 클라이언트의 요청 라인을 확인해 정적, 동적컨텐츠인지 구분하고 각각의 서버에 보냄
void doit(int fd) // -> connfd가 인자로 들어옴
{
    int is_static;
    struct stat sbuf;
    char buf[MAXLINE], method[MAXLINE], uri[MAXLINE], version[MAXLINE];  // 클라이언트에게서 받은 요청(rio)으로 채워진다.
    char filename[MAXLINE], cgiargs[MAXLINE];  // parse_uri를 통해 채워짐.
    rio_t rio;

    /* Read request line and headers */
    /* 클라이언트가 rio로 보낸 request 라인과 헤더를 읽고 분석한다. */
    Rio_readinitb(&rio, fd); // rio 버퍼와 fd, 여기서는 서버의 connfd를 연결시켜준다.
    Rio_readlineb(&rio, buf, MAXLINE); // 그리고 rio(==connfd)에 있는 string 한 줄(응답 라인)을 모두 buf로 옮긴다.
    printf("Request headers:\n");
    printf("%s", buf);  // 요청 라인 buf = "GET /godzilla.gif HTTP/1.1\0"을 표준 출력만 해줌.
    sscanf(buf, "%s %s %s", method, uri, version); // buf에서 문자열 3개를 읽어와 method, uri, version이라는 문자열에 저장.

    // 11.11 문제
    // 요청 method가 GET과 HEAD가 아니면 종료. main으로 가서 연결 닫고 다음 요청 기다림.
    if (!(strcasecmp(method, "GET") == 0 || strcasecmp(method, "HEAD") == 0)) {  // method 스트링이 GET이 아니면 0이 아닌 값이 나옴
        clienterror(fd, method, "501", "Not implemented", "Tiny does not implement this method");
        return;
    }

    // 요청 라인을 뺀 나머지 요청 헤더들을 무시한다.
    read_requesthdrs(&rio);

    /* Parse URI from GET request */
    /* parse_uri : 클라이언트 요청 라인에서 받아온 uri를 이용해 정적/동적 컨텐츠를 구분한다. */
    is_static = parse_uri(uri, filename, cgiargs); // 정적이면 1 동적이면 0

    /* stat(file, *buffer) : file의 상태를 buffer에 넘긴다. */
    // 여기서 filename은 parse_uri로 만들어준 filename
    if (stat(filename, &sbuf) < 0) {  // 못 넘기면 fail. 파일이 없다. 404.
        clienterror(fd, filename, "404", "Not found", "Tiny couldn't find this file");
        return;
    }

    /* 컨텐츠의 유형(정적, 동적)을 파악한 후 각각의 서버에 보낸다. */
    if (is_static) { /* Serve static content */
        // !(일반 파일이다) or !(읽기 권한이 있다)
        if (!(S_ISREG(sbuf.st_mode)) || !(S_IRUSR & sbuf.st_mode)) {
            clienterror(fd, filename, "403", "Forbidden", "Tiny couldn't read the file");
            return;
        }
        // 정적 컨텐츠면 사이즈를 같이 서버에 보낸다. -> Response header에 Content-length 위해
        serve_static(fd, filename, sbuf.st_size, method);
    } else { /* Serve dynamic content */
        // !(일반 파일이다) or !(실행 권한이 있다)
        if (!(S_ISREG(sbuf.st_mode)) || !(S_IXUSR & sbuf.st_mode)) {
            clienterror(fd, filename, "403", "Forbidden", "Tiny couldn't run the CGI program");
            return;
        }
        // 동적 컨텐츠면 인자를 같이 서버에 보낸다.
        serve_dynamic(fd, filename, cgiargs, method);
    }
}

// 클라이언트 오류 보고
void clienterror(int fd, char *cause, char *errnum, char *shortmsg, char *longmsg) {
    char buf[MAXLINE], body[MAXBUF]; // 에러메세지, 응답 본체

    // build HTTP response
    sprintf(body, "<html><title>Tiny Error</title>");
    sprintf(body, "%s<body bgcolor=""ffffff"">\r\n", body);
    sprintf(body, "%s%s: %s\r\n", body, errnum, shortmsg);
    sprintf(body, "%s<p>%s: %s\r\n", body, longmsg, cause);
    sprintf(body, "%s<hr><em>The Tiny Web server></em>\r\n", body);

    // print HTTP response
    sprintf(buf, "HTTP/1.0 %s %s\r\n", errnum, shortmsg);
    Rio_writen(fd, buf, strlen(buf));
    sprintf(buf, "Content-type: text/html\r\n");
    Rio_writen(fd, buf, strlen(buf));
    sprintf(buf, "Content-length: %d\r\n\r\n", (int) strlen(body));

    // Rio_writen으로 buf와 body를 서버 소켓을 통해 클라이언트에게 보냄
    Rio_writen(fd, buf, strlen(buf));
    Rio_writen(fd, body, strlen(body));
}

// tiny는 요청 헤더 내의 어떤 정보도 사용하지 않고 이들을 읽고 무시
void read_requesthdrs(rio_t *rp) {
    char buf[MAXLINE];

    Rio_readlineb(rp, buf, MAXLINE);
    while (strcmp(buf, "\r\n")) { // EOF(한 줄 전체가 개행문자인 곳) 만날 때 까지 계속 읽기
        Rio_readlineb(rp, buf, MAXLINE);
        printf("%s", buf);
    }
}

// uri를 받아 요청받은 filename(파일이름), cgiarg(인자)를 채워줌.
int parse_uri(char *uri, char *filename, char *cgiargs) {
    char *ptr;

    if (!strstr(uri, "cgi-bin")) { // 정적 컨텐츠 요청(uri에 "cgi-bin"이 없으면)
        strcpy(cgiargs, "");
        strcpy(filename, ".");
        strcat(filename, uri); // url '/'로 시작해서 들어옴
        if (uri[strlen(uri) - 1] == '/') {
            /*
              uri : /home.html
              cgiargs :
              filename : ./home.html
            */
            strcat(filename, "home.html");
        }

        return 1;
    } else { // 동적 컨텐츠 요청
        /*
          uri : /cgi-bin/adder?1234&1234
          cgiargs : 1234&1234
          filename : ./cgi-bin/adder
        */
        ptr = index(uri, '?');
        // '?'가 있으면 cgiargs를 '?' 뒤 인자들과 값으로 채워주고 ?를 NULL로
        if (ptr) {
            strcpy(cgiargs, ptr + 1);
            *ptr = '\0';
        } else { // '?' 없으면 cgiargs에 아무것도 안 넣어줌
            strcpy(cgiargs, "");
        }
        strcpy(filename, ".");
        strcat(filename, uri);

        return 0;
    }
}

// 클라이언트가 원하는 정적 컨텐츠를 받아와서 응답 라인과 헤더를 작성하고 서버에게 보냄, 그 후 정적 컨텐츠 파일을 읽어 그 응답 바디를 클라이언트에게 보냄
void serve_static(int fd, char *filename, int filesize, char *method) {
    int srcfd;
    char *srcp, filetype[MAXLINE], buf[MAXBUF];

    // Send response headers to client 클라이언트에게 응답 헤더 보내기
    // 응답 라인과 헤더 작성
    get_filetype(filename, filetype);  // 파일 타입 찾아오기
    sprintf(buf, "HTTP/1.0 200 OK\r\n");  // 응답 라인 작성
    sprintf(buf, "%sServer: Tiny Web Server\r\n", buf);  // 응답 헤더 작성
    sprintf(buf, "%sConnection: close\r\n", buf);
    sprintf(buf, "%sContent-length: %d\r\n", buf, filesize);
    sprintf(buf, "%sContent-type: %s\r\n\r\n", buf, filetype);

    /* 응답 라인과 헤더를 클라이언트에게 보냄 */
    Rio_writen(fd, buf, strlen(buf));  // connfd를 통해 clientfd에게 보냄
    printf("Response headers:\n");
    printf("%s", buf);  // 서버 측에서도 출력한다.

    if (strcasecmp(method, "GET") == 0) { // 11.11 숙제
        /* Send response body to client */
        srcfd = Open(filename, O_RDONLY, 0); // filename의 이름을 갖는 파일을 읽기 권한으로 불러온다.
        // srcp = Mmap(0, filesize, PROT_READ, MAP_PRIVATE, srcfd, 0); -> Mmap방법 : 파일의 메모리를 그대로 가상 메모리에 매핑함.
        srcp = (char *) Malloc(filesize); // 11.9 문제 : mmap()과 달리, 먼저 파일의 크기만큼 메모리를 동적할당 해줌.
        Rio_readn(srcfd, srcp, filesize); // rio_readn을 사용하여 파일의 데이터를 메모리로 읽어옴. ->  srcp에 srcfd의 내용을 매핑해줌
        Close(srcfd); // 파일을 닫는다.
        Rio_writen(fd, srcp, filesize);  // 해당 메모리에 있는 파일 내용들을 fd에 보낸다.
        // Munmap(srcp, filesize); -> Mmap() 방법 : free해주는 느낌
        free(srcp); // malloc 썼으니까 free
    }
}

// filename을 조사해서 filetype을 입력해줌.
void get_filetype(char *filename, char *filetype) {
    if (strstr(filename, ".html"))  // filename 스트링에 ".html"
        strcpy(filetype, "text/html");
    else if (strstr(filename, ".gif"))
        strcpy(filetype, "image/gif");
    else if (strstr(filename, ".png"))
        strcpy(filetype, "image/png");
    else if (strstr(filename, ".jpg"))
        strcpy(filetype, "image/jpeg");
    else if (strstr(filename, ".mp4"))
        strcpy(filetype, "video/mp4"); // 11.7 문제
    else
        strcpy(filetype, "text/plain");
}

// 동적 컨텐츠
void serve_dynamic(int fd, char *filename, char *cgiargs, char *method) {
    char buf[MAXLINE], *emptylist[] = {NULL};
    /* Return first part of HTTP response */
    sprintf(buf, "HTTP/1.0 200 OK\r\n");
    Rio_writen(fd, buf, strlen(buf));
    sprintf(buf, "Server: Tiny Web Server\r\n");
    Rio_writen(fd, buf, strlen(buf));

    if (Fork() == 0) { /* Child */
        /* Real server would set all CGI vars here */
        setenv("QUERY_STRING", cgiargs, 1);  //
        // method를 cgi-bin/adder.c에 넘겨 주기 위해 환경변수 set -> 11.11 문제
        setenv("REQUEST_METHOD", method, 1);
        // 클라이언트의 표준 출력을 CGI 프로그램의 표준 출력과 연결한다.
        // 이제 CGI 프로그램에서 printf하면 클라이언트에서 출력됨
        Dup2(fd, STDOUT_FILENO); /* Redirect stdout to client */
        Execve(filename, emptylist, environ); /* Run CGI program */
    }
    Wait(NULL); /* Parent waits for and reaps child */
}