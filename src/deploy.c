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
    
    src_fd = open(src_path, O_RDONLY);
    if (src_fd == -1) {
        LOG_ERROR("Failed to open source file '%s': %s", src_path, strerror(errno));
        return -1;
    }

    dst_fd = open(dst_path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (dst_fd == -1) {
        LOG_ERROR("Failed to create destination file '%s': %s", dst_path, strerror(errno));
        goto cleanup;
    }

    while ((bytes_read = read(src_fd, buffer, COPY_BUFFER_SIZE)) > 0) {
        bytes_written = write(dst_fd, buffer, bytes_read);
        
        if (bytes_written != bytes_read) {
            if (bytes_written == -1) {
                LOG_ERROR("Write error to destination file '%s': %s", dst_path, strerror(errno));
            } else {
                LOG_ERROR("Incomplete write to destination file '%s'.", dst_path);
            }
            goto cleanup;
        }
    }

    if (bytes_read == -1) {
        LOG_ERROR("Read error from source file '%s': %s", src_path, strerror(errno));
        goto cleanup;
    }
    if (chmod(dst_path, 0755) == -1) {
        LOG_WARN("Failed to set executable permissions on '%s': %s", dst_path, strerror(errno));
    }

    LOG_INFO("Successfully deployed file: %s -> %s", src_path, dst_path);
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
        LOG_ERROR("Cannot open source directory '%s': %s", src, strerror(errno));
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
            LOG_WARN("Could not stat file '%s', skipping: %s", src_path, strerror(errno));
            continue;
        }

        if (S_ISDIR(st.st_mode)) {
            if (mkdir(dst_path, 0755) == -1) {
                if (errno != EEXIST) {
                    LOG_ERROR("Failed to create destination subdirectory '%s': %s", dst_path, strerror(errno));
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
        LOG_ERROR("Target path buffer overflow for initial directory.");
        return -1;
    }

    if (mkdir(target_path_buffer, 0755) == -1) {
        if (errno != EEXIST) {
            LOG_ERROR("Failed to create target directory '%s': %s", target_path_buffer, strerror(errno));
            return -1;
        }
    }

    return deploy_directory_recursive(source_path, target_path_buffer);
}

int handle_deploy(const char *source_path) {
    struct stat st;
    char *target_dir = g_cfg.exec_path;

    if (stat(source_path, &st) == -1) {
        LOG_ERROR("Deploy source '%s' not found or inaccessible: %s", source_path, strerror(errno));
        return -1;
    }

    if (S_ISREG(st.st_mode)) {
        char full_path[512] = {0};
        size_t dst_path_len = strlen(target_dir);
        strncpy(full_path, target_dir, dst_path_len);
        char *file_name = strrchr(source_path, '/');
        if (file_name) strncpy(full_path + dst_path_len, file_name + 1, strlen(file_name) - 1);
        else strncpy(full_path + dst_path_len, source_path, strlen(source_path));
        printf("caffeine: deploying single file: '%s'\n", source_path);
        return deploy_single_file(source_path, full_path);
    } else if (S_ISDIR(st.st_mode)) {
        LOG_INFO("Deploying directory contents: %s", source_path);
        return deploy_directory(source_path, target_dir);
    } else {
        LOG_ERROR("Deploy source '%s' is not a file or directory.", source_path);
        return -1;
    }
}