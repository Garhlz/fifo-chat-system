/*
 * 业务处理模块 — 所有请求的实际处理逻辑。
 *
 * 主线程只负责 poll 监听和分派，业务逻辑全部集中于此。
 * 四个核心处理函数对应四种请求类型，由 handle_request() 统一路由。
 *
 * 所有对 user_store 的访问都通过其 API 完成，
 * user_store 内部已用 mutex 保护共享数据（注册表、在线表、离线消息表）。
 * 本模块不直接访问 user_store 内部结构。
 */

#include "handlers.h"
#include "fifo.h"
#include "log.h"
#include "user_store.h"
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

/* 构造响应结构体的辅助函数 */
static void make_resp(chat_response_t *resp, int ok, response_type_t type,
                      const char *sender, const char *receiver, const char *msg) {
    memset(resp, 0, sizeof(*resp));
    resp->success = ok;
    resp->type = type;
    resp->timestamp = time(NULL);
    snprintf(resp->sender, sizeof(resp->sender), "%s", sender ? sender : "server");
    snprintf(resp->receiver, sizeof(resp->receiver), "%s", receiver ? receiver : "");
    snprintf(resp->message, sizeof(resp->message), "%s", msg ? msg : "");
}

/*
 * 向客户端的一次性 reply FIFO 写入响应。
 * req->reply_fifo 是客户端在发送请求前创建的临时管道。
 */
static void reply_to_request(const chat_request_t *req, int ok, const char *msg) {
    if (req->reply_fifo[0] == '\0') return;
    chat_response_t resp;
    make_resp(&resp, ok, RESP_SYSTEM, "server", req->username, msg);
    if (send_response_fifo(req->reply_fifo, &resp) < 0) {
        log_server("event=reply_failed user=%s fifo=%s", req->username, req->reply_fifo);
    }
}

/*
 * 向用户专用 FIFO 推送消息（聊天消息、离线消息、系统通知等）。
 * 用户登录时创建的 user FIFO 由接收线程持续监听。
 */
static int push_to_user_fifo(const char *fifo, const chat_response_t *resp) {
    return send_response_fifo(fifo, resp);
}

/*
 * ── 注册处理 ──
 *
 * 流程：
 *   1. user_register() 检查用户名唯一性和空值；
 *   2. 注册成功通过 reply FIFO 返回；
 *   3. 无论成败均写入 server.log。
 *
 * is_bot 字段由客户端在请求中设置，机器人复用同一套注册逻辑。
 */
static void handle_register(const chat_request_t *req) {
    char err[128] = "";
    if (user_register(req->username, req->password, req->is_bot, err, sizeof(err)) == 0) {
        reply_to_request(req, 1, "注册成功");
        log_server("event=%s_register_success user=%s",
                   req->is_bot ? "bot" : "user", req->username);
    } else {
        reply_to_request(req, 0, err);
        log_server("event=register_failed user=%s reason=%s", req->username, err);
    }
}

/*
 * 推送离线消息 — 用户登录成功后调用。
 *
 * 流程：
 *   1. user_peek_pending() 取出该用户的待推送消息（不修改状态）；
 *   2. 逐条写入用户专用 FIFO，消息中附带原始发送时间；
 *   3. 全部推送成功后才调用 user_mark_pending_sent() 标记为已发送；
 *   4. 如果中途有推送失败，保留 pending 状态，下次登录可重试。
 */
static void push_pending_messages(const chat_request_t *req) {
    chat_message_t msgs[64];
    int n = user_peek_pending(req->username, msgs, 64);
    int fail = 0;
    for (int i = 0; i < n; i++) {
        chat_response_t resp;
        char text[CHAT_MAX_TEXT];
        struct tm tmv;
        char tbuf[64];
        localtime_r(&msgs[i].timestamp, &tmv);
        strftime(tbuf, sizeof(tbuf), "%Y-%m-%d %H:%M:%S", &tmv);
        snprintf(text, sizeof(text), "[离线消息 %s] %s", tbuf, msgs[i].message);
        make_resp(&resp, 1, RESP_OFFLINE, msgs[i].sender, req->username, text);
        if (push_to_user_fifo(req->user_fifo, &resp) == 0) {
            log_server("event=offline_sent sender=%s receiver=%s state=sent",
                       msgs[i].sender, req->username);
        } else {
            fail = 1;
            log_server("event=offline_push_failed sender=%s receiver=%s",
                       msgs[i].sender, req->username);
        }
    }
    if (!fail && n > 0) user_mark_pending_sent(req->username);
}

/*
 * ── 登录处理 ──
 *
 * 流程：
 *   1. 校验用户名和密码；
 *   2. 保存用户专用 FIFO 路径到在线表；
 *   3. 构造在线用户列表返回给客户端（含总人数和用户名）；
 *   4. 主动推送该用户的离线消息；
 *   5. 写入登录日志。
 *
 * 注意：机器人登录走同一流程，is_bot 已在注册时记录。
 */
