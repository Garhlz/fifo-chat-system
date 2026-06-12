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

#define BOT_STATE_FILE "/tmp/fifo_chat_bots.txt"
#define MAX_BOTS 128

typedef struct {
    server_config_t cfg;
    char username[CHAT_MAX_USERNAME];
    char password[CHAT_MAX_PASSWORD];
    char user_fifo[CHAT_MAX_PATH];
    int running;
} bot_t;

static int join_fifo_name(char *buf, size_t len, const char *dir, const char *name) {
    int n = snprintf(buf, len, "%s/%s", dir, name);
    return n > 0 && (size_t)n < len ? 0 : -1;
}

static int send_request(const char *fifo, const chat_request_t *req) {
    int fd = open(fifo, O_WRONLY);
    if (fd < 0) return -1;
    int rc = write_full(fd, req, sizeof(*req));
    close(fd);
    return rc;
}

static int request_reply(const server_config_t *cfg, const char *fifo, chat_request_t *req, chat_response_t *resp) {
    char reply_fifo[CHAT_MAX_PATH];
    char name[96];
    snprintf(name, sizeof(name), "bot_reply_%d_%ld_fifo", (int)getpid(), random() % 100000);
    if (join_fifo_name(reply_fifo, sizeof(reply_fifo), cfg->fifo_dir, name) < 0) return -1;
    unlink(reply_fifo);
    if (mkfifo(reply_fifo, 0600) < 0) return -1;
    int reply_fd = open(reply_fifo, O_RDWR);
    if (reply_fd < 0) { unlink(reply_fifo); return -1; }
    snprintf(req->reply_fifo, sizeof(req->reply_fifo), "%s", reply_fifo);
    int rc = send_request(fifo, req);
    if (rc == 0) {
        rc = read_full(reply_fd, resp, sizeof(*resp)) == 0 ? 0 : -1;
    }
    close(reply_fd);
    unlink(reply_fifo);
    return rc;
}

static void *bot_recv_main(void *arg) {
    bot_t *bot = arg;
    int fd = open(bot->user_fifo, O_RDWR);
    if (fd < 0) return NULL;
    while (bot->running) {
        chat_response_t resp;
        int rc = read_full(fd, &resp, sizeof(resp));
        if (rc != 0) continue;
        if (resp.type != RESP_CHAT || strncmp(resp.sender, "bot_", 4) == 0) continue;
        chat_request_t req;
        memset(&req, 0, sizeof(req));
        req.type = REQ_MSG;
        req.client_pid = getpid();
        req.is_bot = 1;
        snprintf(req.username, sizeof(req.username), "%s", bot->username);
        snprintf(req.target, sizeof(req.target), "%s", resp.sender);
        snprintf(req.message, sizeof(req.message), "幸会，%s，很高兴认识您", resp.sender);
        send_request(bot->cfg.msg_fifo, &req);
    }
    close(fd);
    return NULL;
}

static void append_bot_state(const bot_t *bot) {
    FILE *fp = fopen(BOT_STATE_FILE, "a");
    if (!fp) return;
    fprintf(fp, "%s %s %s\n", bot->username, bot->password, bot->user_fifo);
    fclose(fp);
}

static int load_bot_state(bot_t *bots, int max, const server_config_t *cfg) {
    FILE *fp = fopen(BOT_STATE_FILE, "r");
    if (!fp) return 0;
    int n = 0;
    while (n < max && fscanf(fp, "%31s %31s %255s", bots[n].username, bots[n].password, bots[n].user_fifo) == 3) {
        bots[n].cfg = *cfg;
        bots[n].running = 0;
        n++;
    }
    fclose(fp);
    return n;
}

static void save_bot_state(bot_t *bots, int n) {
    FILE *fp = fopen(BOT_STATE_FILE, "w");
    if (!fp) return;
    for (int i = 0; i < n; i++) fprintf(fp, "%s %s %s\n", bots[i].username, bots[i].password, bots[i].user_fifo);
    fclose(fp);
}

