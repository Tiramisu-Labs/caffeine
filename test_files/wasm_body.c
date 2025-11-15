#include <stdint.h>

#define MEMORY_SIZE 65536
static uint8_t memory[MEMORY_SIZE];

// Simple bump allocator
static uint32_t heap_offset = 1024;

uint32_t alloc(uint32_t size) {
    if (heap_offset + size > MEMORY_SIZE) return 0;
    uint32_t ptr = heap_offset;
    heap_offset += size;
    return ptr;
}

void dealloc(uint32_t ptr, uint32_t size) {
    (void)ptr; (void)size; // no-op
}

// Minimal memcpy implementation
static void my_memcpy(uint8_t* dst, const uint8_t* src, uint32_t len) {
    for (uint32_t i = 0; i < len; i++) {
        dst[i] = src[i];
    }
}

// handle_request(ptr, len) -> i64 (ptr<<32 | len)
uint64_t handle_request(uint32_t ptr, uint32_t len) {
    (void)ptr; (void)len;  // ignore input for simplicity

    const uint8_t response[] = "{\"status\":200,\"message\":\"Hello WASM auanafrena\"}";
    uint32_t res_len = sizeof(response) - 1;

    uint32_t res_ptr = alloc(res_len);
    if (!res_ptr) return 0;

    my_memcpy(&memory[res_ptr], response, res_len);

    return ((uint64_t)res_ptr << 32) | res_len;
}

// Export memory for host
uint8_t* memory_export() {
    return memory;
}
