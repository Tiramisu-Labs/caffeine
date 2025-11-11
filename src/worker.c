#define _GNU_SOURCE         /* See feature_test_macros(7) */
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
#include <wasm.h> 
#include <wasmtime.h>

#define MAX_ENV_STRINGS 128
#define MAX_ENV_LENGTH 512

typedef struct {
    wasm_engine_t* engine;
    wasmtime_store_t* store;
} WasmContext;

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
    FILE* file = NULL;
    struct stat stat_buf;
    
    if (stat(path, &stat_buf) != 0) {
        return false;
    }
    size_t file_size = (size_t)stat_buf.st_size;
    
    file = fopen(path, "rb");
    if (!file) {
        return false;
    }

    uint8_t* data = (uint8_t*)malloc(file_size);
    if (!data) {
        fclose(file);
        return false;
    }

    if (fread(data, 1, file_size, file) != file_size) {
        free(data);
        fclose(file);
        return false;
    }
    fclose(file);

    out->size = file_size;
    out->data = data;
    return true;
}

int wasm_execute(
    wasm_engine_t *engine, 
    wasmtime_store_t *store, 
    const char *wasm_path, 
    const char *req_data, 
    size_t req_len, 
    char **res_data, 
    size_t *res_len) 
{
    wasmtime_error_t *error = NULL;
    wasm_trap_t *trap = NULL;
    int execution_result = -1;

    wasm_byte_vec_t binary;
    if (!load_wasm_file_binary(wasm_path, &binary)) {
        goto cleanup_exit;
    }

    wasmtime_module_t *module = NULL;
    error = wasmtime_module_new(engine, (uint8_t *)binary.data, binary.size, &module);
    free(binary.data);
    if (error) {
        goto cleanup_exit;
    }

    wasmtime_instance_t instance;
    
    error = wasmtime_instance_new(wasmtime_store_context(store), module, NULL, 0, &instance, &trap); 
    wasmtime_module_delete(module);
    if (error || trap) {
        if (trap) wasm_trap_delete(trap);
        goto cleanup_exit;
    }

    wasmtime_extern_t item;
    wasmtime_func_t handler_func;
    wasmtime_memory_t memory;
    bool ok;

    ok = wasmtime_instance_export_get(wasmtime_store_context(store), &instance, "handle_request", 
                                     strlen("handle_request"), &item);
    if (!ok || item.kind != WASMTIME_EXTERN_FUNC) {
        LOG_ERROR("WASM module does not export 'handle_request' function.");
        goto cleanup_exit;
    }
    handler_func = item.of.func;

    ok = wasmtime_instance_export_get(wasmtime_store_context(store), &instance, "memory", 
                                     strlen("memory"), &item);
    if (!ok || item.kind != WASMTIME_EXTERN_MEMORY) {
        LOG_ERROR("WASM module does not export 'memory'.");
        goto cleanup_exit;
    }
    memory = item.of.memory;
    
    char *wasm_memory_ptr = wasmtime_memory_data(wasmtime_store_context(store), &memory);
    size_t mem_size = wasmtime_memory_data_size(wasmtime_store_context(store), &memory);
    
    const int32_t REQUEST_OFFSET = 1024; 
    if (mem_size < REQUEST_OFFSET + req_len) {
        LOG_ERROR("WASM memory too small for request data.");
        goto cleanup_exit;
    }
    
    memcpy(wasm_memory_ptr + REQUEST_OFFSET, req_data, req_len);

    wasmtime_val_t args[2];
    args[0].kind = WASMTIME_I32; args[0].of.i32 = REQUEST_OFFSET;
    args[1].kind = WASMTIME_I32; args[1].of.i32 = (int32_t)req_len;

    wasmtime_val_t results[1];
    
    error = wasmtime_func_call(wasmtime_store_context(store), &handler_func, args, 2, results, 1, &trap);

    if (error || trap) {
        if (trap) wasm_trap_delete(trap);
        goto cleanup_exit;
    }

    int64_t packed_res = results[0].of.i64;
    int32_t res_ptr = (int32_t)(packed_res >> 32);
    *res_len = (int32_t)(packed_res & 0xFFFFFFFF);
    
    if ((size_t)res_ptr + *res_len > mem_size) {
        LOG_ERROR("Wasm module returned invalid memory bounds.");
        goto cleanup_exit;
    }
    
    *res_data = (char*)malloc(*res_len);
    if (!*res_data) {
        LOG_ERROR("Failed to allocate host memory for result.");
        goto cleanup_exit;
    }
    
    memcpy(*res_data, wasm_memory_ptr + res_ptr, *res_len);
    execution_result = 0;

cleanup_exit:
    if (error) wasmtime_error_delete(error);
    return execution_result;
}

