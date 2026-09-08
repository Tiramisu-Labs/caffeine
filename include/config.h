#ifndef CONFIG_H
#define CONFIG_H

typedef struct {
    int   listen_fd;
    int   port;
    int   min_workers;
    int   max_workers;
    int   current_workers;

    char *handler_root;
    char *log_level;
} config_t;

#define DEFAULT_WORKERS   4
#define DEFAULT_PORT      8080
#define DEFAULT_LOG_LEVEL "INFO"

extern config_t g_cfg;

int config_init(int argc, char **argv);
void config_destroy(void);

#endif