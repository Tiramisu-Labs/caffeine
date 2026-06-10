Private Mini-Orchestrator with Dynamic .so Modules
Architecture Overview

The master server manages a persistent pool of workers.

Each worker can dynamically load a .so module at runtime using dlopen() / LoadLibrary() and call standardized functions:

char* handler(char **args, SharedMemory *mem);


Workers execute handlers and communicate results back to the master.

This architecture avoids fork/exec per request → minimal overhead and maximum performance.

Supported Languages for .so Modules

C / C++ → full support, use extern "C" to export functions.

Rust → crate-type cdylib, export functions with #[no_mangle] pub extern "C" fn ....

Go → -buildmode=c-shared.

Fortran, D, Nim, Zig, Ada, Crystal, Swift → compile to native shared libraries; export functions compatible with C ABI.

VM/JIT Languages → require wrappers to produce native .so compatible with C (e.g., Cython for Python).

Worker and Handler Monitoring

Timeouts:

Each handler invocation should have a configurable timeout.

Workers exceeding the timeout can be reset by the master.

Buffer Limit:

Pass a pre-allocated memory buffer to the handler for controlled memory usage:

typedef struct {
    char *buffer;
    size_t size;
} SharedMemory;


Worker vs Master Responsibilities:

Workers manage execution time and monitoring locally.

The master handles worker creation, restart, and tracking only.

Resource Control

Memory / CPU:

Optional limits via ulimit or lightweight cgroups.

Preallocated buffers restrict heap usage indirectly.

Memory Leaks:

Cannot enforce stack-only usage in C/C++ at runtime, but you can:

Provide only SharedMemory buffers to handlers.

Monitor memory growth per worker and reset if exceeding thresholds.

Use static analysis or runtime tools like Valgrind, AddressSanitizer during development.

Safety and Stability

Server runs in a private, trusted environment, so main concerns are:

Crash isolation → worker crash does not affect others.

Timeout enforcement → prevents infinite loops.

Memory limits → prevents resource saturation.

Using dynamic .so modules implies a misbehaving module can crash a worker, but the worker pool with automatic restart mitigates the impact.

Operational Recommendations

Define a clear interface for all .so handlers.

Each worker loads a single module at a time to reduce interference.

Support hot-reload via dlclose() + dlopen() on updated modules.

Combine timeouts + memory limits to achieve near-sandboxing without container overhead.

Example Worker API (C)
typedef struct {
    char *buffer;
    size_t size;
} SharedMemory;

typedef char* (*HandlerFunc)(char **args, SharedMemory *mem);

int worker_load_module(const char *path);
char* worker_invoke_handler(char **args, SharedMemory *mem, unsigned int timeout_ms);
void worker_unload_module();


worker_load_module() → loads the .so.

worker_invoke_handler() → runs handler with timeout and buffer.

worker_unload_module() → unloads .so safely.

Summary

Performance: No fork/exec per request.

Flexibility: Direct data passing via buffers.

Safety: Worker pool, timeouts, and memory limits prevent server-wide crashes.

Extensible: Works with multiple compiled languages producing .so modules.

This architecture is designed for private, resource-constrained environments, providing a lightweight alternative to Kubernetes without requiring containers.