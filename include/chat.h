#ifndef CHAT_H
#define CHAT_H

#include <sys/types.h>
#include <time.h>

#define CHAT_MAX_USERNAME 32
#define CHAT_MAX_PASSWORD 32
#define CHAT_MAX_MESSAGE 256
#define CHAT_MAX_PATH 256
#define CHAT_MAX_TEXT 512
#define CHAT_MAX_USERS 256
#define CHAT_MAX_OFFLINE_MESSAGES 1024

typedef enum {
    REQ_REGISTER = 1,
    REQ_LOGIN = 2,
    REQ_MSG = 3,
    REQ_LOGOUT = 4
} request_type_t;

typedef enum {
    WORKER_IDLE = 0,
    WORKER_BUSY = 1
} worker_state_t;

typedef enum {
    MSG_PENDING = 0,
    MSG_SENT = 1,
    MSG_DISCARDED = 2
} message_state_t;

typedef enum {
    RESP_SYSTEM = 1,
    RESP_CHAT = 2,
    RESP_ONLINE = 3,
    RESP_OFFLINE = 4
} response_type_t;

typedef struct {
    request_type_t type;
    char username[CHAT_MAX_USERNAME];
    char password[CHAT_MAX_PASSWORD];
    char target[CHAT_MAX_USERNAME];
    char message[CHAT_MAX_MESSAGE];
    char reply_fifo[CHAT_MAX_PATH];
    char user_fifo[CHAT_MAX_PATH];
    pid_t client_pid;
    int is_bot;
} chat_request_t;

typedef struct {
    int success;
    response_type_t type;
    time_t timestamp;
    char sender[CHAT_MAX_USERNAME];
    char receiver[CHAT_MAX_USERNAME];
    char message[CHAT_MAX_TEXT];
} chat_response_t;

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
