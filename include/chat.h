/*
 * 公共通信协议定义。
 *
 * 本系统所有进程间通信使用固定长度 C 结构体，通过命名管道（FIFO）传输。
 * 选择结构体而非字符串协议的原因：
 *   1. FIFO 按字节流传输，固定长度结构体天然支持 read/write 完整一条消息；
 *   2. 不需要字符串解析，减少代码复杂度；
 *   3. 结构体不含指针，跨进程传输安全。
 */
#ifndef CHAT_H
#define CHAT_H

#include <sys/types.h>
#include <time.h>

/* 固定上限，使用栈上数组避免动态分配跨进程传递 */
#define CHAT_MAX_USERNAME 32
#define CHAT_MAX_PASSWORD 32
#define CHAT_MAX_MESSAGE 256
#define CHAT_MAX_PATH 256
#define CHAT_MAX_TEXT 512
#define CHAT_MAX_USERS 256
#define CHAT_MAX_OFFLINE_MESSAGES 1024

/* 四种请求类型，客户端通过不同公共 FIFO 发送到服务器 */
typedef enum {
    REQ_REGISTER = 1,
    REQ_LOGIN = 2,
    REQ_MSG = 3,
    REQ_LOGOUT = 4
} request_type_t;

/* worker 线程的两种状态 */
typedef enum {
    WORKER_IDLE = 0,
    WORKER_BUSY = 1
} worker_state_t;

/* 离线消息的生命周期状态 */
typedef enum {
    MSG_PENDING = 0,   /* 接收者离线，消息待推送 */
    MSG_SENT = 1,      /* 接收者已收到 */
    MSG_DISCARDED = 2  /* 发送给离线机器人，直接丢弃 */
} message_state_t;

/* 响应类型区分：系统通知、聊天消息、在线列表、离线消息 */
typedef enum {
    RESP_SYSTEM = 1,
    RESP_CHAT = 2,
    RESP_ONLINE = 3,
    RESP_OFFLINE = 4
} response_type_t;

/*
 * 请求结构体 — 客户端写入公共 FIFO，服务器读出。
 * 全部使用定长 char 数组，不含指针，可安全跨进程 memcpy。
 */
typedef struct {
    request_type_t type;
    char username[CHAT_MAX_USERNAME];
    char password[CHAT_MAX_PASSWORD];
    char target[CHAT_MAX_USERNAME];          /* 消息接收者 */
    char message[CHAT_MAX_MESSAGE];
    char reply_fifo[CHAT_MAX_PATH];          /* 一次性回复管道路径 */
    char user_fifo[CHAT_MAX_PATH];           /* 用户专用管道，用于接收消息 */
    pid_t client_pid;
    int is_bot;                              /* 机器人标记，服务器据此调整离线消息策略 */
} chat_request_t;

/*
 * 响应结构体 — 服务器写入客户端管道。
 * type 字段帮助客户端区分：系统通知 / 聊天消息 / 在线列表 / 离线消息。
 */
typedef struct {
    int success;
    response_type_t type;
    time_t timestamp;                        /* 发送时间，离线消息推送时保留原始时间 */
    char sender[CHAT_MAX_USERNAME];
    char receiver[CHAT_MAX_USERNAME];
    char message[CHAT_MAX_TEXT];
} chat_response_t;

/*
 * 离线消息存储结构。
 * 服务器内存中维护一个定长数组，接收者登录时批量推送。
 */
typedef struct {
    char sender[CHAT_MAX_USERNAME];
    char receiver[CHAT_MAX_USERNAME];
    char message[CHAT_MAX_MESSAGE];
    time_t timestamp;
    message_state_t state;
} chat_message_t;

const char *request_type_name(request_type_t type);
const char *message_state_name(message_state_t state);

#endif
