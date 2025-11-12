#define _GNU_SOURCE
#define _FILE_OFFSET_BITS 64
#include <caffeine.h>
#include <caffeine_utils.h>
#include <caffeine_cfg.h>
#include <log.h>
#include <response.h>
#include <headers.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <ctype.h>
#include <errno.h>
#include <signal.h>
#include <limits.h>
#include <assert.h>
#include <wasmtime.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <arpa/inet.h>
#include <poll.h>

#define MODULE_CACHE_SIZE 128

typedef struct {
    char path[2048];
    time_t mtime;
    wasmtime_module_t *module;
} WasmModuleCache;

static WasmModuleCache g_module_cache[MODULE_CACHE_SIZE] = {0};

void worker_redirect_logs() {
    char *log_path = get_log_path();
    int log_fd;
    
    log_fd = open(log_path, O_RDWR | O_CREAT | O_APPEND, 0644); 
    
    if (log_fd < 0) {
        LOG_ERROR("FATAL: Worker failed to open log file %s: %s", log_path, strerror(errno));
        return;
    }
    
    if (dup2(log_fd, STDOUT_FILENO) < 0 || dup2(log_fd, STDERR_FILENO) < 0) {
        close(log_fd);
        return;
    }

    if (log_fd > 2) close(log_fd);
    
    if (isatty(STDIN_FILENO)) freopen("/dev/null", "r", stdin); 
    if (isatty(STDOUT_FILENO)) freopen(log_path, "a", stdout);
    if (isatty(STDERR_FILENO)) freopen(log_path, "a", stderr); 
    
    LOG_INFO("Worker successfully forced redirection of STDOUT/STDERR to log file.");
}

static bool load_wasm_file_binary(const char* path, wasm_byte_vec_t* out) {
    FILE *f = fopen(path, "rb");
    if (!f) return false;

    struct stat st;
    if (stat(path, &st) != 0) { fclose(f); return false; }

    uint8_t *data = malloc(st.st_size);
    if (!data) { fclose(f); return false; }

    if (fread(data, 1, st.st_size, f) != st.st_size) { free(data); fclose(f); return false; }
    fclose(f);

    out->data = data;
    out->size = st.st_size;
    return true;
}

/* module cache lookup and update */
static wasmtime_module_t* get_cached_module(wasm_engine_t *engine, const char *path) {
    struct stat st;
    if (stat(path, &st) != 0) return NULL;

    /* search cache */
    for (int i = 0; i < MODULE_CACHE_SIZE; i++) {
        if (g_module_cache[i].module && strcmp(g_module_cache[i].path, path) == 0) {
            if (g_module_cache[i].mtime == st.st_mtime) return g_module_cache[i].module;
            /* file changed: delete old module */
            wasmtime_module_delete(g_module_cache[i].module);
            g_module_cache[i].module = NULL;
        }
    }

    /* compile module */
    wasm_byte_vec_t binary;
    if (!load_wasm_file_binary(path, &binary)) return NULL;

    wasmtime_module_t *module = NULL;
    wasmtime_error_t *err = wasmtime_module_new(engine, (uint8_t*)binary.data, binary.size, &module);
    free(binary.data);
    if (err) { wasmtime_error_delete(err); return NULL; }
    if (!module) return NULL;

    /* store in first empty slot */
    for (int i = 0; i < MODULE_CACHE_SIZE; i++) {
        if (!g_module_cache[i].module) {
            strncpy(g_module_cache[i].path, path, sizeof(g_module_cache[i].path)-1);
            g_module_cache[i].mtime = st.st_mtime;
            g_module_cache[i].module = module;
            break;
        }
    }
    return module;
}

/* helper: read exactly N bytes */
static ssize_t read_exact(int fd, char *buf, size_t n) {
    size_t total = 0;
    while (total < n) {
        ssize_t r = read(fd, buf + total, n - total);
        if (r < 0) {
            if (errno == EINTR) continue;
            return -1;
        } else if (r == 0) break;
        total += r;
    }
    return total;
}