void handle_request(int client_fd, wasm_engine_t *engine, wasmtime_store_t *store) {
    headers_t hdrs = {0};
    if (read_headers(client_fd, &hdrs) < 0) return;
    
    char full_wasm_path[2048];
    snprintf(full_wasm_path, sizeof(full_wasm_path), "%s%s.wasm", g_cfg.exec_path, hdrs.handler_name);
    
    if (access(full_wasm_path, F_OK) == -1) {
        write(client_fd, NOT_FOUND, NOT_FOUND_LEN);
        return ;
        LOG_ERROR("Wasm handler not found at: %s", full_wasm_path);
    }

    char *content_length = strcasestr(hdrs.headers, "Content-Length");
    if (content_length) {
        char length_buffer[32];
        const char *end = strchr(content_length, ' ');
        if (!end || end - content_length >= sizeof(length_buffer)) {
            write(client_fd, BAD_REQUEST, strlen(BAD_REQUEST));
            return;
        }
        strncpy(length_buffer, content_length, end - content_length);
        length_buffer[end - content_length] = '\0';
        hdrs.content_length = strtol(length_buffer, NULL, 10);
        if (hdrs.content_length == LONG_MIN || hdrs.content_length == LONG_MAX) {
            LOG_ERROR("error: %s", strerror(errno));
        }
    }
    size_t total_payload_len = hdrs.content_length + hdrs.headers_size;
    char *body_buffer = NULL;
    if (hdrs.content_length > 0) {
        body_buffer = (char*)malloc(hdrs.content_length);
        if (!body_buffer) {
            LOG_ERROR("Failed to allocate body buffer.");
            return;
        }
        
        ssize_t body_already_read = hdrs.bytes_read - (hdrs.headers_end - hdrs.headers) - 4;
        ssize_t body_bytes_streamed = 0;

        if (body_already_read > 0) {
            char *body_start = hdrs.headers_end + 4;
            strncpy(body_buffer, hdrs.headers + hdrs.headers_size, hdrs.content_length);
            body_bytes_streamed += body_already_read;
        }

        ssize_t remaining = (ssize_t)(content_length - body_bytes_streamed);
        
        while (remaining > 0) {
            struct pollfd pfd = {.fd = client_fd, .events = POLLIN};
            int poll_result = poll(&pfd, 1, 5000);

            if (poll_result < 0) {
                if (errno == EINTR) continue;
                LOG_ERROR("poll failed during body stream: %s", strerror(errno));
                break;
            } else if (poll_result == 0) {
                LOG_WARN("Client timeout mid-stream on FD %d. Sent %zd/%d bytes.", client_fd, body_bytes_streamed, content_length);
                break;
            }

            char stream_buffer[4096];
            ssize_t chunk_size = read(client_fd, stream_buffer, sizeof(stream_buffer));

            if (chunk_size > 0) {
                strncpy(body_buffer + body_bytes_streamed, stream_buffer, remaining);
                body_bytes_streamed += chunk_size;
                remaining -= chunk_size;
            } else if (chunk_size == 0) {
                break;
            } else if (chunk_size < 0) {
                if (errno == EINTR) continue;
                LOG_ERROR("read failed during body stream: %s", strerror(errno));
                break;
            }
        }
    }

    char *wasm_res_data = NULL;
    size_t wasm_res_len = 0;
    
    int result = wasm_execute(
        engine, 
        store,
        full_wasm_path, 
        body_buffer,
        hdrs.content_length,
        &wasm_res_data, 
        &wasm_res_len);
        
    if (body_buffer) free(body_buffer);

    if (result != 0 || wasm_res_data == NULL) {
        write(client_fd, INTERNAL_ERROR, INTERNAL_ERROR_LEN);
        return;
    }

    printf("wasm res_data %s\n", wasm_res_data);
    free(wasm_res_data); 
}

void exec_worker(int listen_fd) {
    if (g_cfg.daemonize) worker_redirect_logs();

    wasm_engine_t* engine = wasm_engine_new();
    assert(engine != NULL);

    wasmtime_store_t *store = wasmtime_store_new(engine, NULL, NULL);
    assert(store != NULL);
    wasmtime_context_t *context = wasmtime_store_context(store);

    signal(SIGCHLD, SIG_IGN);

    LOG_INFO("Worker (PID %d) is running and listening on shared FD %d.", getpid(), listen_fd);

    struct sockaddr_in client_addr;
    socklen_t client_len = sizeof(client_addr);
    char client_ip[INET_ADDRSTRLEN];
    int client_fd;

    while (1) {
        client_fd = accept(listen_fd, (struct sockaddr *)&client_addr, &client_len);

        if (client_fd < 0) {
            if (errno == EINTR) continue;
            
            LOG_ERROR("Worker accept() failed: %s", strerror(errno));
            close(listen_fd);
            exit(EXIT_FAILURE);
        }

        inet_ntop(AF_INET, &client_addr.sin_addr, client_ip, INET_ADDRSTRLEN);

        LOG_INFO("Worker (PID %d) accepted connection from %s:%d on new FD %d.",
                getpid(), client_ip, ntohs(client_addr.sin_port), client_fd);

        handle_request(client_fd, engine, store);
        
        if (close(client_fd) < 0) {
            if (errno == EBADF) {
                LOG_DEBUG("Expected EBADF (FD already closed by child) on client FD %d.", client_fd);
            } else {
                LOG_WARN("Unexpected error closing client FD %d: %s", client_fd, strerror(errno));
            }
        } else {
            LOG_DEBUG("Successfully closed client FD %d.", client_fd);
        }

        LOG_DEBUG("Worker (PID %d) finished and closed client FD %d. Waiting for next connection.", getpid(), client_fd);
    }

    LOG_INFO("Worker exiting...");
    close(listen_fd);
    exit(EXIT_SUCCESS);
}
