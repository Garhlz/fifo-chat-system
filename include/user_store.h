/*
 * 用户存储模块接口。
 *
 * 统一维护注册用户表、在线用户表和离线消息表。
 * 所有函数内部已使用 mutex 保护共享数据，调用者无需额外加锁。
 */

#ifndef USER_STORE_H
#define USER_STORE_H

#include <stddef.h>
#include "chat.h"

/* 在线用户摘要信息，用于退出广播 */
typedef struct {
    char username[CHAT_MAX_USERNAME];
    char fifo_path[CHAT_MAX_PATH];
    int is_bot;
} online_peer_t;

void user_store_init(void);

/* 注册用户，is_bot 标记机器人身份 */
int user_register(const char *username, const char *password, int is_bot,
                  char *err, size_t err_len);

/* 验证密码，成功时通过 is_bot 返回用户类型 */
int user_check_password(const char *username, const char *password, int *is_bot);

/* 设置在线/离线状态，记录用户专用 FIFO 路径 */
int user_set_online(const char *username, const char *fifo_path);
int user_set_offline(const char *username);

int user_exists(const char *username);
int user_is_online(const char *username);
int user_is_bot(const char *username);

/* 获取在线用户的 FIFO 路径 */
int user_get_fifo(const char *username, char *fifo_path, size_t len);

/* 构造在线用户列表字符串 */
int user_online_list(char *buf, size_t len);

/* 获取在线用户列表（排除指定用户），用于退出广播 */
int user_get_online_peers(online_peer_t *peers, int max, const char *exclude);

/* 保存离线消息，状态 pending */
int user_save_offline(const char *sender, const char *receiver,
                      const char *message, time_t ts);

/* 查看待推送消息（不修改状态） */
int user_peek_pending(const char *receiver, chat_message_t *out, int max);

/* 将接收者的 pending 消息标记为 sent */
int user_mark_pending_sent(const char *receiver);

/* 获取在线机器人列表 */
int user_get_online_bots(char names[][CHAT_MAX_USERNAME], int max);

#endif
