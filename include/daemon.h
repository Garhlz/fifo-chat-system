/*
 * 守护进程接口。
 *
 * daemonize_process() 将当前进程转为后台守护进程：
 *   双 fork + setsid + 忽略 SIGHUP + 重定向标准 IO 到 /dev/null。
 * 调用后进程失去控制终端，只能通过日志文件输出。
 */

#ifndef DAEMON_H
#define DAEMON_H

int daemonize_process(void);

#endif
