#include <caffeine_cfg.h>
#include <caffeine_utils.h>
#include <log.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#define MAX_LINE_LENGTH 256

config_t g_cfg;

void print_usage(const char *progname) {
    fprintf(stderr, "Usage: %s [OPTIONS]\n", progname);
    fprintf(stderr, "Caffeine: A high-performance pre-fork web server.\n\n");
    fprintf(stderr, "--- Instance Control ---\n");
    fprintf(stderr, "  -n, --name <name>      Specify a unique instance name (MANDATORY for -d, -s, -l, -r).\n");
    fprintf(stderr, "  -d, --daemon           Run the specified instance in the background.\n");
    fprintf(stderr, "  -s, --stop             Send SIGTERM to stop the specified instance.\n");
    fprintf(stderr, "  -L, --list-instances   Show all running instances (by scanning PID files).\n");
    fprintf(stderr, "\n--- Core Server Settings ---\n");
    fprintf(stderr, "  -c, --config <file>    Load configuration from the specified file path (processed immediately).\n");
    fprintf(stderr, "  -p, --port <port>      Set the listening port (default: %d).\n", DEFAULT_PORT);
    fprintf(stderr, "  -w, --workers <num>    Set the number of worker processes (default: %d).\n", DEFAULT_WORKERS);
    fprintf(stderr, "  --path <path>          Set the base path for executable handlers (default: %s).\n", EXEC_PATH);
    fprintf(stderr, "\n--- Logging & Utilities ---\n");
    fprintf(stderr, "  -l, --log              Display the log file for the instance specified by -n.\n");
    fprintf(stderr, "  --log-level <level>    Set logging verbosity (DEBUG, INFO, WARN, ERROR) (default: %s).\n", DEFAULT_LOG_LEVEL);
    fprintf(stderr, "  --reset-logs           Clear the log file for the specified instance.\n");
    fprintf(stderr, "  -h, --help             Display this help message and exit.\n");
    fprintf(stderr, "\n");
}

void init_config()
{
    memset(&g_cfg, 0, sizeof(config_t));
    g_cfg.port = DEFAULT_PORT;
    g_cfg.workers = DEFAULT_WORKERS;
}

static char* trim_whitespace(char *str) {
    if (!str) return NULL;
    char *end;

    while(isspace((unsigned char)*str)) str++;

    if(*str == 0) return str;

    end = str + strlen(str) - 1;
    while(end > str && isspace((unsigned char)*end)) end--;

    *(end + 1) = 0;

    return str;
}

static void parse_config_line(char *line) {
    char *key;
    char *value;

    if (line[0] == '#' || line[0] == '\n' || line[0] == '\0') {
        return;
    }

    value = strchr(line, '=');
    if (value == NULL) {
        LOG_ERROR("Config file: Skipping malformed line (no '=')");
        return;
    }

    *value = '\0';
    value++;
    key = trim_whitespace(line);
    value = trim_whitespace(value);

    if (strcmp(key, "port") == 0) {
        g_cfg.port = atoi(value);
        LOG_DEBUG("Config read: port = %d", g_cfg.port);
    } else if (strcmp(key, "workers") == 0) {
        g_cfg.workers = atoi(value);
        LOG_DEBUG("Config read: workers = %d", g_cfg.workers);
    } else if (strcmp(key, "log_level") == 0) {
        if (g_cfg.log_level) free(g_cfg.log_level);
        g_cfg.log_level = strdup(value);
        LOG_DEBUG("Config read: log_level = %s", g_cfg.log_level);
    } else if (strcmp(key, "base_path") == 0) {
        if (g_cfg.exec_path) free(g_cfg.exec_path);
        g_cfg.exec_path = strdup(value);
        LOG_DEBUG("Config read: exec_path = %s", g_cfg.exec_path);
    } 
}

static int read_config_file(const char *path) {
    FILE *file;
    char line[MAX_LINE_LENGTH];
    
    if (!path) {
        LOG_ERROR("read_config_file called without a path.");
        return -1;
    }

    file = fopen(path, "r");
    if (file == NULL) {
        LOG_ERROR("failed to open configuration file at: %s. Check path and permissions.", path);
        return -1;
    }

    LOG_DEBUG("reading configuration from: %s", path);
    
    while (fgets(line, MAX_LINE_LENGTH, file) != NULL) {
        parse_config_line(line);
    }

    fclose(file);
    return 0;
}

int parse_arguments(int argc, char **argv) {
    for (int i = 1; i < argc; i++) {
        char *arg = argv[i];

        #define CHECK_ARG(option) \
            if (i + 1 >= argc) { \
                LOG_ERROR("option %s requires an argument.", option); \
                print_usage(argv[0]); \
                return -1; \
            } \
            i++;

        if (strcmp(arg, "-n") == 0 || strcmp(arg, "--name") == 0) {
            CHECK_ARG(arg);
            free(g_cfg.instance_name);
            g_cfg.instance_name = strdup(argv[i]);
        } else if (strcmp(arg, "-p") == 0 || strcmp(arg, "--port") == 0) {
            CHECK_ARG(arg);
            g_cfg.port = atoi(argv[i]);
        } else if (strcmp(arg, "-w") == 0 || strcmp(arg, "--workers") == 0) {
            CHECK_ARG(arg);
            g_cfg.workers = atoi(argv[i]);
        } else if (strcmp(arg, "--log-level") == 0) {
            CHECK_ARG(arg);
            g_cfg.log_level = strdup(argv[i]);
        } else if (strcmp(arg, "--path") == 0) {
            CHECK_ARG(arg);
            g_cfg.exec_path = strdup(argv[i]);
        } else if (strcmp(arg, "-c") == 0 || strcmp(arg, "--config") == 0) {
            CHECK_ARG(argv[i]);
            if (read_config_file(arg) < 0) {
                LOG_ERROR("an error occured while trying to read the configuration file");
                return -1;
            }
        } else if (strcmp(arg, "-d") == 0 || strcmp(arg, "--daemon") == 0) {
            g_cfg.daemonize = 1;
        } else if (strcmp(arg, "-s") == 0 || strcmp(arg, "--stop") == 0) {
            g_cfg.stop_instance = 1;
        } else if (strcmp(arg, "-L") == 0 || strcmp(arg, "--list-instances") == 0) {
            g_cfg.list_instances = 1;
        } else if (strcmp(arg, "-l") == 0 || strcmp(arg, "--log") == 0) {
            g_cfg.show_log = 1;
        } else if (strcmp(arg, "--reset-logs") == 0) {
            g_cfg.reset_logs = 1;
        } else if (strcmp(arg, "-h") == 0 || strcmp(arg, "--help") == 0) {
            print_usage(argv[0]);
            return -1;
        } else {
            LOG_ERROR("unknown option: %s", arg);
            print_usage(argv[0]);
            return -1;
        }
    }
    
    if ((g_cfg.daemonize || g_cfg.stop_instance || g_cfg.show_log || g_cfg.reset_logs) && 
        (g_cfg.instance_name == NULL || strcmp(g_cfg.instance_name, "caffeine_default") == 0)) 
    {
        LOG_ERROR("The commands -d, -s, -l, and --reset-logs require a unique instance name (-n <name>).");
        print_usage(argv[0]);
        return -1;
    }
    
    #undef CHECK_ARG
    if (!g_cfg.exec_path) g_cfg.exec_path = get_default_path();
    if (!g_cfg.log_level) g_cfg.log_level = strdup(DEFAULT_LOG_LEVEL);
    return 0;
}
