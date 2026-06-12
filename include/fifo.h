/*
 * FIFO（命名管道）操作接口。
 *
 * 提供管道创建、打开、读写和响应发送的统一封装。
 * write_full 和 read_full 处理短写/短读和 EINTR/EAGAIN，
 * 确保结构体能被完整地通过 FIFO 传输。
 */

#ifndef FIFO_H
#define FIFO_H

#include <stddef.h>
#include "chat.h"
#include "config.h"

/* 逐级创建目录（类似 mkdir -p） */
int ensure_dir(const char *path, int mode);

/* 确保路径对应一个 FIFO，不存在则创建，存在则验证类型 */
int ensure_fifo(const char *path, int mode);

/* 创建 4 个公共 FIFO：注册、登录、消息、退出 */
int setup_public_fifos(const server_config_t *cfg);

/* 以 O_RDWR | O_NONBLOCK 打开公共 FIFO */
int open_public_fifo(const char *path);

/* 完整写入 len 字节（防短写、EINTR） */
int write_full(int fd, const void *buf, size_t len);

/* 完整读取 len 字节（防短读、EINTR、EAGAIN） */
int read_full(int fd, void *buf, size_t len);

/* 向指定管道写入响应结构体（O_WRONLY | O_NONBLOCK） */
int send_response_fifo(const char *path, const chat_response_t *resp);

#endif
