/*
 * 服务器主程序入口。
 *
 * 主线程只负责 poll() 多路复用监听 4 个公共 FIFO，收到请求后立即分派给线程池。
 * 业务逻辑全部放在 handlers.c，主线程不直接处理任何请求。
 *
 * 初始化流程：
 *   读取配置 → daemonize(可选) → 初始化日志 → 创建公共 FIFO
 *   → 打开 FIFO → 初始化用户存储 → 创建线程池 → poll 主循环
 */

#include "config.h"
#include "daemon.h"
#include "fifo.h"
#include "log.h"
#include "thread_pool.h"
#include "user_store.h"
#include <errno.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static volatile sig_atomic_t running = 1;

static void on_signal(int sig) {
    (void)sig;
    running = 0;
}

/*
 * 从指定 fd 中循环读出请求结构体，并分派给线程池。
 * 返回 0 表示本次无更多数据（对面可能还没写完），-1 表示读取出错。
 */
static int read_and_dispatch(int fd, thread_pool_t *pool, request_type_t expected) {
    for (;;) {
        chat_request_t req;
        int rc = read_full(fd, &req, sizeof(req));
        if (rc == 1) return 0;      /* 非阻塞读，数据未就绪 */
        if (rc < 0) {
            log_server("event=request_read_failed expected=%s", request_type_name(expected));
            return -1;
        }
        /* 记录请求类型不匹配但不丢弃，仍分派给 handler 根据实际 type 处理 */
        if (req.type != expected) {
            log_server("event=request_type_mismatch expected=%s actual=%s user=%s",
                       request_type_name(expected), request_type_name(req.type), req.username);
        }
        log_server("event=request_arrived type=%s user=%s pid=%d",
                   request_type_name(req.type), req.username, (int)req.client_pid);
        thread_pool_dispatch(pool, &req);
    }
}

int main(int argc, char **argv) {
    const char *conf = "config/server.conf";
    int foreground = 0;
    int print_config = 0;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--foreground") == 0) foreground = 1;
        else if (strcmp(argv[i], "--print-config") == 0) print_config = 1;
        else conf = argv[i];
    }

    /* ── 初始化阶段 ── */
    server_config_t cfg;
    if (load_config(conf, &cfg) < 0) {
        config_set_defaults(&cfg);
        fprintf(stderr, "warning: cannot read %s, using defaults\n", conf);
    }
    if (print_config) {
        config_print(&cfg);
        return 0;
    }

    /*
     * daemonize 必须在日志初始化之前调用。
     * daemonize 后会关闭 stdout/stderr 并将工作目录切换到 /，
     * 因此配置路径必须在 daemonize 前解析完毕。
     */
    if (!foreground && daemonize_process() < 0) {
        perror("daemonize");
        return 1;
    }
    if (log_init(&cfg) < 0) return 1;
    log_server("event=server_start name=%s config=%s", cfg.server_name, conf);
    log_server("event=config_loaded fifo_dir=%s log_dir=%s pool=%d",
               cfg.fifo_dir, cfg.log_dir, cfg.pool_size);

    /* 创建 4 个公共 FIFO，如果已存在且是管道类型则复用 */
    if (setup_public_fifos(&cfg) < 0) {
        log_server("event=fifo_setup_failed");
        return 1;
    }
    log_server("event=fifo_ready reg=%s login=%s msg=%s logout=%s",
               cfg.reg_fifo, cfg.login_fifo, cfg.msg_fifo, cfg.logout_fifo);

    /*
     * 以 O_RDWR | O_NONBLOCK 打开每个公共 FIFO。
     * O_RDWR 确保即使所有客户端都关闭了写端，服务器读端也不会收到 EOF；
     * O_NONBLOCK 让 read() 在没有数据时返回 EAGAIN 而不是阻塞，保证 poll 循环不被一个 fd 卡住。
     */
    int fds_raw[4];
    fds_raw[0] = open_public_fifo(cfg.reg_fifo);
    fds_raw[1] = open_public_fifo(cfg.login_fifo);
    fds_raw[2] = open_public_fifo(cfg.msg_fifo);
    fds_raw[3] = open_public_fifo(cfg.logout_fifo);
    for (int i = 0; i < 4; i++) {
        if (fds_raw[i] < 0) {
            log_server("event=fifo_open_failed index=%d errno=%d", i, errno);
            return 1;
        }
    }

    user_store_init();

    /* 一次性创建固定数量的 worker 线程，后续请求只做分派不做创建 */
    thread_pool_t pool;
    if (thread_pool_init(&pool, cfg.pool_size) < 0) {
        log_server("event=thread_pool_failed size=%d", cfg.pool_size);
        return 1;
    }
    log_server("event=thread_pool_ready size=%d", cfg.pool_size);

    signal(SIGTERM, on_signal);
    signal(SIGINT, on_signal);

    /*
     * ── poll 主循环 ──
     *
     * 4 个 fd 分别对应 4 个公共 FIFO：
     *   [0] ynp_reg_fifo    — 注册请求
     *   [1] ynp_login_fifo  — 登录请求
     *   [2] ynp_msg_fifo    — 发消息请求
     *   [3] ynp_logout_fifo — 退出请求
     *
     * poll() 阻塞等待，任一 FIFO 可读时返回；
     * SIGTERM/SIGINT 通过 EINTR 跳出循环实现优雅退出。
     */
    struct pollfd pfds[4] = {
        { .fd = fds_raw[0], .events = POLLIN },
        { .fd = fds_raw[1], .events = POLLIN },
        { .fd = fds_raw[2], .events = POLLIN },
        { .fd = fds_raw[3], .events = POLLIN },
    };
    request_type_t types[4] = {REQ_REGISTER, REQ_LOGIN, REQ_MSG, REQ_LOGOUT};
    while (running) {
        int n = poll(pfds, 4, -1);
        if (n < 0) {
            if (errno == EINTR) continue;   /* 信号唤醒，重新检查 running 标志 */
            log_server("event=poll_failed errno=%d", errno);
            break;
        }
        for (int i = 0; i < 4; i++) {
            if (pfds[i].revents & POLLIN)
                read_and_dispatch(pfds[i].fd, &pool, types[i]);
            if (pfds[i].revents & (POLLERR | POLLHUP | POLLNVAL)) {
                log_server("event=poll_fifo_error index=%d revents=%d", i, pfds[i].revents);
            }
        }
    }

    /* ── 退出清理 ── */
    log_server("event=server_stop");
    thread_pool_destroy(&pool);
    for (int i = 0; i < 4; i++) close(fds_raw[i]);
    log_close();
    return 0;
}