static int add_bots(const server_config_t *cfg, int count) {
    bot_t *live = calloc((size_t)count, sizeof(bot_t));
    if (!live) return 1;
    int live_count = 0;
    for (int i = 0; i < count; i++) {
        bot_t bot;
        memset(&bot, 0, sizeof(bot));
        bot.cfg = *cfg;
        snprintf(bot.username, sizeof(bot.username), "bot_ynp_%ld_%ld", (long)time(NULL), random() % 100000);
        snprintf(bot.password, sizeof(bot.password), "pw%ld", random() % 1000000);
        char fifo_name[96];
        snprintf(fifo_name, sizeof(fifo_name), "user_%s_fifo", bot.username);
        if (join_fifo_name(bot.user_fifo, sizeof(bot.user_fifo), cfg->fifo_dir, fifo_name) < 0) {
            printf("机器人 FIFO 路径过长: %s\n", bot.username);
            continue;
        }
        unlink(bot.user_fifo);
        if (mkfifo(bot.user_fifo, 0600) < 0) {
            perror("mkfifo bot");
            continue;
        }
        chat_request_t req;
        chat_response_t resp;
        memset(&req, 0, sizeof(req));
        req.type = REQ_REGISTER;
        req.client_pid = getpid();
        req.is_bot = 1;
        snprintf(req.username, sizeof(req.username), "%s", bot.username);
        snprintf(req.password, sizeof(req.password), "%s", bot.password);
        if (request_reply(cfg, cfg->reg_fifo, &req, &resp) < 0 || !resp.success) {
            printf("机器人注册失败: %s\n", bot.username);
            unlink(bot.user_fifo);
            continue;
        }
        memset(&req, 0, sizeof(req));
        req.type = REQ_LOGIN;
        req.client_pid = getpid();
        req.is_bot = 1;
        snprintf(req.username, sizeof(req.username), "%s", bot.username);
        snprintf(req.password, sizeof(req.password), "%s", bot.password);
        snprintf(req.user_fifo, sizeof(req.user_fifo), "%s", bot.user_fifo);
        if (request_reply(cfg, cfg->login_fifo, &req, &resp) < 0 || !resp.success) {
            printf("机器人登录失败: %s\n", bot.username);
            unlink(bot.user_fifo);
            continue;
        }
        bot.running = 1;
        live[live_count] = bot;
        pthread_t tid;
        pthread_create(&tid, NULL, bot_recv_main, &live[live_count]);
        pthread_detach(tid);
        append_bot_state(&live[live_count]);
        printf("已增加机器人: %s\n", live[live_count].username);
        live_count++;
    }
    printf("提示：本进程保持运行时机器人可自动回复，Ctrl+C 后机器人仍在线但不会自动回复。\n");
    pause();
    free(live);
    return 0;
}

static int del_bots(const server_config_t *cfg, int count) {
    bot_t bots[MAX_BOTS];
    int n = load_bot_state(bots, MAX_BOTS, cfg);
    if (n == 0) {
        printf("没有可删除的机器人记录\n");
        return 1;
    }
    if (count > n) count = n;
    for (int i = n - 1; i > 0; i--) {
        int j = random() % (i + 1);
        bot_t tmp = bots[i];
        bots[i] = bots[j];
        bots[j] = tmp;
    }
    for (int i = 0; i < count; i++) {
        chat_request_t req;
        memset(&req, 0, sizeof(req));
        req.type = REQ_LOGOUT;
        req.client_pid = getpid();
        req.is_bot = 1;
        snprintf(req.username, sizeof(req.username), "%s", bots[i].username);
        send_request(cfg->logout_fifo, &req);
        unlink(bots[i].user_fifo);
        printf("已减少机器人: %s\n", bots[i].username);
    }
    save_bot_state(bots + count, n - count);
    return 0;
}

int main(int argc, char **argv) {
    if (argc != 3) {
        printf("用法: ./bin/bot_manager add x | ./bin/bot_manager del x\n");
        return 1;
    }
    int count = atoi(argv[2]);
    if (count <= 0) {
        printf("数量必须为正整数\n");
        return 1;
    }
    srandom((unsigned)time(NULL) ^ (unsigned)getpid());
    const char *conf = getenv("CHAT_CONFIG");
    if (!conf) conf = "config/server.conf";
    server_config_t cfg;
    if (load_config(conf, &cfg) < 0) config_set_defaults(&cfg);
    ensure_dir(cfg.fifo_dir, 0755);
    if (strcmp(argv[1], "add") == 0) return add_bots(&cfg, count);
    if (strcmp(argv[1], "del") == 0) return del_bots(&cfg, count);
    printf("未知操作: %s\n", argv[1]);
    return 1;
}
