#ifndef USER_STORE_H
#define USER_STORE_H

#include <stddef.h>
#include "chat.h"

typedef struct {
    char username[CHAT_MAX_USERNAME];
    char fifo_path[CHAT_MAX_PATH];
    int is_bot;
} online_peer_t;

void user_store_init(void);
int user_register(const char *username, const char *password, int is_bot, char *err, size_t err_len);
int user_check_password(const char *username, const char *password, int *is_bot);
int user_set_online(const char *username, const char *fifo_path);
int user_set_offline(const char *username);
int user_exists(const char *username);
int user_is_online(const char *username);
int user_is_bot(const char *username);
int user_get_fifo(const char *username, char *fifo_path, size_t len);
int user_online_list(char *buf, size_t len);
int user_get_online_peers(online_peer_t *peers, int max, const char *exclude);
int user_save_offline(const char *sender, const char *receiver, const char *message, time_t ts);
int user_take_pending(const char *receiver, chat_message_t *out, int max);
int user_get_online_bots(char names[][CHAT_MAX_USERNAME], int max);

#endif
