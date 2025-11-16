#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

char* handler(void *req) {
    while (1) ;
    return "200 ok";
}

#ifdef __cplusplus
}
#endif
