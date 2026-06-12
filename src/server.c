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

static int read_and_dispatch(int fd, thread_pool_t *pool, request_type_t expected) {
    for (;;) {
        chat_request_t req;
        int rc = read_full(fd, &req, sizeof(req));
        if (rc == 1) return 0;
        if (rc < 0) {
            log_server("event=request_read_failed expected=%s", request_type_name(expected));
            return -1;
        }
        if (req.type != expected) {
            log_server("event=request_type_mismatch expected=%s actual=%s user=%s",
                       request_type_name(expected), request_type_name(req.type), req.username);
        }
        log_server("event=request_arrived type=%s user=%s pid=%d", request_type_name(req.type), req.username, (int)req.client_pid);
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

    server_config_t cfg;
    if (load_config(conf, &cfg) < 0) {
        config_set_defaults(&cfg);
        fprintf(stderr, "warning: cannot read %s, using defaults\n", conf);
    }
    if (print_config) {
        config_print(&cfg);
        return 0;
    }
    if (!foreground && daemonize_process() < 0) {
        perror("daemonize");
        return 1;
    }
    if (log_init(&cfg) < 0) return 1;
    log_server("event=server_start name=%s config=%s", cfg.server_name, conf);
    log_server("event=config_loaded fifo_dir=%s log_dir=%s pool=%d", cfg.fifo_dir, cfg.log_dir, cfg.pool_size);

    if (setup_public_fifos(&cfg) < 0) {
        log_server("event=fifo_setup_failed");
        return 1;
    }
    log_server("event=fifo_ready reg=%s login=%s msg=%s logout=%s", cfg.reg_fifo, cfg.login_fifo, cfg.msg_fifo, cfg.logout_fifo);

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
    thread_pool_t pool;
    if (thread_pool_init(&pool, cfg.pool_size) < 0) {
        log_server("event=thread_pool_failed size=%d", cfg.pool_size);
        return 1;
    }
    log_server("event=thread_pool_ready size=%d", cfg.pool_size);

    signal(SIGTERM, on_signal);
    signal(SIGINT, on_signal);

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
            if (errno == EINTR) continue;
            log_server("event=poll_failed errno=%d", errno);
            break;
        }
        for (int i = 0; i < 4; i++) {
            if (pfds[i].revents & POLLIN) read_and_dispatch(pfds[i].fd, &pool, types[i]);
            if (pfds[i].revents & (POLLERR | POLLHUP | POLLNVAL)) {
                log_server("event=poll_fifo_error index=%d revents=%d", i, pfds[i].revents);
            }
        }
    }

    log_server("event=server_stop");
    thread_pool_destroy(&pool);
    for (int i = 0; i < 4; i++) close(fds_raw[i]);
    log_close();
    return 0;
}
