#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

#include <stdio.h>
#include <string.h>
#include <stddef.h>

const char* handler(
    const char *request_data, 
    size_t data_len, 
    char *response_buffer, 
    size_t buffer_size,
    size_t *result_len
) {
    const char* message = "{\"status\": 201, \"body\": \"Created, but handler forgot length!\"}";
    
    size_t message_len = strlen(message);
    if (message_len < buffer_size) {
        strncpy(response_buffer, message, message_len);
        response_buffer[message_len] = '\0'; 
    } else {
        return NULL;
    }

    return response_buffer;
}

#ifdef __cplusplus
}
#endif
