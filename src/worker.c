#define _GNU_SOURCE

#include <caffeine.h>
#include <config.h>
#include <utils.h>
#include <handler_registry.h>
#include <shared_mem.h>
#include <log.h>
#include <response.h>
#include <headers.h>

#include <cJSON.h>

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

static volatile sig_atomic_t g_worker_shutdown = 0;


static void worker_signal_handler(int sig)
{
    if (sig == SIGTERM || sig == SIGINT)
        g_worker_shutdown = 1;
}


static int worker_setup_signals(void)
{
    struct sigaction sa;

    memset(&sa, 0, sizeof(sa));

    sa.sa_handler = worker_signal_handler;
    sigemptyset(&sa.sa_mask);

    /*
     * Do not use SA_RESTART.
     *
     * We want blocking accept()/read() calls to return EINTR
     * when the worker receives SIGTERM.
     */
    sa.sa_flags = 0;

    if (sigaction(SIGTERM, &sa, NULL) < 0)
        return -1;

    if (sigaction(SIGINT, &sa, NULL) < 0)
        return -1;


    /*
     * The master blocks these signals before fork().
     * Workers must unblock the signals they handle themselves.
     */
    sigset_t mask;

    sigemptyset(&mask);
    sigaddset(&mask, SIGTERM);
    sigaddset(&mask, SIGINT);

    if (sigprocmask(SIG_UNBLOCK, &mask, NULL) < 0)
        return -1;

    return 0;
}


static void set_worker_state(
    shm_layout_t *map,
    int worker_index,
    worker_state_t state
)
{
    atomic_store(
        &map->workers[worker_index].state,
        state
    );
}


static void handle_request(
    int client_fd,
    const handler_registry_t *registry,
    shm_layout_t *map,
    int worker_index
)
{
    struct timeval tv = {
        .tv_sec = 5,
        .tv_usec = 0
    };

    if (setsockopt(
            client_fd,
            SOL_SOCKET,
            SO_RCVTIMEO,
            &tv,
            sizeof(tv)
        ) < 0) {
        LOG_WARN(
            "failed to set receive timeout: %s",
            strerror(errno)
        );
    }


    headers_t hdrs = {0};

    if (read_headers(client_fd, &hdrs) < 0) {
        if (!g_worker_shutdown)
            LOG_WARN("failed to read request headers");

        return;
    }


    handler_entry_t *handler =
        handler_registry_find(
            registry,
            hdrs.host,
            hdrs.path
        );

    if (!handler) {
        write_fully(
            client_fd,
            NOT_FOUND,
            NOT_FOUND_LEN
        );

        return;
    }


    /*
     * The worker is now executing user code.
     */
    set_worker_state(
        map,
        worker_index,
        W_BUSY
    );

    uint64_t start = now_ms();

    atomic_store(
        &map->workers[worker_index].start_ms,
        start
    );

    atomic_store(
        &map->workers[worker_index].deadline_ms,
        start + handler->timeout_ms
    );


    /*
     * Keep the current ABI unchanged for now.
     */
    char response_buffer[65536];
    size_t response_length = 0;

    cJSON *request_json = cJSON_CreateObject();

    if (!request_json) {
        write_fully(
            client_fd,
            INTERNAL_ERROR,
            INTERNAL_ERROR_LEN
        );

        goto done;
    }


    cJSON_AddStringToObject(
        request_json,
        "handler",
        hdrs.path
    );

    cJSON_AddStringToObject(
        request_json,
        "method",
        hdrs.method
    );

    cJSON_AddStringToObject(
        request_json,
        "headers",
        hdrs.headers
    );


    char *request_string =
        cJSON_PrintUnformatted(request_json);

    if (!request_string) {
        cJSON_Delete(request_json);

        write_fully(
            client_fd,
            INTERNAL_ERROR,
            INTERNAL_ERROR_LEN
        );

        goto done;
    }


    const char *result = handler->func(
        request_string,
        response_buffer,
        sizeof(response_buffer),
        &response_length
    );

    const char *final_response =
        result != NULL
            ? result
            : response_buffer;


    cJSON *response_json =
        cJSON_Parse(final_response);

    if (!response_json) {
        write_fully(
            client_fd,
            INTERNAL_ERROR,
            INTERNAL_ERROR_LEN
        );

        free(request_string);
        cJSON_Delete(request_json);

        goto done;
    }


    cJSON *status =
        cJSON_GetObjectItem(response_json, "status");

    cJSON *body =
        cJSON_GetObjectItem(response_json, "body");


    int http_status =
        cJSON_IsNumber(status)
            ? status->valueint
            : 200;


    char *body_string = NULL;

    if (cJSON_IsObject(body) || cJSON_IsArray(body)) {
        body_string = cJSON_PrintUnformatted(body);
    }
    else if (cJSON_IsString(body)) {
        body_string = strdup(body->valuestring);
    }
    else {
        body_string = strdup("");
    }


    if (!body_string) {
        cJSON_Delete(response_json);
        free(request_string);
        cJSON_Delete(request_json);

        write_fully(
            client_fd,
            INTERNAL_ERROR,
            INTERNAL_ERROR_LEN
        );

        goto done;
    }


    char http_response[70000];

    int response_size = snprintf(
        http_response,
        sizeof(http_response),

        "HTTP/1.1 %d %s\r\n"
        "Content-Length: %zu\r\n"
        "Content-Type: application/json\r\n"
        "Connection: close\r\n"
        "\r\n"
        "%s",

        http_status,
        http_status >= 200 && http_status < 300
            ? "OK"
            : "Error",

        strlen(body_string),
        body_string
    );


    if (response_size > 0 &&
        (size_t)response_size < sizeof(http_response)) {

        write_fully(
            client_fd,
            http_response,
            (size_t)response_size
        );
    }
    else {
        write_fully(
            client_fd,
            INTERNAL_ERROR,
            INTERNAL_ERROR_LEN
        );
    }


    free(body_string);
    cJSON_Delete(response_json);
    free(request_string);
    cJSON_Delete(request_json);


done:

    atomic_store(
        &map->workers[worker_index].deadline_ms,
        0
    );

    atomic_store(
        &map->workers[worker_index].start_ms,
        0
    );

    if (!g_worker_shutdown) {
        set_worker_state(
            map,
            worker_index,
            W_IDLE
        );
    }
}


