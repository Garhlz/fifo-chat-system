/*
 * FIFO（命名管道）创建与通信模块。
 *
 * 服务器通过 4 个公共 FIFO 接收请求（注册/登录/消息/退出），
 * 客户端登录时创建用户专用 FIFO 接收服务器推送的消息和通知。
 *
 * write_full / read_full 封装了防短写/短读的循环读写，
 * 因为 FIFO 可能一次 write/read 不完整条结构体。
 */

#include "fifo.h"
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

/* 逐级创建目录，类似 mkdir -p */
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

/*
 * 确保路径对应一个 FIFO 文件。
 * 如果文件已存在：用 stat + S_ISFIFO 判断是否为管道，
 *   不是管道则拒绝（避免当普通文件用导致行为异常）。
 * 如果文件不存在：用 mkfifo 创建。
 */
int ensure_fifo(const char *path, int mode) {
    struct stat st;
    if (stat(path, &st) == 0) {
        return S_ISFIFO(st.st_mode) ? 0 : -1;
    }
    if (mkfifo(path, mode) < 0 && errno != EEXIST) return -1;
    return 0;
}

/* 创建 4 个公共 FIFO：注册、登录、消息、退出 */
int setup_public_fifos(const server_config_t *cfg) {
    if (ensure_dir(cfg->fifo_dir, 0755) < 0) return -1;
    if (ensure_fifo(cfg->reg_fifo, 0666) < 0) return -1;
    if (ensure_fifo(cfg->login_fifo, 0666) < 0) return -1;
    if (ensure_fifo(cfg->msg_fifo, 0666) < 0) return -1;
    if (ensure_fifo(cfg->logout_fifo, 0666) < 0) return -1;
    return 0;
}

/*
 * 以非阻塞读写模式打开公共 FIFO。
 *
 * O_RDWR：同时作为读写端，确保即使所有客户端关闭写端，
 *   服务器读端也不会收到 EOF（没有写端的 FIFO 读会返回 0）。
 * O_NONBLOCK：读操作不阻塞，配合 poll 使用。
 */
int open_public_fifo(const char *path) {
    return open(path, O_RDWR | O_NONBLOCK);
}

/*
 * 完整写入 len 字节，处理短写和信号中断。
 * FIFO 写可能只写入部分数据，需要循环直到全部写完。
 */
int write_full(int fd, const void *buf, size_t len) {
    const char *p = buf;
    size_t off = 0;
    while (off < len) {
        ssize_t n = write(fd, p + off, len - off);
        if (n < 0) {
            if (errno == EINTR) continue;      /* 信号中断，重试 */
            return -1;
        }
        if (n == 0) return -1;
        off += (size_t)n;
    }
    return 0;
}

/*
 * 完整读取 len 字节，处理短读、信号中断和非阻塞模式下数据未就绪。
 * 返回值：0 成功，1 表示当前无数据可读（EAGAIN），-1 出错。
 */
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
        if (n == 0) return off == 0 ? 1 : -1;  /* EOF */
        off += (size_t)n;
    }
    return 0;
}

/*
 * 向指定管道写入一个响应结构体。
 * 以 O_WRONLY | O_NONBLOCK 打开，写完后立即关闭。
 * 客户端的 reply FIFO 由客户端保持 O_RDWR 打开作为 reader，
 * 因此服务器的 O_WRONLY open 不会因无 reader 而失败。
 */
int send_response_fifo(const char *path, const chat_response_t *resp) {
    int fd = open(path, O_WRONLY | O_NONBLOCK);
    if (fd < 0) return -1;
    int rc = write_full(fd, resp, sizeof(*resp));
    close(fd);
    return rc;
}
