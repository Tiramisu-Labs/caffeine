#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/types.h>
#include <unistd.h>
#include <signal.h>
#include <errno.h>
#include <caffeine.h>
#include <caffeine_utils.h>
#include <caffeine_cfg.h>
#include <log.h>

static int is_pid_running(pid_t pid) {
    if (pid <= 0) return 0;
    if (kill(pid, 0) == 0) return 1;
    if (errno == ESRCH) return 0; 
    
    return 1; 
}

static pid_t read_pid_from_file(const char *pid_file_path) {
    FILE *f = fopen(pid_file_path, "r");
    pid_t pid = -1;

    if (f == NULL) {
        LOG_ERROR("Failed to open PID file %s: %s", pid_file_path, strerror(errno));
        return -1;
    }

    if (fscanf(f, "%d", &pid) != 1) {
        LOG_ERROR("PID file %s corrupted or empty.", pid_file_path);
        pid = -1;
    }

    fclose(f);
    return pid;
}

void list_running_instances() {
    DIR *dir;
    struct dirent *entry;
    int found_any = 0;

    LOG_DEBUG("Scanning directory for PID files: %s", PID_PATH);
    
    dir = opendir(PID_PATH);
    if (dir == NULL) {
        fprintf(stderr, "ERROR: Could not open PID directory %s: %s\n", 
                PID_PATH, strerror(errno));
        return;
    }

    printf("--- Running Caffeine Instances ---\n");

    while ((entry = readdir(dir)) != NULL) {
        if (strstr(entry->d_name, PID_FILE_PREFIX) == entry->d_name &&
            strstr(entry->d_name, PID_FILE_SUFFIX) != NULL) {

            size_t name_start_len = strlen(PID_FILE_PREFIX);
            size_t name_end_len = strlen(PID_FILE_SUFFIX);
            size_t total_len = strlen(entry->d_name);
            size_t name_len = total_len - name_start_len - name_end_len;
            
            char *instance_name = (char *)malloc(name_len + 1);
            if (!instance_name) {
                fprintf(stderr, "ERROR: Memory allocation failed.\n");
                break;
            }
            
            strncpy(instance_name, entry->d_name + name_start_len, name_len);
            instance_name[name_len] = '\0';

            char full_path[512];
            snprintf(full_path, sizeof(full_path), "%s/%s", PID_PATH, entry->d_name);

            pid_t pid = read_pid_from_file(full_path);

            if (pid != -1 && is_pid_running(pid)) {
                printf("  [RUNNING] Name: %-20s PID: %d\n", instance_name, pid);
                found_any = 1;
            } else {
                LOG_DEBUG("Found stale PID file for instance '%s'. Removing: %s", instance_name, full_path);
                remove(full_path);
            }
            free(instance_name);
        }
    }

    if (!found_any) printf("No active Caffeine instances found.\n");
    closedir(dir);
}
