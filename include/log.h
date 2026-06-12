#ifndef LOG_H
#define LOG_H

#include "config.h"

int log_init(const server_config_t *cfg);
void log_close(void);
void log_server(const char *fmt, ...);
void log_thread(const char *fmt, ...);

#endif
