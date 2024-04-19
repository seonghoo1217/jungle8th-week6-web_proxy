/*
 * adder.c - a minimal CGI program that adds two numbers together
 */
/* $begin adder */
#include "csapp.h"

int main(void) {
    char *buf, *p;
    char arg1[MAXLINE], arg2[MAXLINE], content[MAXLINE]; //8192
    int n1 = 0, n2 = 0;

    if ((buf = getenv("QUERY_STRING")) != NULL) {
        p = strchr(buf, '&'); //strchr : 위치 찾기 queryString의 다음 key - value 값을 찾는 용도 ex) name=john&age=23
        *p = '\0'; //실제 첫 파라메터 뒤의&를 NULL로 만듬
        strcpy(arg1, buf); // 첫 파라메터
        strcpy(arg2, p + 1); // 두번째 인자값
        n1 = atoi(arg1); // ASCII to integer : atoi
        n2 = atoi(arg2);
    }

    // MAKE RESPONSE BODY
    sprintf(content, "QUERY_STRING=%s", buf);
    sprintf(content, "Welcome to add. com:");
    sprintf(content, "%sTHE Internet addition portal. \r\n<p>", content);
    sprintf(content, "%sThe answer is: %d + %d = %d\r\n<p>", content, n1, n2, n1 + n2);
    sprintf(content, "%sThanks for visiting\r\n", content);

    // HTTP RESPONSE
    printf("Connection : close\r\n");
    printf("Content-Length: %d\r\n", (int) strlen(content));
    printf("Content-type:text/html\r\n\r\n");
    printf("%s", content);
    fflush(stdout);

    exit(0);
}
/* $end adder */
