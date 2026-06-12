/*
 * 日志模块接口。
 *
 * log_server() — 记录业务事件到 server.log
 * log_thread() — 记录线程调度事件到 threads.log
 *
 * 每条日志自动添加时间戳，写入操作使用 mutex 保证多线程安全。
 */

#ifndef LOG_H
#define LOG_H

#include "config.h"

/* 初始化日志：创建日志目录和日志文件 */
int log_init(const server_config_t *cfg);

void log_close(void);

/* 业务日志：注册、登录、消息、退出等事件 */
void log_server(const char *fmt, ...);

/* 线程池日志：worker 创建、分派、回收、状态变化 */
void log_thread(const char *fmt, ...);

#endif
