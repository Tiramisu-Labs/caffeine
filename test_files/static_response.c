#include <string.h>
#include <stddef.h>

const char *handler(
    const char *request_data,
    char *response_buffer,
    size_t buffer_size,
    size_t *result_len
)
{
    (void)request_data;
    (void)response_buffer;
    (void)buffer_size;

    static const char *response =
        "{\"status\":200,\"body\":\"Hello from Static C!\\n\"}";

    *result_len = strlen(response);

    return response;
}