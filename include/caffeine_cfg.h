#ifndef CAFFEINE_CFG_H
#define CAFFEINE_CFG_H

#define DEFAULT_WORKERS 4
#define DEFAULT_PORT 8080
#define DEFAULT_LOG_LEVEL "INFO"

typedef struct {
    int     port;
    int     workers;
    int     daemonize;
    int     show_log;
    int     reset_logs;
    int     stop_instance;
    int     list_instances;
    char    *instance_name;
    char    *exec_path;
    char    *log_level;
    char    *socket_path;
    char    *log_path;
    char    *pid_path;
}   config_t;

extern config_t g_cfg;

void init_config();
int parse_arguments(int argc, char **argv);

#endif