#include <stdio.h>
#include <unistd.h>

int main(int argc, char **argv)
{
    // printf("starting debug program\n");
    // char buffer[4096] = {0};
    // read(0, buffer, sizeof(buffer));
    // fprintf(stderr, "%s\n", buffer);
    // fprintf(stderr, "debug print test\n");
    printf("HTTP/1.1 200 OK\r\nContent-Length: 13\r\n\r\nHello, world!\n");
    fflush(stdout);
    return 0;
}