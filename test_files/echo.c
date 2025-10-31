#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

void send_response(const char *body, const char *status_code, const char *status_text) {
    int content_length = strlen(body);

    printf("HTTP/1.1 %s %s\r\n", status_code, status_text);
    printf("Content-Type: text/plain\r\n");
    printf("Content-Length: %d\r\n", content_length);
    printf("Server: Caffeine-Test-Suite\r\n");
    printf("Connection: close\r\n");
    printf("\r\n");
    printf("%s", body);

    fflush(stdout); 
}

int main(int argc, char *argv[]) {
    const char *method = getenv("REQUEST_METHOD");
    
    if (method && strcmp(method, "POST") == 0) {
        const char *content_length_str = getenv("CONTENT_LENGTH");
        int content_length = 0;
        
        if (content_length_str) {
            content_length = atoi(content_length_str);
        }

        if (content_length > 0) {
            char *body_buffer = (char *)malloc(content_length + 1);
            if (!body_buffer) {
                send_response("Internal Server Error: Memory allocation failed.", "500", "Internal Server Error");
                return 1;
            }

            int bytes_read = fread(body_buffer, 1, content_length, stdin);
            body_buffer[bytes_read] = '\0';

            size_t response_len = 100 + bytes_read;
            char *response_body = (char *)malloc(response_len);
            snprintf(response_body, response_len, 
                    "echo: %s", body_buffer);
            
            send_response(response_body, "200", "OK");
            
            free(body_buffer);
            free(response_body);
        } else {
            send_response("POST request received, but no body found.", "200", "OK");
        }
        
    } else {
        const char *response_body = 
            "Caffeine handler executed successfully! Instance: integration_test (Method: GET)";
        send_response(response_body, "200", "OK");
    }
    
    return 0;
}
