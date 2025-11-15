#include <stdint.h>

// Optional: alloc/dealloc exports if you want them, but not used here
int32_t alloc(int32_t size) { return 0; }
void dealloc(int32_t ptr, int32_t size) {}

// Exported function called by your worker
// Returns i64: high 32 bits = pointer, low 32 bits = length
__attribute__((export_name("handle_request")))
int64_t handle_request(int32_t ptr, int32_t len) {
    // No response body
    return ((int64_t)0 << 32) | 0;
}