#include "user_store.h"
#include <pthread.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    char username[CHAT_MAX_USERNAME];
    char password[CHAT_MAX_PASSWORD];
    char fifo_path[CHAT_MAX_PATH];
    int registered;
    int online;
    int is_bot;
} user_t;

static user_t users[CHAT_MAX_USERS];
static chat_message_t offline_msgs[CHAT_MAX_OFFLINE_MESSAGES];
static pthread_mutex_t store_mutex = PTHREAD_MUTEX_INITIALIZER;

static int find_user_locked(const char *username) {
    for (int i = 0; i < CHAT_MAX_USERS; i++) {
        if (users[i].registered && strcmp(users[i].username, username) == 0) return i;
    }
    return -1;
}

void user_store_init(void) {
    pthread_mutex_lock(&store_mutex);
    memset(users, 0, sizeof(users));
    memset(offline_msgs, 0, sizeof(offline_msgs));
    pthread_mutex_unlock(&store_mutex);
}

int user_register(const char *username, const char *password, int is_bot, char *err, size_t err_len) {
    if (!username || username[0] == '\0') {
        snprintf(err, err_len, "用户名为空");
        return -1;
    }
    if (!password || password[0] == '\0') {
        snprintf(err, err_len, "密码为空");
        return -1;
    }
    pthread_mutex_lock(&store_mutex);
    if (find_user_locked(username) >= 0) {
        pthread_mutex_unlock(&store_mutex);
        snprintf(err, err_len, "用户名已存在");
        return -1;
    }
    int slot = -1;
    for (int i = 0; i < CHAT_MAX_USERS; i++) {
        if (!users[i].registered) {
            slot = i;
            break;
        }
    }
    if (slot < 0) {
        pthread_mutex_unlock(&store_mutex);
        snprintf(err, err_len, "用户表已满");
        return -1;
    }
    snprintf(users[slot].username, sizeof(users[slot].username), "%s", username);
    snprintf(users[slot].password, sizeof(users[slot].password), "%s", password);
    users[slot].registered = 1;
    users[slot].online = 0;
    users[slot].is_bot = is_bot ? 1 : 0;
    users[slot].fifo_path[0] = '\0';
    pthread_mutex_unlock(&store_mutex);
    return 0;
}

int user_check_password(const char *username, const char *password, int *is_bot) {
    pthread_mutex_lock(&store_mutex);
    int idx = find_user_locked(username);
    int ok = idx >= 0 && strcmp(users[idx].password, password) == 0;
    if (ok && is_bot) *is_bot = users[idx].is_bot;
    pthread_mutex_unlock(&store_mutex);
    return ok ? 0 : -1;
}

int user_set_online(const char *username, const char *fifo_path) {
    pthread_mutex_lock(&store_mutex);
    int idx = find_user_locked(username);
    if (idx < 0) {
        pthread_mutex_unlock(&store_mutex);
        return -1;
    }
    users[idx].online = 1;
    snprintf(users[idx].fifo_path, sizeof(users[idx].fifo_path), "%s", fifo_path);
    pthread_mutex_unlock(&store_mutex);
    return 0;
}

int user_set_offline(const char *username) {
    pthread_mutex_lock(&store_mutex);
    int idx = find_user_locked(username);
    if (idx < 0) {
        pthread_mutex_unlock(&store_mutex);
        return -1;
    }
    users[idx].online = 0;
    users[idx].fifo_path[0] = '\0';
    pthread_mutex_unlock(&store_mutex);
    return 0;
}

int user_exists(const char *username) {
    pthread_mutex_lock(&store_mutex);
    int ok = find_user_locked(username) >= 0;
    pthread_mutex_unlock(&store_mutex);
    return ok;
}

int user_is_online(const char *username) {
    pthread_mutex_lock(&store_mutex);
    int idx = find_user_locked(username);
    int ok = idx >= 0 && users[idx].online;
    pthread_mutex_unlock(&store_mutex);
    return ok;
}

int user_is_bot(const char *username) {
    pthread_mutex_lock(&store_mutex);
    int idx = find_user_locked(username);
    int ok = idx >= 0 && users[idx].is_bot;
    pthread_mutex_unlock(&store_mutex);
    return ok;
}

