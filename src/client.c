/*
 * 普通客户端 — 模拟真实用户操作。
 *
 * 支持两种模式：
 *   1. 命令行模式：register/login 一次完成并返回结果
 *   2. 交互模式：登录后持续接收消息，send/logout/quit 命令
 *
 * 通信机制：
 *   - 请求通过公共 FIFO 发送（reg_fifo / login_fifo / msg_fifo / logout_fifo）
 *   - 一次性响应通过临时 reply FIFO 接收
 *   - 登录后创建用户专用 FIFO，由独立接收线程持续监听服务器推送
 */

#include "config.h"
#include "fifo.h"
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

typedef struct {
    char fifo[CHAT_MAX_PATH];
    int running;
} recv_arg_t;

/* 构造临时 reply FIFO 名称：dir/reply_<pid>_fifo */
static void build_client_fifo(char *buf, size_t len, const char *dir,
                               const char *prefix) {
    snprintf(buf, len, "%s/%s_%d_fifo", dir, prefix, (int)getpid());
}

/* 构造用户专用 FIFO 名称：dir/user_<username>_fifo */
static int build_user_fifo(char *buf, size_t len, const char *dir,
                            const char *username) {
    int n = snprintf(buf, len, "%s/user_%s_fifo", dir, username);
    return n > 0 && (size_t)n < len ? 0 : -1;
}

/* 向服务器公共 FIFO 写入请求结构体 */
static int send_request(const char *fifo, const chat_request_t *req) {
    int fd = open(fifo, O_WRONLY);
    if (fd < 0) {
        fprintf(stderr, "打开服务器 FIFO 失败: %s (%s)\n", fifo, strerror(errno));
        return -1;
    }
    int rc = write_full(fd, req, sizeof(*req));
    close(fd);
    return rc;
}

/* 从已打开的 reply FIFO fd 读取响应，阻塞直到服务器写回 */
static int read_reply_fd(int fd, chat_response_t *resp) {
    int rc = read_full(fd, resp, sizeof(*resp));
    return rc == 0 ? 0 : -1;
}

/* 打印服务器响应，区分聊天消息和系统通知 */
static void print_response(const chat_response_t *resp) {
    char tbuf[64];
    struct tm tmv;
    localtime_r(&resp->timestamp, &tmv);
    strftime(tbuf, sizeof(tbuf), "%H:%M:%S", &tmv);
    if (resp->type == RESP_CHAT || resp->type == RESP_OFFLINE) {
        printf("[%s] %s -> %s: %s\n", tbuf, resp->sender, resp->receiver, resp->message);
    } else {
        printf("[%s] %s\n", tbuf, resp->message);
    }
    fflush(stdout);
}

/*
 * 接收线程主循环。
 * 持续读取用户专用 FIFO，服务器通过此管道推送：
 *   - 其他用户发来的聊天消息
 *   - 系统通知（用户退出、在线列表更新）
 *   - 离线消息（登录时批量推送）
 */
static void *receiver_main(void *arg) {
    recv_arg_t *ra = arg;
    int fd = open(ra->fifo, O_RDWR);
    if (fd < 0) {
        perror("open user fifo");
        return NULL;
    }
    while (ra->running) {
        chat_response_t resp;
        int rc = read_full(fd, &resp, sizeof(resp));
        if (rc == 0) print_response(&resp);
        else if (rc < 0) break;
    }
    close(fd);
    return NULL;
}

static void usage(void) {
    printf("用法:\n");
    printf("  ./bin/client register 用户名 密码\n");
    printf("  ./bin/client login 用户名 密码\n");
}

/*
 * 注册/登录的公共流程。
 *
 * 关键点：在发送请求之前，先用 O_RDWR 打开 reply FIFO。
 * 这样服务器在 open(reply_fifo, O_WRONLY) 时一定能找到 reader，
 * 消除了"客户端尚未打开读端，服务器写端打开失败"的竞态条件。
 */
