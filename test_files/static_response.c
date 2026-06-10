#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

#include <string.h>

const char* handler(
    const char *request_data, 
    char *response_buffer, 
    size_t buffer_size,
    size_t *result_len
) {
    const char* STATIC_JSON = "{\"status\": 200, \"body\": \"Hello from Static C!\n\"}";

    // 1. Set the length (Optional, but highly recommended for clarity)
    *result_len = strlen(STATIC_JSON);
    
    // 2. Return the pointer to the static memory address
    return STATIC_JSON;
}

#ifdef __cplusplus
}
#endif
