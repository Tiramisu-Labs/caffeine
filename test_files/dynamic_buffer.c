#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

#include <stdio.h>
#include <string.h>
#include <stddef.h>
#include <time.h>

const char* handler(
    const char *request_data, 
    size_t data_len, 
    char *response_buffer, 
    size_t buffer_size,
    size_t *result_len
) {
    time_t timer;
    char time_buffer[26];
    struct tm* tm_info;
    
    time(&timer);
    tm_info = localtime(&timer);
    strftime(time_buffer, 26, "%Y-%m-%d %H:%M:%S", tm_info);

    int written = snprintf(
        response_buffer, 
        buffer_size, 
        "{\"status\": 200, \"body\": \"Time is: %s. Request Data Size: %zu\"}", 
        time_buffer, 
        data_len
    );

    // Ensure we didn't overflow the buffer
    if (written < 0 || (size_t)written >= buffer_size) {
        // Handle overflow error by writing a static error or crashing gracefully
        *result_len = 0;
        return NULL; // Or return a static error string
    }

    // 1. Set the length (CRUCIAL for this path)
    *result_len = written;

    // 2. Return the pointer to the start of the buffer
    return response_buffer;
}

#ifdef __cplusplus
}
#endif