static int command_with_reply(const server_config_t *cfg, request_type_t type,
                              const char *username, const char *password) {
    char reply_fifo[CHAT_MAX_PATH];
    build_client_fifo(reply_fifo, sizeof(reply_fifo), cfg->fifo_dir, "reply");
    unlink(reply_fifo);
    if (mkfifo(reply_fifo, 0600) < 0) {
        perror("mkfifo reply");
        return 1;
    }

    /*
     * 以 O_RDWR 打开 reply FIFO（不设 O_NONBLOCK，需要阻塞等待服务器响应）。
     * O_RDWR 同时作为 reader 和 writer，因此服务器 O_WRONLY open 不会因
     * 缺少 reader 而返回 ENXIO。
     */
    int reply_fd = open(reply_fifo, O_RDWR);
    if (reply_fd < 0) {
        perror("open reply fifo");
        unlink(reply_fifo);
        return 1;
    }

    chat_request_t req;
    memset(&req, 0, sizeof(req));
    req.type = type;
    req.client_pid = getpid();
    snprintf(req.username, sizeof(req.username), "%s", username);
    snprintf(req.password, sizeof(req.password), "%s", password);
    snprintf(req.reply_fifo, sizeof(req.reply_fifo), "%s", reply_fifo);

    const char *target_fifo = type == REQ_REGISTER ? cfg->reg_fifo : cfg->login_fifo;
    char user_fifo[CHAT_MAX_PATH] = "";
    recv_arg_t ra;
    pthread_t tid;
    int receiver_started = 0;

    /* 登录模式：创建用户专用 FIFO 并启动接收线程 */
    if (type == REQ_LOGIN) {
        if (build_user_fifo(user_fifo, sizeof(user_fifo), cfg->fifo_dir, username) < 0) {
            fprintf(stderr, "用户 FIFO 路径过长\n");
            close(reply_fd);
            unlink(reply_fifo);
            return 1;
        }
        unlink(user_fifo);
        if (mkfifo(user_fifo, 0600) < 0) {
            perror("mkfifo user");
            close(reply_fd);
            unlink(reply_fifo);
            return 1;
        }
        snprintf(req.user_fifo, sizeof(req.user_fifo), "%s", user_fifo);
        snprintf(ra.fifo, sizeof(ra.fifo), "%s", user_fifo);
        ra.running = 1;
        if (pthread_create(&tid, NULL, receiver_main, &ra) == 0) receiver_started = 1;
    }

    if (send_request(target_fifo, &req) < 0) {
        close(reply_fd);
        unlink(reply_fifo);
        if (user_fifo[0]) unlink(user_fifo);
        if (receiver_started) { ra.running = 0; pthread_cancel(tid); pthread_join(tid, NULL); }
        return 1;
    }

    chat_response_t resp;
    if (read_reply_fd(reply_fd, &resp) < 0) {
        fprintf(stderr, "读取服务器响应失败\n");
        close(reply_fd);
        unlink(reply_fifo);
        if (user_fifo[0]) unlink(user_fifo);
        if (receiver_started) { ra.running = 0; pthread_cancel(tid); pthread_join(tid, NULL); }
        return 1;
    }
    print_response(&resp);
    close(reply_fd);
    unlink(reply_fifo);

    /* 如果是注册或登录失败，清理并返回 */
    if (type != REQ_LOGIN || !resp.success) {
        if (user_fifo[0]) unlink(user_fifo);
        if (receiver_started) { ra.running = 0; pthread_cancel(tid); pthread_join(tid, NULL); }
        return resp.success ? 0 : 1;
    }

    /*
     * ── 交互模式主循环 ──
     * 登录成功后进入，支持 send / logout / quit 命令。
     * 每次 send 或 logout 都需要创建临时 reply FIFO 获取服务器响应。
     */
    char line[512];
    printf("输入: send 目标用户名 消息 / online / logout / quit\n");
    while (fgets(line, sizeof(line), stdin)) {
        line[strcspn(line, "\n")] = '\0';
        if (strncmp(line, "send ", 5) == 0) {
            char *target = line + 5;
            char *space = strchr(target, ' ');
            if (!space) {
                printf("格式: send 目标用户名 消息\n");
                continue;
            }
            *space = '\0';
            char *msg = space + 1;

            chat_request_t m;
            memset(&m, 0, sizeof(m));
            m.type = REQ_MSG;
            m.client_pid = getpid();
            snprintf(m.username, sizeof(m.username), "%s", username);
            snprintf(m.target, sizeof(m.target), "%s", target);
            snprintf(m.message, sizeof(m.message), "%s", msg);
            snprintf(m.reply_fifo, sizeof(m.reply_fifo), "%s", reply_fifo);

            /* 重建 reply FIFO：先删旧文件再 mkfifo + O_RDWR 打开 */
            unlink(reply_fifo);
            mkfifo(reply_fifo, 0600);
            {
                int reply_fd2 = open(reply_fifo, O_RDWR);
                if (reply_fd2 >= 0) {
                    if (send_request(cfg->msg_fifo, &m) == 0
                        && read_reply_fd(reply_fd2, &resp) == 0)
                        print_response(&resp);
                    close(reply_fd2);
                }
            }
            unlink(reply_fifo);
        } else if (strcmp(line, "online") == 0) {
            puts("登录响应中已显示在线列表，后续上下线会自动推送");
        } else if (strcmp(line, "logout") == 0 || strcmp(line, "quit") == 0) {
            chat_request_t q;
            memset(&q, 0, sizeof(q));
            q.type = REQ_LOGOUT;
            q.client_pid = getpid();
            snprintf(q.username, sizeof(q.username), "%s", username);
            snprintf(q.reply_fifo, sizeof(q.reply_fifo), "%s", reply_fifo);

            unlink(reply_fifo);
            mkfifo(reply_fifo, 0600);
            {
                int reply_fd3 = open(reply_fifo, O_RDWR);
                if (reply_fd3 >= 0) {
                    if (send_request(cfg->logout_fifo, &q) == 0
                        && read_reply_fd(reply_fd3, &resp) == 0)
                        print_response(&resp);
                    close(reply_fd3);
                }
            }
            unlink(reply_fifo);
            break;
        } else if (line[0] != '\0') {
            printf("未知命令\n");
        }
    }

    /* 退出交互模式：通知接收线程停止，清理用户 FIFO */
    ra.running = 0;
    /* 向用户 FIFO 写入一个空字节唤醒阻塞在 read 上的接收线程 */
    int wake = open(user_fifo, O_WRONLY | O_NONBLOCK);
    if (wake >= 0) close(wake);
    pthread_cancel(tid);
    pthread_join(tid, NULL);
    unlink(user_fifo);
    return 0;
}

int main(int argc, char **argv) {
    if (argc != 4) {
        usage();
        return 1;
    }
    const char *conf = getenv("CHAT_CONFIG");
    if (!conf) conf = "config/server.conf";
    server_config_t cfg;
    if (load_config(conf, &cfg) < 0) config_set_defaults(&cfg);
    if (ensure_dir(cfg.fifo_dir, 0755) < 0) {
        perror("ensure fifo dir");
        return 1;
    }
    if (strcmp(argv[1], "register") == 0)
        return command_with_reply(&cfg, REQ_REGISTER, argv[2], argv[3]);
    if (strcmp(argv[1], "login") == 0)
        return command_with_reply(&cfg, REQ_LOGIN, argv[2], argv[3]);
    usage();
    return 1;
}
