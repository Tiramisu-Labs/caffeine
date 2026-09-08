#include <config.h>
#include <log.h>

#include <errno.h>
#include <getopt.h>
#include <limits.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

config_t g_cfg = {
    .listen_fd = -1,
    .port = DEFAULT_PORT,
    .min_workers = DEFAULT_WORKERS,
    .max_workers = DEFAULT_WORKERS,
    .current_workers = 0,
    .handler_root = NULL,
    .log_level = NULL
};

static int parse_int(
    const char *value,
    int min,
    int max,
    int *result
)
{
    char *end = NULL;
    long parsed;

    if (!value || !result)
        return -1;

    errno = 0;
    parsed = strtol(value, &end, 10);

    if (errno != 0 || end == value || *end != '\0')
        return -1;

    if (parsed < min || parsed > max)
        return -1;

    *result = (int)parsed;
    return 0;
}

static char *build_default_handler_root(void)
{
    const char *home = getenv("HOME");

    if (!home || home[0] == '\0') {
        struct passwd *pw = getpwuid(getuid());

        if (!pw || !pw->pw_dir)
            return NULL;

        home = pw->pw_dir;
    }

    size_t len = strlen(home) + strlen("/.config/caffeine/handlers") + 1;

    char *path = malloc(len);

    if (!path)
        return NULL;

    snprintf(
        path,
        len,
        "%s/.config/caffeine/handlers",
        home
    );

    return path;
}

static void print_help(const char *program)
{
    printf(
        "Usage: %s [options]\n"
        "\n"
        "Options:\n"
        "  -p, --port PORT         Listen port\n"
        "  -w, --workers N         Number of workers\n"
        "      --path PATH         Handler root directory\n"
        "      --log-level LEVEL   Log level\n"
        "  -h, --help              Show this help\n",
        program
    );
}

static int ensure_directory(const char *path)
{
    char tmp[PATH_MAX];

    if (!path || strlen(path) >= sizeof(tmp))
        return -1;

    strcpy(tmp, path);

    for (char *p = tmp + 1; *p; p++) {
        if (*p != '/')
            continue;

        *p = '\0';

        if (mkdir(tmp, 0755) < 0 && errno != EEXIST)
            return -1;

        *p = '/';
    }

    if (mkdir(tmp, 0755) < 0 && errno != EEXIST)
        return -1;

    return 0;
}
int config_init(int argc, char **argv)
{
    g_cfg.handler_root = build_default_handler_root();

    if (!g_cfg.handler_root) {
        LOG_ERROR("failed to determine handler root");
        return -1;
    }

    static const struct option long_options[] = {
        { "port",     required_argument, NULL, 'p' },
        { "workers",  required_argument, NULL, 'w' },
        { "path",     required_argument, NULL,  1  },
        { "log-level", required_argument, NULL, 2  },
        { "help",     no_argument,       NULL, 'h' },
        { NULL,       0,                 NULL,  0  }
    };

    int option;

    while ((option = getopt_long(
        argc,
        argv,
        "p:w:h",
        long_options,
        NULL
    )) != -1) {
        switch (option) {
        case 'p':
            if (parse_int(optarg, 1, 65535, &g_cfg.port) < 0) {
                fprintf(stderr, "invalid port: %s\n", optarg);
                return -1;
            }
            break;

        case 'w':
            if (parse_int(optarg, 1, 64, &g_cfg.min_workers) < 0) {
                fprintf(stderr, "invalid worker count: %s\n", optarg);
                return -1;
            }

            g_cfg.max_workers = g_cfg.min_workers;
            break;

        case 1:
            free(g_cfg.handler_root);

            g_cfg.handler_root = strdup(optarg);

            if (!g_cfg.handler_root) {
                LOG_ERROR("failed to allocate handler path");
                return -1;
            }
            break;

        case 2:
            free(g_cfg.log_level);

            g_cfg.log_level = strdup(optarg);

            if (!g_cfg.log_level) {
                LOG_ERROR("failed to allocate log level");
                return -1;
            }

            set_log_level(g_cfg.log_level);
            break;

        case 'h':
            print_help(argv[0]);
            return 1;

        default:
            return -1;
        }
    }

    if (ensure_directory(g_cfg.handler_root) < 0) {
        LOG_ERROR(
            "failed to create handler root %s: %s",
            g_cfg.handler_root,
            strerror(errno)
        );
        return -1;
    }

    return 0;
}

void config_destroy(void)
{
    free(g_cfg.handler_root);
    free(g_cfg.log_level);

    g_cfg.handler_root = NULL;
    g_cfg.log_level = NULL;
}