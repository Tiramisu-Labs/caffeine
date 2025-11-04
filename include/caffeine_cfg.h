#ifndef CAFFEINE_CFG_H
#define CAFFEINE_CFG_H

#define DEFAULT_WORKERS 4
#define DEFAULT_PORT 8080
#define DEFAULT_LOG_LEVEL "INFO"

#include <inttypes.h>

typedef struct {
    uint8_t     daemonize;
    uint8_t     show_log;
    uint8_t     reset_logs;
    uint8_t     stop_instance;
    uint8_t     list_instances;
    uint8_t     deploy;
    int         port;
    int         workers;
    char        *instance_name;
    char        *exec_path;
    char        *log_level;
    char        *socket_path;
    char        *log_path;
    char        *pid_path;
    char        **deploy_start;
}   config_t;

extern config_t g_cfg;

void init_config();
int is_flag(char *s);
int parse_arguments(int argc, char **argv);

#endif