int wasm_execute(wasm_engine_t *engine, const char *wasm_path,
                 const char *req_data, size_t req_len,
                 char **res_data, size_t *res_len)
{
    *res_data = NULL; *res_len = 0;

    wasmtime_module_t *module = get_cached_module(engine, wasm_path);
    if (!module) { fprintf(stderr,"Failed to load wasm module %s\n", wasm_path); return -1; }

    wasmtime_store_t *store = wasmtime_store_new(engine, NULL, NULL);
    if (!store) return -1;
    wasmtime_context_t *ctx = wasmtime_store_context(store);
    wasmtime_instance_t instance;
    wasm_trap_t *trap = NULL;
    wasmtime_error_t *err = NULL;
    bool ok;
    wasmtime_extern_t item;

    /* instantiate */
    err = wasmtime_instance_new(ctx, module, NULL, 0, &instance, &trap);
    if (err || trap) { if(err) wasmtime_error_delete(err); if(trap) wasm_trap_delete(trap); wasmtime_store_delete(store); return -1; }

    /* exports */
    ok = wasmtime_instance_export_get(ctx, &instance, "memory", strlen("memory"), &item);
    if(!ok || item.kind != WASMTIME_EXTERN_MEMORY) { wasmtime_store_delete(store); return -1; }
    wasmtime_memory_t memory = item.of.memory;

    ok = wasmtime_instance_export_get(ctx, &instance, "alloc", strlen("alloc"), &item);
    if(!ok || item.kind != WASMTIME_EXTERN_FUNC) { wasmtime_store_delete(store); return -1; }
    wasmtime_func_t alloc_fn = item.of.func;

    wasmtime_func_t dealloc_fn; bool have_dealloc = false;
    ok = wasmtime_instance_export_get(ctx, &instance, "dealloc", strlen("dealloc"), &item);
    if(ok && item.kind == WASMTIME_EXTERN_FUNC) { dealloc_fn = item.of.func; have_dealloc = true; }

    ok = wasmtime_instance_export_get(ctx, &instance, "handle_request", strlen("handle_request"), &item);
    if(!ok || item.kind != WASMTIME_EXTERN_FUNC) { wasmtime_store_delete(store); return -1; }
    wasmtime_func_t handle_fn = item.of.func;

    /* alloc guest memory */
    wasmtime_val_t alloc_args[1] = { {.kind=WASMTIME_I32, .of.i32=(int32_t)req_len} };
    wasmtime_val_t alloc_res[1];
    err = wasmtime_func_call(ctx, &alloc_fn, alloc_args, 1, alloc_res, 1, &trap);
    if(err || trap) { if(err) wasmtime_error_delete(err); if(trap) wasm_trap_delete(trap); wasmtime_store_delete(store); return -1; }
    int32_t guest_ptr = alloc_res[0].of.i32;

    /* copy request into guest memory */
    uint8_t *mem_data = wasmtime_memory_data(ctx, &memory);
    size_t mem_size = wasmtime_memory_data_size(ctx, &memory);
    if ((uint64_t)guest_ptr + req_len > mem_size) { wasmtime_store_delete(store); return -1; }
    memcpy(mem_data + guest_ptr, req_data, req_len);

    /* call handle_request */
    wasmtime_val_t handle_args[2] = { {.kind=WASMTIME_I32,.of.i32=guest_ptr}, {.kind=WASMTIME_I32,.of.i32=(int32_t)req_len} };
    wasmtime_val_t handle_res[1];
    err = wasmtime_func_call(ctx, &handle_fn, handle_args, 2, handle_res, 1, &trap);
    if(err || trap) { if(err) wasmtime_error_delete(err); if(trap) wasm_trap_delete(trap); wasmtime_store_delete(store); return -1; }

    int64_t packed = handle_res[0].of.i64;
    int32_t res_ptr = (int32_t)(packed >> 32);
    int32_t res_len_local = (int32_t)(packed & 0xFFFFFFFF);
    if(res_ptr < 0 || res_len_local < 0 || (uint64_t)res_ptr + (uint64_t)res_len_local > mem_size) { wasmtime_store_delete(store); return -1; }

    *res_data = malloc(res_len_local);
    if(!*res_data) { wasmtime_store_delete(store); return -1; }
    memcpy(*res_data, mem_data + res_ptr, res_len_local);
    *res_len = res_len_local;

    if(have_dealloc) {
        wasmtime_val_t dealloc_args[2] = { {.kind=WASMTIME_I32,.of.i32=res_ptr}, {.kind=WASMTIME_I32,.of.i32=res_len_local} };
        wasmtime_func_call(ctx, &dealloc_fn, dealloc_args, 2, NULL, 0, &trap);
    }

    wasmtime_store_delete(store);
    return 0;
}

