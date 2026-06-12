#ifndef FIFO_H
#define FIFO_H

#include <stddef.h>
#include "chat.h"
#include "config.h"

int ensure_dir(const char *path, int mode);
int ensure_fifo(const char *path, int mode);
int setup_public_fifos(const server_config_t *cfg);
int open_public_fifo(const char *path);
int write_full(int fd, const void *buf, size_t len);
int read_full(int fd, void *buf, size_t len);
int send_response_fifo(const char *path, const chat_response_t *resp);

#endif
