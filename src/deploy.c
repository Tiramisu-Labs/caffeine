#include <deploy.h>
#include <caffeine_cfg.h>
#include <log.h>
#include <sys/stat.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <stdio.h>
#include <dirent.h>
#include <libgen.h>
#include <fcntl.h>

#define COPY_BUFFER_SIZE 4096

int deploy_single_file(const char *src_path, const char *dst_path) {
    int src_fd = -1;
    int dst_fd = -1;
    char buffer[COPY_BUFFER_SIZE];
    ssize_t bytes_read, bytes_written;
    int result = -1;
    
    printf("caffeine: deploying file: '%s'\n", src_path);
    src_fd = open(src_path, O_RDONLY);
    if (src_fd == -1) {
        fprintf(stderr, "caffine: error: failed to open source file '%s': %s\n", src_path, strerror(errno));
        return -1;
    }

    dst_fd = open(dst_path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (dst_fd == -1) {
        fprintf(stderr, "caffeine: error: failed to create destination file '%s': %s\n", dst_path, strerror(errno));
        goto cleanup;
    }

    while ((bytes_read = read(src_fd, buffer, COPY_BUFFER_SIZE)) > 0) {
        bytes_written = write(dst_fd, buffer, bytes_read);
        
        if (bytes_written != bytes_read) {
            if (bytes_written == -1) {
                fprintf(stderr,"%scaffeine: error: write error to destination file '%s': %s%s\n", COLOR_BRIGHT_RED, dst_path, strerror(errno), COLOR_RESET);
            } else {
                fprintf(stderr,"%scaffeine: error: incomplete write to destination file '%s'%s\n",COLOR_BRIGHT_RED, dst_path, COLOR_RESET);
            }
            goto cleanup;
        }
    }

    if (bytes_read == -1) {
        fprintf(stderr, "%scaffeine: error: read error from source file '%s': %s%s", COLOR_BRIGHT_RED, src_path, strerror(errno), COLOR_RESET);
        goto cleanup;
    }
    if (chmod(dst_path, 0755) == -1) {
        fprintf(stdout, "%scaffeine: warning: failed to set executable permissions on '%s': %s%s\n", COLOR_BRIGHT_YELLOW, dst_path, strerror(errno), COLOR_RESET);
    }
    result = 0;

cleanup:
    if (src_fd != -1) close(src_fd);
    if (dst_fd != -1) close(dst_fd);

    return result;
}

int deploy_directory_recursive(const char *src, const char *dst) {
    DIR *dir;
    struct dirent *entry;
    struct stat st;
    int result = 0;

    if ((dir = opendir(src)) == NULL) {
        fprintf(stderr, "%scaffeine: error: cannot open source directory '%s': %s%s\n", COLOR_BRIGHT_RED, src, strerror(errno), COLOR_RESET);
        return -1;
    }

    while ((entry = readdir(dir)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }

        char src_path[MAX_PATH];
        char dst_path[MAX_PATH];
        
        snprintf(src_path, MAX_PATH, "%s/%s", src, entry->d_name);
        snprintf(dst_path, MAX_PATH, "%s/%s", dst, entry->d_name);

        if (stat(src_path, &st) == -1) {
            fprintf(stdout, "%scaffeine: warning: could not stat file '%s', skipping: %s%s\n", COLOR_BRIGHT_YELLOW, src_path, strerror(errno), COLOR_RESET);
            continue;
        }

        if (S_ISDIR(st.st_mode)) {
            if (mkdir(dst_path, 0755) == -1) {
                if (errno != EEXIST) {
                    fprintf(stderr, "%scaffeine: error: failed to create destination subdirectory '%s': %s%s\n", COLOR_BRIGHT_RED, dst_path, strerror(errno), COLOR_RESET);
                    result = -1;
                    break;
                }
            }
            result = deploy_directory_recursive(src_path, dst_path);
            if (result == -1) break;

        } else if (S_ISREG(st.st_mode)) {
            result = deploy_single_file(src_path, dst_path);
            if (result == -1) break;
        }
    }

    closedir(dir);
    return result;
}

int deploy_directory(const char *source_path, const char *target_dir) {
    char target_path_buffer[MAX_PATH];
    const char *folder_name = basename((char *)source_path);

    if (snprintf(target_path_buffer, MAX_PATH, "%s/%s", target_dir, folder_name) >= MAX_PATH) {
        fprintf(stderr, "%scaffeine: error: target path buffer overflow for initial directory%s\n", COLOR_BRIGHT_RED, COLOR_RESET);
        return -1;
    }

    if (mkdir(target_path_buffer, 0755) == -1) {
        if (errno != EEXIST) {
            fprintf(stderr, "%scaffeine: error: Failed to create target directory '%s': %s%s\n", COLOR_BRIGHT_RED, target_path_buffer, strerror(errno), COLOR_RESET);
            return -1;
        }
    }

    return deploy_directory_recursive(source_path, target_path_buffer);
}

int handle_deploy(const char *source_path) {
    struct stat st;
    char *target_dir = g_cfg.exec_path;

    if (stat(source_path, &st) == -1) {
        fprintf(stderr, "%scaffeine: error: deploy source '%s': %s%s\n",COLOR_BRIGHT_RED, source_path, strerror(errno), COLOR_RESET);
        return -1;
    }

    if (S_ISREG(st.st_mode)) {
        char full_path[512] = {0};
        size_t dst_path_len = strlen(target_dir);
        strncpy(full_path, target_dir, dst_path_len);
        char *file_name = strrchr(source_path, '/');
        if (file_name) strncpy(full_path + dst_path_len, file_name + 1, strlen(file_name) - 1);
        else strncpy(full_path + dst_path_len, source_path, strlen(source_path));
        return deploy_single_file(source_path, full_path);
    } else if (S_ISDIR(st.st_mode)) {
        fprintf(stdout, "caffeine: deploying directory contents: %s\n", source_path);
        return deploy_directory(source_path, target_dir);
    } else {
        fprintf(stderr, "%scaffeine: error: deploy source '%s' is not a file or directory.%s\n",COLOR_BRIGHT_RED, source_path, COLOR_RESET);
        return -1;
    }
}