void handle_request(int client_fd, wasm_engine_t *engine) {
    headers_t hdrs = {0};
    if (read_headers(client_fd, &hdrs) < 0) return;

    char full_wasm_path[2048];
    snprintf(full_wasm_path, sizeof(full_wasm_path), "%s%s.wasm",
             g_cfg.exec_path, hdrs.handler_name);

    if (access(full_wasm_path, F_OK) == -1) {
        write(client_fd, NOT_FOUND, NOT_FOUND_LEN);
        LOG_ERROR("Wasm handler not found: %s", full_wasm_path);
        return;
    }

    size_t content_length = 0;
    char *body_buffer = NULL;
    char *cl_header = strcasestr(hdrs.headers, "Content-Length");
    if (cl_header) {
        content_length = strtoul(cl_header + 15, NULL, 10);
        if (content_length > 0) {
            body_buffer = malloc(content_length);
            if (!body_buffer) {
                LOG_ERROR("Failed to allocate request body buffer");
                write(client_fd, INTERNAL_ERROR, INTERNAL_ERROR_LEN);
                return;
            }

            ssize_t bytes_read = 0;
            while (bytes_read < content_length) {
                ssize_t n = read(client_fd, body_buffer + bytes_read, content_length - bytes_read);
                if (n <= 0) break;
                bytes_read += n;
            }
        }
    }

    char *wasm_res = NULL;
    size_t wasm_res_len = 0;

    int rc = wasm_execute(engine, full_wasm_path,
                          body_buffer, content_length,
                          &wasm_res, &wasm_res_len);

    if (body_buffer) free(body_buffer);

    if (rc != 0 || !wasm_res) {
        write(client_fd, INTERNAL_ERROR, INTERNAL_ERROR_LEN);
        return;
    }

    char header[256];
    int body_len = (int)wasm_res_len;
    int header_len = snprintf(header, sizeof(header),
        "HTTP/1.1 200 OK\r\n"
        "Content-Length: %d\r\n"
        "Content-Type: application/json\r\n"
        "Connection: close\r\n"
        "\r\n",
        body_len);

    write(client_fd, header, header_len);
    write(client_fd, wasm_res, wasm_res_len);

    free(wasm_res);
}


void exec_worker(int listen_fd) {
    if (g_cfg.daemonize) worker_redirect_logs();

    wasm_engine_t* engine = wasm_engine_new();
    assert(engine != NULL);

    signal(SIGCHLD, SIG_IGN);

    struct sockaddr_in client_addr;
    socklen_t client_len = sizeof(client_addr);
    char client_ip[INET_ADDRSTRLEN];
    int client_fd;

    LOG_INFO("Worker (PID %d) running on FD %d", getpid(), listen_fd);

    while (1) {
        client_fd = accept(listen_fd, (struct sockaddr*)&client_addr, &client_len);
        if (client_fd < 0) { if (errno == EINTR) continue; LOG_ERROR("accept failed"); break; }

        inet_ntop(AF_INET, &client_addr.sin_addr, client_ip, sizeof(client_ip));
        LOG_INFO("Accepted connection from %s:%d FD %d", client_ip, ntohs(client_addr.sin_port), client_fd);

        handle_request(client_fd, engine);
        close(client_fd);
    }
}
