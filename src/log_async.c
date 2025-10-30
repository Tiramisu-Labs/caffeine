#include <log.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <errno.h>
#include <ctype.h>

static log_message_t *log_queue_head = NULL;
static log_message_t *log_queue_tail = NULL;
static pthread_mutex_t log_queue_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t log_queue_cond = PTHREAD_COND_INITIALIZER;
static volatile int keep_running = 1;
static pthread_t log_consumer_tid;
static int log_fd = -1;

log_level_t g_log_level;

const char* log_level_to_str(log_level_t level) {
    switch (level) {
        case DEBUG: return "DEBUG";
        case INFO:  return "INFO";
        case WARN:  return "WARN";
        case ERROR: return "ERROR";
        default: return "UNKNOWN";
    }
}

void set_log_level(const char *level_str) {
    char upper_level[10];
    if (level_str == NULL || strlen(level_str) >= sizeof(upper_level)) {
        g_log_level = INFO;
        return;
    }
    
    for (int i = 0; level_str[i] != '\0'; i++) {
        upper_level[i] = toupper((unsigned char)level_str[i]);
    }
    upper_level[strlen(level_str)] = '\0';

    if (strcmp(upper_level, "DEBUG") == 0) {
        g_log_level = DEBUG;
    } else if (strcmp(upper_level, "INFO") == 0) {
        g_log_level = INFO;
    } else if (strcmp(upper_level, "WARN") == 0) {
        g_log_level = WARN;
    } else if (strcmp(upper_level, "ERROR") == 0) {
        g_log_level = ERROR;
    } else {
        fprintf(stderr, "Warning: Invalid log level '%s' specified. Defaulting to INFO.\n", level_str);
        g_log_level = INFO;
    }
}

static log_message_t *create_log_message(const char *msg) {
    log_message_t *new_msg = (log_message_t *)malloc(sizeof(log_message_t));
    if (new_msg == NULL) return NULL;
    

    strncpy(new_msg->message, msg, LOG_MAX_MESSAGE_SIZE - 1);
    new_msg->message[LOG_MAX_MESSAGE_SIZE - 1] = '\0';
    new_msg->next = NULL;
    
    return new_msg;
}

static log_message_t *dequeue_log_message() {
    log_message_t *msg = log_queue_head;
    if (msg) {
        log_queue_head = msg->next;
        if (log_queue_head == NULL) {
            log_queue_tail = NULL;
        }
    }
    return msg;
}

void log_async_enqueue(log_level_t level, const char *fmt, ...) {
    if (!keep_running) return;

    char formatted_msg[LOG_MAX_MESSAGE_SIZE];
    va_list args;

    va_start(args, fmt);
    int len = vsnprintf(formatted_msg, LOG_MAX_MESSAGE_SIZE, fmt, args);
    va_end(args);

    if (len >= LOG_MAX_MESSAGE_SIZE || len < 0) {
        return; 
    }
    
    char time_buf[24];
    char final_entry[LOG_MAX_MESSAGE_SIZE + 64] = {0};
    time_t timer;
    struct tm *tm_info;
    time(&timer);
    tm_info = localtime(&timer);
    strftime(time_buf, sizeof(time_buf), "%Y-%m-%d %H:%M:%S", tm_info);

    const char *color = COLOR_RESET;
    if (level == ERROR) color = COLOR_BRIGHT_RED;
    else if (level == WARN) color = COLOR_BRIGHT_YELLOW;
    else if (level == INFO) color = COLOR_GREEN;
    else if (level == DEBUG) color = COLOR_CYAN;

    snprintf(final_entry, sizeof(final_entry), "%s%s [%s] [PID %d] %s%s\n",
         color, time_buf, log_level_to_str(level), getpid(), formatted_msg, COLOR_RESET);

    log_message_t *new_msg = create_log_message(final_entry);
    if (!new_msg) return;

    pthread_mutex_lock(&log_queue_mutex);

    if (log_queue_tail == NULL) {
        log_queue_head = new_msg;
        log_queue_tail = new_msg;
    } else {
        log_queue_tail->next = new_msg;
        log_queue_tail = new_msg;
    }

    pthread_cond_signal(&log_queue_cond);
    pthread_mutex_unlock(&log_queue_mutex);
}

static void *log_consumer_thread(void *arg) {
    (void)arg;
    struct timespec timeout;

    while (keep_running) {
        pthread_mutex_lock(&log_queue_mutex);
            
        if (log_queue_head == NULL) {
            clock_gettime(CLOCK_REALTIME, &timeout);
            timeout.tv_nsec += 100000000;
            if (timeout.tv_nsec >= 1000000000) {
                timeout.tv_sec += timeout.tv_nsec / 1000000000;
                timeout.tv_nsec %= 1000000000;
            }
            pthread_cond_timedwait(&log_queue_cond, &log_queue_mutex, &timeout);
        }
    
        log_message_t *current = log_queue_head;
        log_queue_head = NULL;
        log_queue_tail = NULL;
        
        pthread_mutex_unlock(&log_queue_mutex);
        
        while (current) {
            ssize_t written = write(log_fd, current->message, strlen(current->message));
            if (written < 0) {
                // failed to write log
            }         
            log_message_t *next = current->next;
            free(current);
            current = next;
        }
    }
    
    if (log_fd != -1) {
        close(log_fd);
    }   
    return NULL;
}

void init_logging(const char *log_file_path) {
    log_fd = open(log_file_path, O_CREAT | O_WRONLY | O_APPEND, 0644);
    if (log_fd < 0) {
    
        fprintf(stderr, "FATAL: Could not open log file %s: %s\n", log_file_path, strerror(errno));
        exit(EXIT_FAILURE);
    }
    

    keep_running = 1;
    if (pthread_create(&log_consumer_tid, NULL, log_consumer_thread, NULL) != 0) {
        fprintf(stderr, "FATAL: Could not start logging thread.\n");
        close(log_fd);
        exit(EXIT_FAILURE);
    }

    fprintf(stderr, "Asynchronous logging initialized.\n");
}

void shutdown_logging() {

    keep_running = 0;
    
    pthread_mutex_lock(&log_queue_mutex);
    pthread_cond_signal(&log_queue_cond);
    pthread_mutex_unlock(&log_queue_mutex);

    pthread_join(log_consumer_tid, NULL);
}
