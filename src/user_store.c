/*
 * 用户存储模块 — 统一维护注册用户表、在线用户表和离线消息表。
 *
 * 所有共享数据结构使用单一的 pthread_mutex_t (store_mutex) 保护。
 * 外部调用者不需要关心内部锁——每个 API 函数内部自己管理加锁/解锁。
 *
 * 数据结构选择：
 *   - 使用定长数组而非动态链表，降低复杂度、便于实验展示；
 *   - 上限为 CHAT_MAX_USERS(256) 和 CHAT_MAX_OFFLINE_MESSAGES(1024)，
 *     对课程实验规模完全足够。
 */

#include "user_store.h"
#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    char username[CHAT_MAX_USERNAME];
    char password[CHAT_MAX_PASSWORD];
    char fifo_path[CHAT_MAX_PATH];   /* 用户专用管道完整路径，登录时设置 */
    int registered;
    int online;
    int is_bot;                      /* 机器人标记，影响离线消息策略 */
} user_t;

static user_t users[CHAT_MAX_USERS];
static chat_message_t offline_msgs[CHAT_MAX_OFFLINE_MESSAGES];

/* 单一互斥锁保护所有用户数据和离线消息表 */
static pthread_mutex_t store_mutex = PTHREAD_MUTEX_INITIALIZER;

/* 内部函数：在已持有 store_mutex 的前提下查找用户索引 */
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

/*
 * 注册用户。
 * 加锁检查用户名唯一性，找到空闲槽位后写入。
 * is_bot 标记在注册时记录，后续登录/发消息时据此调整行为。
 */
int user_register(const char *username, const char *password, int is_bot,
                  char *err, size_t err_len) {
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
        if (!users[i].registered) { slot = i; break; }
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

/* 验证密码，成功时同步返回用户的 is_bot 属性 */
int user_check_password(const char *username, const char *password, int *is_bot) {
    pthread_mutex_lock(&store_mutex);
    int idx = find_user_locked(username);
    int ok = idx >= 0 && strcmp(users[idx].password, password) == 0;
    if (ok && is_bot) *is_bot = users[idx].is_bot;
    pthread_mutex_unlock(&store_mutex);
    return ok ? 0 : -1;
}

/* 设置用户为在线状态，记录其用户专用 FIFO 路径 */
int user_set_online(const char *username, const char *fifo_path) {
    pthread_mutex_lock(&store_mutex);
    int idx = find_user_locked(username);
    if (idx < 0) { pthread_mutex_unlock(&store_mutex); return -1; }
    users[idx].online = 1;
    snprintf(users[idx].fifo_path, sizeof(users[idx].fifo_path), "%s", fifo_path);
    pthread_mutex_unlock(&store_mutex);
    return 0;
}

/* 设置用户离线，清空其 FIFO 路径 */
int user_set_offline(const char *username) {
    pthread_mutex_lock(&store_mutex);
    int idx = find_user_locked(username);
    if (idx < 0) { pthread_mutex_unlock(&store_mutex); return -1; }
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

/* 获取在线用户的 FIFO 路径，用于消息转发 */
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

/*
 * 安全的文本追加函数。
 * used 到达 len 后不再追加，避免 buf+used 越界。
 * 用于构造在线用户列表等变长文本。
 */
static void append_text(char *buf, size_t len, size_t *used,
                        const char *fmt, ...) {
    if (*used >= len) return;
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf + *used, len - *used, fmt, ap);
    va_end(ap);
    if (n > 0) {
        if ((size_t)n >= len - *used) *used = len;
        else *used += (size_t)n;
    }
}

/*
 * 构造在线用户列表字符串。
 * 格式："当前在线 N 人; 在线用户: user1, user2, ..."
 * 加锁遍历 registered+online 用户表并拼接。
 */
int user_online_list(char *buf, size_t len) {
    pthread_mutex_lock(&store_mutex);
    int count = 0;
    for (int i = 0; i < CHAT_MAX_USERS; i++) {
        if (users[i].registered && users[i].online) count++;
    }
    size_t used = 0;
    append_text(buf, len, &used, "当前在线 %d 人; 在线用户: ", count);
    int first = 1;
    for (int i = 0; i < CHAT_MAX_USERS; i++) {
        if (!users[i].registered || !users[i].online) continue;
        if (!first) append_text(buf, len, &used, ", ");
        first = 0;
        append_text(buf, len, &used, "%s", users[i].username);
    }
    pthread_mutex_unlock(&store_mutex);
    return count;
}

/*
 * 获取在线用户列表（排除指定用户）。
 * 用于退出广播：向除退出用户外的所有人发送通知。
 */
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

/*
 * 保存离线消息。
 * 查找空闲槽位（receiver 为空或已 sent 的条目可复用），
 * 写入发送者、接收者、内容和时间戳，状态设为 pending。
 */
int user_save_offline(const char *sender, const char *receiver,
                      const char *message, time_t ts) {
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

/*
 * 查看待推送的离线消息——只读不修改状态。
 * 推送成功后再由调用者调用 user_mark_pending_sent 标记。
 */
int user_peek_pending(const char *receiver, chat_message_t *out, int max) {
    pthread_mutex_lock(&store_mutex);
    int n = 0;
    for (int i = 0; i < CHAT_MAX_OFFLINE_MESSAGES && n < max; i++) {
        if (offline_msgs[i].state == MSG_PENDING
            && strcmp(offline_msgs[i].receiver, receiver) == 0) {
            out[n++] = offline_msgs[i];
        }
    }
    pthread_mutex_unlock(&store_mutex);
    return n;
}

/* 将指定接收者的所有 pending 消息标记为已发送 */
int user_mark_pending_sent(const char *receiver) {
    pthread_mutex_lock(&store_mutex);
    for (int i = 0; i < CHAT_MAX_OFFLINE_MESSAGES; i++) {
        if (offline_msgs[i].state == MSG_PENDING
            && strcmp(offline_msgs[i].receiver, receiver) == 0) {
            offline_msgs[i].state = MSG_SENT;
        }
    }
    pthread_mutex_unlock(&store_mutex);
    return 0;
}

/* 获取当前在线机器人列表，用于 bot_manager del 选择退出对象 */
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
