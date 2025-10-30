#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
 
int main(int argc, char *argv[]) {     
    const char *response_body = "Caffeine handler executed successfully! Instance: integration_test";
     
    int content_length = strlen(response_body);
    
    printf("HTTP/1.1 200 OK\r\n");
    printf("Content-Type: text/plain\r\n");
    printf("Content-Length: %d\r\n", content_length);
    printf("Server: Caffeine-Test-Suite\r\n");
    printf("\r\n");
    
    printf("%s\n", response_body);
    fflush(stdout); 
    
    return 0;
}
 