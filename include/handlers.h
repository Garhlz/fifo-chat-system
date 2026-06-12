/*
 * 业务处理模块接口。
 *
 * handle_request() 是工作线程处理请求的统一入口。
 * 根据请求类型分派到注册、登录、消息、退出四个处理函数。
 */

#ifndef HANDLERS_H
#define HANDLERS_H

#include "chat.h"

void handle_request(const chat_request_t *req);

#endif
