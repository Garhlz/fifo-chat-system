/*
 * 配置文件读取模块。
 *
 * 配置文件格式为简单的 KEY=VALUE，支持：
 *   - 忽略空行和 # 注释行
 *   - ~ 展开为用户 HOME 目录
 *   - 路径自动拼接（处理尾部斜杠）
 *   - 缺失配置使用合理默认值
 */

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

/* 设置默认配置（在无法读取配置文件时使用） */
void config_set_defaults(server_config_t *cfg);

/* 读取配置文件，解析后填充 server_config_t */
int load_config(const char *path, server_config_t *cfg);

/* 打印当前配置（--print-config 参数时使用） */
void config_print(const server_config_t *cfg);

#endif
