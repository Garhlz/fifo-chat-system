/*
 * 守护进程 daemonize 实现。
 *
 * 标准双 fork 流程：
 *   1. 第一次 fork() → 父进程 _exit(0)，子进程继续；
 *      这一步让子进程不再是会话组长。
 *   2. setsid() → 子进程创建新会话，成为新会话组长，脱离控制终端。
 *   3. 第二次 fork() → 新会话组长 _exit(0)，孙进程继续；
 *      确保进程不再是会话组长，无法重新获得控制终端。
 *   4. 忽略 SIGHUP（控制终端关闭信号）。
 *   5. umask(027) → 限制默认文件权限。
 *   6. chdir("/") → 工作目录改为根目录，避免占用挂载点。
 *   7. 关闭 stdin/stdout/stderr 并重定向到 /dev/null。
 */

#include "daemon.h"
#include <fcntl.h>
#include <signal.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>

int daemonize_process(void) {
    /* 第一次 fork */
    pid_t pid = fork();
    if (pid < 0) return -1;
    if (pid > 0) _exit(0);          /* 父进程退出 */

    /* 创建新会话，脱离控制终端 */
    if (setsid() < 0) return -1;

    signal(SIGHUP, SIG_IGN);        /* 忽略终端关闭信号 */

    /* 第二次 fork，确保进程不是会话组长 */
    pid = fork();
    if (pid < 0) return -1;
    if (pid > 0) _exit(0);          /* 会话组长退出 */

    umask(027);

    if (chdir("/") < 0) return -1;  /* 工作目录切换到根 */

    /* 关闭标准 I/O，重定向到 /dev/null */
    close(STDIN_FILENO);
    close(STDOUT_FILENO);
    close(STDERR_FILENO);
    int fd = open("/dev/null", O_RDWR);
    if (fd >= 0) {
        dup2(fd, STDIN_FILENO);
        dup2(fd, STDOUT_FILENO);
        dup2(fd, STDERR_FILENO);
        if (fd > STDERR_FILENO) close(fd);
    }
    return 0;
}
