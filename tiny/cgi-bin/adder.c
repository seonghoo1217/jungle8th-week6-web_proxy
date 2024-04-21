#include "../csapp.h"

int main(void) {
    char *buf, *p, *method;
    char arg1[MAXLINE], arg2[MAXLINE], content[MAXLINE];
    int nl = 0, n2 = 0;

    if ((buf = getenv("QUERY_STRING")) != NULL) {
        p = strchr(buf, '&');
        *p = '\0';
        strcpy(arg1, buf);
        strcpy(arg2, p + 1);
        nl = atoi(arg1);
        n2 = atoi(arg2);
    }
    method = getenv("REQUEST_METHOD");

/* Make the response body */
    sprintf(content, "%sWelcome to add.com: ", content);
    sprintf(content, "%sTHE Internet addition portal.\r\n<p>", content);
    sprintf(content, "%sThe answer is: %d + %d = %d\r\n<p>",
            content, nl, n2, nl + n2);
    sprintf(content, "%sThanks for visiting!\r\n", content);
/* Generate the HTTP response */
    printf("Connection: close\r\n");
    printf("Content-length: %d\r\n", (int) strlen(content));
    printf("Content-type: text/html\r\n\r\n");
    if (strcasecmp(method, "GET") == 0) {
        printf("%s", content);
    }
    fflush(stdout);
    exit(0);
}