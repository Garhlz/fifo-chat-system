#ifndef CONFIG_H
#define CONFIG_H

#include "chat.h"

typedef struct {
    char server_name[64];
    char fifo_dir[CHAT_MAX_PATH];
    char reg_fifo[CHAT_MAX_PATH];
    char login_fifo[CHAT_MAX_PATH];
    char msg_fifo[CHAT_MAX_PATH];
    char logout_fifo[CHAT_MAX_PATH];
    char log_dir[CHAT_MAX_PATH];
    char server_log[CHAT_MAX_PATH];
    char thread_log[CHAT_MAX_PATH];
    int pool_size;
} server_config_t;

void config_set_defaults(server_config_t *cfg);
int load_config(const char *path, server_config_t *cfg);
void config_print(const server_config_t *cfg);

#endif
