#include <stdio.h>

int main()
{
    fprintf(stderr, "debug print test\n");
    printf("HTTP/1.1 200 OK\r\nContent-Length: 13\r\n\r\nHello, world!\n");
    return 0;
}