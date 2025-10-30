#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/**
 * @brief Sends a complete HTTP response to stdout.
 */
void send_response(const char *body, const char *status_code, const char *status_text) {
    int content_length = strlen(body);
    
    // Print the HTTP status line and required headers
    printf("HTTP/1.1 %s %s\r\n", status_code, status_text);
    printf("Content-Type: text/plain\r\n");
    printf("Content-Length: %d\r\n", content_length);
    printf("Server: Caffeine-Test-Suite\r\n");
    printf("\r\n"); // End of headers
    
    // Print the body content
    printf("%s", body);
    
    // Flush output to ensure the parent process (Caffeine) reads everything
    fflush(stdout); 
}

int main(int argc, char *argv[]) {
    // Get the request method from environment variables
    const char *method = getenv("REQUEST_METHOD");
    
    if (method && strcmp(method, "POST") == 0) {
        // --- Handle POST Request ---
        const char *content_length_str = getenv("CONTENT_LENGTH");
        int content_length = 0;
        
        if (content_length_str) {
            content_length = atoi(content_length_str);
        }

        if (content_length > 0) {
            // Dynamically allocate buffer for the request body
            char *body_buffer = (char *)malloc(content_length + 1);
            if (!body_buffer) {
                send_response("Internal Server Error: Memory allocation failed.", "500", "Internal Server Error");
                return 1;
            }
            
            // Read the body from standard input (stdin)
            int bytes_read = fread(body_buffer, 1, content_length, stdin);
            body_buffer[bytes_read] = '\0'; // Null-terminate
            
            // Construct the response body: Echo the received body
            size_t response_len = 100 + bytes_read;
            char *response_body = (char *)malloc(response_len);
            snprintf(response_body, response_len, 
                     "POST request processed. Received body (%d bytes): %s", 
                     bytes_read, body_buffer);
            
            send_response(response_body, "200", "OK");
            
            free(body_buffer);
            free(response_body);
        } else {
            send_response("POST request received, but no body found.", "200", "OK");
        }
        
    } else {
        // --- Handle GET Request (Default) ---
        const char *response_body = 
            "Caffeine handler executed successfully! Instance: integration_test (Method: GET)";
        send_response(response_body, "200", "OK");
    }
    
    return 0;
}
