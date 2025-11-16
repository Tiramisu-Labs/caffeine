#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

char* handler(void *req) {
    return "200 ok";
}

#ifdef __cplusplus
}
#endif
