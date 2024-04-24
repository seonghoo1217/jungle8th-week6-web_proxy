#include "../csapp.h"

int main(void) {
    char *buf, *p, *method;
    char arg1[MAXLINE], arg2[MAXLINE], content[MAXLINE];
    int nl = 0, n2 = 0;
    char *is_head_request = getenv("IS_HEAD_REQUEST");

    if ((buf = getenv("QUERY_STRING")) != NULL) {
        p = strchr(buf, '&');
        *p = '\0';
        strcpy(arg1, buf + 3);
        strcpy(arg2, p + 1 + 3);
        n1 = atoi(arg1);
        n2 = atoi(arg2);
    }

    sprintf(content, "QUERY_STRING=%s", buf);
    sprintf(content, "Welcome to add.com: ");
    sprintf(content, "%sTHE Internet addition portal.\r\n<p>", content);
    sprintf(content, "%sThe answer is: %d + %d = %d\r\n<p>", content, n1, n2, n1 + n2);
    sprintf(content, "%sThanks for visiting!\r\n", content);

    printf("Connection: close\r\n");
    printf("content-length: %d\r\n", (int) strlen(content));
    printf("content-type: text/html\r\n\r\n");

    if (is_head_request == NULL || strcmp(is_head_request, "1") != 0) {
        printf("%s", content);
    }
    fflush(stdout);


    exit(0);
}