static void handle_login(const chat_request_t *req) {
    int is_bot = 0;
    if (user_check_password(req->username, req->password, &is_bot) < 0) {
        reply_to_request(req, 0, "登录失败：用户不存在或密码错误");
        log_server("event=login_failed user=%s reason=bad_credentials", req->username);
        return;
    }
    if (req->user_fifo[0] == '\0') {
        reply_to_request(req, 0, "登录失败：缺少用户专用 FIFO");
        log_server("event=login_failed user=%s reason=missing_user_fifo", req->username);
        return;
    }
    user_set_online(req->username, req->user_fifo);
    char online[CHAT_MAX_TEXT];
    user_online_list(online, sizeof(online));
    reply_to_request(req, 1, online);
    log_server("event=%s_login_success user=%s fifo=%s",
               is_bot ? "bot" : "user", req->username, req->user_fifo);
    push_pending_messages(req);
}

/*
 * ── 消息处理 ──
 *
 * 三种情况：
 *   1. 接收者在线   → 写入接收者 user FIFO，记录 msg_sent 日志；
 *   2. 接收者是离线机器人 → 丢弃消息并记录日志（机器人用户名随机生成，
 *      下次可能不再存在，长期保存无意义）；
 *   3. 接收者是离线普通用户 → 保存为离线消息，状态 pending。
 *
 * 操作 user_store 时只读取 FIFO 路径，写 FIFO 在释放锁之后进行，
 * 避免在持有锁的同时进行可能阻塞的 I/O。
 */
static void handle_msg(const chat_request_t *req) {
    if (!user_is_online(req->username)) {
        reply_to_request(req, 0, "发送失败：发送者未登录");
        log_server("event=msg_failed sender=%s receiver=%s reason=sender_offline",
                   req->username, req->target);
        return;
    }
    if (!user_exists(req->target)) {
        reply_to_request(req, 0, "发送失败：接收者不存在");
        log_server("event=msg_failed sender=%s receiver=%s reason=receiver_missing",
                   req->username, req->target);
        return;
    }
    time_t now = time(NULL);
    char target_fifo[CHAT_MAX_PATH];

    /* 接收者在线：立即转发 */
    if (user_get_fifo(req->target, target_fifo, sizeof(target_fifo)) == 0) {
        chat_response_t resp;
        make_resp(&resp, 1, RESP_CHAT, req->username, req->target, req->message);
        if (push_to_user_fifo(target_fifo, &resp) == 0) {
            reply_to_request(req, 1, "消息已发送");
            log_server("event=%s_msg_sent sender=%s receiver=%s state=sent",
                       req->is_bot ? "bot" : "user", req->username, req->target);
        } else {
            reply_to_request(req, 0, "发送失败：写入接收者 FIFO 失败");
            log_server("event=msg_failed sender=%s receiver=%s reason=fifo_write_failed",
                       req->username, req->target);
        }
        return;
    }

    /* 接收者是离线机器人：丢弃消息 */
    if (user_is_bot(req->target)) {
        reply_to_request(req, 1, "接收者是离线机器人，消息已丢弃");
        log_server("event=msg_discarded_bot_offline sender=%s receiver=%s state=discarded",
                   req->username, req->target);
        return;
    }

    /* 接收者是离线普通用户：保存为离线消息 */
    if (user_save_offline(req->username, req->target, req->message, now) == 0) {
        reply_to_request(req, 1, "接收者离线，消息已保存");
        log_server("event=offline_pending sender=%s receiver=%s state=pending",
                   req->username, req->target);
    } else {
        reply_to_request(req, 0, "离线消息表已满");
        log_server("event=offline_failed sender=%s receiver=%s reason=store_full",
                   req->username, req->target);
    }
}

/*
 * 向除退出用户外的所有在线用户广播退出通知和新的在线列表。
 * 先复制在线用户 FIFO 路径到本地数组，释放锁后再写入，
 * 避免在持有 user_store 锁时进行阻塞 I/O。
 */
static void broadcast_logout(const char *username) {
    online_peer_t peers[CHAT_MAX_USERS];
    int n = user_get_online_peers(peers, CHAT_MAX_USERS, username);
    char online[CHAT_MAX_TEXT];
    user_online_list(online, sizeof(online));
    for (int i = 0; i < n; i++) {
        chat_response_t resp;
        char text[CHAT_MAX_TEXT];
        snprintf(text, sizeof(text), "%s 已退出; %.450s", username, online);
        make_resp(&resp, 1, RESP_SYSTEM, "server", peers[i].username, text);
        push_to_user_fifo(peers[i].fifo_path, &resp);
    }
}

/*
 * ── 退出处理 ──
 *
 * 流程：
 *   1. 将用户从在线表移除；
 *   2. 向剩余在线用户广播退出通知和更新后的在线列表；
 *   3. 写入退出日志。
 *
 * 机器人退出同样走此流程。
 */
static void handle_logout(const chat_request_t *req) {
    int was_bot = user_is_bot(req->username);
    if (user_set_offline(req->username) == 0) {
        reply_to_request(req, 1, "退出成功");
        broadcast_logout(req->username);
        log_server("event=%s_logout user=%s", was_bot ? "bot" : "user", req->username);
    } else {
        reply_to_request(req, 0, "退出失败：用户不存在");
        log_server("event=logout_failed user=%s", req->username);
    }
}

/* 请求路由：根据 type 字段分派到对应处理函数 */
void handle_request(const chat_request_t *req) {
    switch (req->type) {
    case REQ_REGISTER: handle_register(req); break;
    case REQ_LOGIN:    handle_login(req);    break;
    case REQ_MSG:      handle_msg(req);      break;
    case REQ_LOGOUT:   handle_logout(req);   break;
    default:
        log_server("event=unknown_request type=%d user=%s", req->type, req->username);
        break;
    }
}
