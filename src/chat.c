#include "chat.h"

const char *request_type_name(request_type_t type) {
    switch (type) {
    case REQ_REGISTER: return "register";
    case REQ_LOGIN: return "login";
    case REQ_MSG: return "message";
    case REQ_LOGOUT: return "logout";
    default: return "unknown";
    }
}

const char *message_state_name(message_state_t state) {
    switch (state) {
    case MSG_PENDING: return "pending";
    case MSG_SENT: return "sent";
    case MSG_DISCARDED: return "discarded";
    default: return "unknown";
    }
}