void exec_worker(
    int listen_fd,
    shm_layout_t *map,
    int worker_index,
    const handler_registry_t *registry
)
{
    if (!registry) {
        LOG_ERROR("worker received NULL handler registry");
        _exit(EXIT_FAILURE);
    }


    if (worker_setup_signals() < 0) {
        LOG_ERROR(
            "failed to setup worker signals: %s",
            strerror(errno)
        );

        _exit(EXIT_FAILURE);
    }


    LOG_INFO(
        "worker started: pid=%d index=%d",
        getpid(),
        worker_index
    );


    for (;;) {

        if (g_worker_shutdown)
            break;


        set_worker_state(
            map,
            worker_index,
            W_IDLE
        );


        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);

        int client_fd = accept(
            listen_fd,
            (struct sockaddr *)&client_addr,
            &client_len
        );


        if (client_fd < 0) {

            if (errno == EINTR) {
                if (g_worker_shutdown)
                    break;

                continue;
            }

            LOG_ERROR(
                "accept failed: %s",
                strerror(errno)
            );

            continue;
        }


        char client_ip[INET_ADDRSTRLEN] = {0};

        if (!inet_ntop(
                AF_INET,
                &client_addr.sin_addr,
                client_ip,
                sizeof(client_ip)
            )) {

            strncpy(
                client_ip,
                "unknown",
                sizeof(client_ip) - 1
            );
        }


        LOG_DEBUG(
            "worker %d accepted connection from %s:%d on fd %d",
            getpid(),
            client_ip,
            ntohs(client_addr.sin_port),
            client_fd
        );


        /*
         * A SIGTERM may have arrived immediately after accept().
         * Do not start new work while draining.
         */
        if (g_worker_shutdown) {
            close(client_fd);
            break;
        }


        handle_request(
            client_fd,
            registry,
            map,
            worker_index
        );


        if (close(client_fd) < 0) {
            LOG_WARN(
                "failed to close client fd %d: %s",
                client_fd,
                strerror(errno)
            );
        }


        if (g_worker_shutdown)
            break;
    }


    set_worker_state(
        map,
        worker_index,
        W_DRAINING
    );


    LOG_INFO(
        "worker %d shutting down",
        getpid()
    );

    _exit(EXIT_SUCCESS);
}