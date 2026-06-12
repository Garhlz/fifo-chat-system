#include "fifo.h"
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

int ensure_dir(const char *path, int mode) {
    char tmp[CHAT_MAX_PATH];
    snprintf(tmp, sizeof(tmp), "%s", path);
    for (char *p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            if (mkdir(tmp, mode) < 0 && errno != EEXIST) return -1;
            *p = '/';
        }
    }
    if (mkdir(tmp, mode) < 0 && errno != EEXIST) return -1;
    return 0;
}

int ensure_fifo(const char *path, int mode) {
    struct stat st;
    if (stat(path, &st) == 0) {
        return S_ISFIFO(st.st_mode) ? 0 : -1;
    }
    if (mkfifo(path, mode) < 0 && errno != EEXIST) return -1;
    return 0;
}

int setup_public_fifos(const server_config_t *cfg) {
    if (ensure_dir(cfg->fifo_dir, 0755) < 0) return -1;
    if (ensure_fifo(cfg->reg_fifo, 0666) < 0) return -1;
    if (ensure_fifo(cfg->login_fifo, 0666) < 0) return -1;
    if (ensure_fifo(cfg->msg_fifo, 0666) < 0) return -1;
    if (ensure_fifo(cfg->logout_fifo, 0666) < 0) return -1;
    return 0;
}

int open_public_fifo(const char *path) {
    return open(path, O_RDWR | O_NONBLOCK);
}

int write_full(int fd, const void *buf, size_t len) {
    const char *p = buf;
    size_t off = 0;
    while (off < len) {
        ssize_t n = write(fd, p + off, len - off);
        if (n < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        if (n == 0) return -1;
        off += (size_t)n;
    }
    return 0;
}

int read_full(int fd, void *buf, size_t len) {
    char *p = buf;
    size_t off = 0;
    while (off < len) {
        ssize_t n = read(fd, p + off, len - off);
        if (n < 0) {
            if (errno == EINTR) continue;
            if (errno == EAGAIN || errno == EWOULDBLOCK) return off == 0 ? 1 : -1;
            return -1;
        }
        if (n == 0) return off == 0 ? 1 : -1;
        off += (size_t)n;
    }
    return 0;
}

int send_response_fifo(const char *path, const chat_response_t *resp) {
    int fd = open(path, O_WRONLY | O_NONBLOCK);
    if (fd < 0) return -1;
    int rc = write_full(fd, resp, sizeof(*resp));
    close(fd);
    return rc;
}