int user_get_fifo(const char *username, char *fifo_path, size_t len) {
    pthread_mutex_lock(&store_mutex);
    int idx = find_user_locked(username);
    if (idx < 0 || !users[idx].online) {
        pthread_mutex_unlock(&store_mutex);
        return -1;
    }
    snprintf(fifo_path, len, "%s", users[idx].fifo_path);
    pthread_mutex_unlock(&store_mutex);
    return 0;
}

int user_online_list(char *buf, size_t len) {
    pthread_mutex_lock(&store_mutex);
    int count = 0;
    size_t used = 0;
    used += snprintf(buf + used, len > used ? len - used : 0, "在线用户: ");
    for (int i = 0; i < CHAT_MAX_USERS; i++) {
        if (!users[i].registered || !users[i].online) continue;
        count++;
        used += snprintf(buf + used, len > used ? len - used : 0, "%s%s",
                         count == 1 ? "" : ", ", users[i].username);
    }
    char prefix[64];
    snprintf(prefix, sizeof(prefix), "当前在线 %d 人; ", count);
    char old[CHAT_MAX_TEXT];
    snprintf(old, sizeof(old), "%s", buf);
    snprintf(buf, len, "%s%s", prefix, old);
    pthread_mutex_unlock(&store_mutex);
    return count;
}

int user_get_online_peers(online_peer_t *peers, int max, const char *exclude) {
    pthread_mutex_lock(&store_mutex);
    int n = 0;
    for (int i = 0; i < CHAT_MAX_USERS && n < max; i++) {
        if (!users[i].registered || !users[i].online) continue;
        if (exclude && strcmp(users[i].username, exclude) == 0) continue;
        snprintf(peers[n].username, sizeof(peers[n].username), "%s", users[i].username);
        snprintf(peers[n].fifo_path, sizeof(peers[n].fifo_path), "%s", users[i].fifo_path);
        peers[n].is_bot = users[i].is_bot;
        n++;
    }
    pthread_mutex_unlock(&store_mutex);
    return n;
}

int user_save_offline(const char *sender, const char *receiver, const char *message, time_t ts) {
    pthread_mutex_lock(&store_mutex);
    for (int i = 0; i < CHAT_MAX_OFFLINE_MESSAGES; i++) {
        if (offline_msgs[i].receiver[0] == '\0' || offline_msgs[i].state == MSG_SENT) {
            snprintf(offline_msgs[i].sender, sizeof(offline_msgs[i].sender), "%s", sender);
            snprintf(offline_msgs[i].receiver, sizeof(offline_msgs[i].receiver), "%s", receiver);
            snprintf(offline_msgs[i].message, sizeof(offline_msgs[i].message), "%s", message);
            offline_msgs[i].timestamp = ts;
            offline_msgs[i].state = MSG_PENDING;
            pthread_mutex_unlock(&store_mutex);
            return 0;
        }
    }
    pthread_mutex_unlock(&store_mutex);
    return -1;
}

int user_take_pending(const char *receiver, chat_message_t *out, int max) {
    pthread_mutex_lock(&store_mutex);
    int n = 0;
    for (int i = 0; i < CHAT_MAX_OFFLINE_MESSAGES && n < max; i++) {
        if (offline_msgs[i].state == MSG_PENDING && strcmp(offline_msgs[i].receiver, receiver) == 0) {
            out[n++] = offline_msgs[i];
            offline_msgs[i].state = MSG_SENT;
        }
    }
    pthread_mutex_unlock(&store_mutex);
    return n;
}

int user_get_online_bots(char names[][CHAT_MAX_USERNAME], int max) {
    pthread_mutex_lock(&store_mutex);
    int n = 0;
    for (int i = 0; i < CHAT_MAX_USERS && n < max; i++) {
        if (users[i].registered && users[i].online && users[i].is_bot) {
            snprintf(names[n++], CHAT_MAX_USERNAME, "%s", users[i].username);
        }
    }
    pthread_mutex_unlock(&store_mutex);
    return n;
}
