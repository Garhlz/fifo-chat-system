#include "log.h"
#include "fifo.h"
#include <fcntl.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

static int server_fd = -1;
static int thread_fd = -1;
static pthread_mutex_t log_mutex = PTHREAD_MUTEX_INITIALIZER;

static void write_log(int fd, const char *fmt, va_list ap) {
    if (fd < 0) return;
    char msg[1024];
    char line[1280];
    char tbuf[64];
    time_t now = time(NULL);
    struct tm tmv;
    localtime_r(&now, &tmv);
    strftime(tbuf, sizeof(tbuf), "%Y-%m-%d %H:%M:%S", &tmv);
    vsnprintf(msg, sizeof(msg), fmt, ap);
    snprintf(line, sizeof(line), "[%s] %s\n", tbuf, msg);
    pthread_mutex_lock(&log_mutex);
    (void)write(fd, line, strlen(line));
    pthread_mutex_unlock(&log_mutex);
}

int log_init(const server_config_t *cfg) {
    if (ensure_dir(cfg->log_dir, 0755) < 0) return -1;
    server_fd = open(cfg->server_log, O_CREAT | O_APPEND | O_WRONLY, 0600);
    thread_fd = open(cfg->thread_log, O_CREAT | O_APPEND | O_WRONLY, 0600);
    return (server_fd < 0 || thread_fd < 0) ? -1 : 0;
}

void log_close(void) {
    if (server_fd >= 0) close(server_fd);
    if (thread_fd >= 0) close(thread_fd);
    server_fd = -1;
    thread_fd = -1;
}

void log_server(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    write_log(server_fd, fmt, ap);
    va_end(ap);
}

void log_thread(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    write_log(thread_fd, fmt, ap);
    va_end(ap);
}
