#include "config.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void copy_str(char *dst, size_t len, const char *src) {
    snprintf(dst, len, "%s", src ? src : "");
}

static void trim(char *s) {
    char *p = s;
    while (isspace((unsigned char)*p)) p++;
    if (p != s) memmove(s, p, strlen(p) + 1);
    size_t n = strlen(s);
    while (n > 0 && isspace((unsigned char)s[n - 1])) s[--n] = '\0';
}

static void expand_home(char *dst, size_t len, const char *src) {
    if (!src || src[0] != '~') {
        copy_str(dst, len, src);
        return;
    }
    const char *home = getenv("HOME");
    if (!home) home = ".";
    if (src[1] == '/' || src[1] == '\0') {
        snprintf(dst, len, "%s%s", home, src + 1);
    } else {
        copy_str(dst, len, src);
    }
}

static void join_path(char *dst, size_t len, const char *dir, const char *name) {
    if (name[0] == '/') {
        copy_str(dst, len, name);
        return;
    }
    size_t n = strlen(dir);
    snprintf(dst, len, "%s%s%s", dir, (n > 0 && dir[n - 1] == '/') ? "" : "/", name);
}

void config_set_defaults(server_config_t *cfg) {
    memset(cfg, 0, sizeof(*cfg));
    copy_str(cfg->server_name, sizeof(cfg->server_name), "chatserver_ynp_1.0");
    expand_home(cfg->fifo_dir, sizeof(cfg->fifo_dir), "~/Server/fifo");
    join_path(cfg->reg_fifo, sizeof(cfg->reg_fifo), cfg->fifo_dir, "ynp_reg_fifo");
    join_path(cfg->login_fifo, sizeof(cfg->login_fifo), cfg->fifo_dir, "ynp_login_fifo");
    join_path(cfg->msg_fifo, sizeof(cfg->msg_fifo), cfg->fifo_dir, "ynp_msg_fifo");
    join_path(cfg->logout_fifo, sizeof(cfg->logout_fifo), cfg->fifo_dir, "ynp_logout_fifo");
    expand_home(cfg->log_dir, sizeof(cfg->log_dir), "~/log/chat-logs/server");
    join_path(cfg->server_log, sizeof(cfg->server_log), cfg->log_dir, "server.log");
    join_path(cfg->thread_log, sizeof(cfg->thread_log), cfg->log_dir, "threads.log");
    cfg->pool_size = 100;
}

int load_config(const char *path, server_config_t *cfg) {
    char fifo_dir[CHAT_MAX_PATH] = "";
    char log_dir[CHAT_MAX_PATH] = "";
    char reg_name[CHAT_MAX_PATH] = "ynp_reg_fifo";
    char login_name[CHAT_MAX_PATH] = "ynp_login_fifo";
    char msg_name[CHAT_MAX_PATH] = "ynp_msg_fifo";
    char logout_name[CHAT_MAX_PATH] = "ynp_logout_fifo";
    char server_log_name[CHAT_MAX_PATH] = "server.log";
    char thread_log_name[CHAT_MAX_PATH] = "threads.log";

    config_set_defaults(cfg);
    copy_str(fifo_dir, sizeof(fifo_dir), cfg->fifo_dir);
    copy_str(log_dir, sizeof(log_dir), cfg->log_dir);

    FILE *fp = fopen(path, "r");
    if (!fp) return -1;

    char line[512];
    while (fgets(line, sizeof(line), fp)) {
        trim(line);
        if (line[0] == '\0' || line[0] == '#') continue;
        char *eq = strchr(line, '=');
        if (!eq) continue;
        *eq = '\0';
        char *key = line;
        char *val = eq + 1;
        trim(key);
        trim(val);

        if (strcmp(key, "SERVER_NAME") == 0) copy_str(cfg->server_name, sizeof(cfg->server_name), val);
        else if (strcmp(key, "FIFO_DIR") == 0) expand_home(fifo_dir, sizeof(fifo_dir), val);
        else if (strcmp(key, "REG_FIFO") == 0) copy_str(reg_name, sizeof(reg_name), val);
        else if (strcmp(key, "LOGIN_FIFO") == 0) copy_str(login_name, sizeof(login_name), val);
        else if (strcmp(key, "MSG_FIFO") == 0) copy_str(msg_name, sizeof(msg_name), val);
        else if (strcmp(key, "LOGOUT_FIFO") == 0) copy_str(logout_name, sizeof(logout_name), val);
        else if (strcmp(key, "LOG_DIR") == 0) expand_home(log_dir, sizeof(log_dir), val);
        else if (strcmp(key, "SERVER_LOG") == 0) copy_str(server_log_name, sizeof(server_log_name), val);
        else if (strcmp(key, "THREAD_LOG") == 0) copy_str(thread_log_name, sizeof(thread_log_name), val);
        else if (strcmp(key, "POOLSIZE") == 0) {
            int n = atoi(val);
            if (n > 0 && n <= 512) cfg->pool_size = n;
        }
    }
    fclose(fp);

    copy_str(cfg->fifo_dir, sizeof(cfg->fifo_dir), fifo_dir);
    copy_str(cfg->log_dir, sizeof(cfg->log_dir), log_dir);
    join_path(cfg->reg_fifo, sizeof(cfg->reg_fifo), fifo_dir, reg_name);
    join_path(cfg->login_fifo, sizeof(cfg->login_fifo), fifo_dir, login_name);
    join_path(cfg->msg_fifo, sizeof(cfg->msg_fifo), fifo_dir, msg_name);
    join_path(cfg->logout_fifo, sizeof(cfg->logout_fifo), fifo_dir, logout_name);
    join_path(cfg->server_log, sizeof(cfg->server_log), log_dir, server_log_name);
    join_path(cfg->thread_log, sizeof(cfg->thread_log), log_dir, thread_log_name);
    return 0;
}

void config_print(const server_config_t *cfg) {
    printf("SERVER_NAME=%s\nFIFO_DIR=%s\nREG_FIFO=%s\nLOGIN_FIFO=%s\nMSG_FIFO=%s\nLOGOUT_FIFO=%s\n",
           cfg->server_name, cfg->fifo_dir, cfg->reg_fifo, cfg->login_fifo, cfg->msg_fifo, cfg->logout_fifo);
    printf("LOG_DIR=%s\nSERVER_LOG=%s\nTHREAD_LOG=%s\nPOOLSIZE=%d\n",
           cfg->log_dir, cfg->server_log, cfg->thread_log, cfg->pool_size);
}
