/*
 * 日志模块 — 线程安全的服务器和线程池日志。
 *
 * 为什么需要 mutex：
 *   多个 worker 线程可能同时处理请求并写日志，
 *   如果不加锁，两个线程的日志行可能交错（如时间戳和消息内容拼在一起）。
 *
 * 日志分为两个文件：
 *   server.log  — 业务事件（注册、登录、发消息、退出等）
 *   threads.log — 线程池调度事件（创建、分派、回收、状态变化）
 */

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

/* 全局日志互斥锁，确保每次 write 是原子的 */
static pthread_mutex_t log_mutex = PTHREAD_MUTEX_INITIALIZER;

/* 内部写日志函数：格式化时间戳 + 消息，加锁后 write */
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

/*
 * 初始化日志：创建日志目录和两个日志文件。
 * server.log 权限设为 0600（仅当前用户可读写）。
 */
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

/* 业务日志：注册、登录、消息、退出等事件 */
void log_server(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    write_log(server_fd, fmt, ap);
    va_end(ap);
}

/* 线程池日志：worker 创建、分派、回收、状态变化 */
void log_thread(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    write_log(thread_fd, fmt, ap);
    va_end(ap);
}
