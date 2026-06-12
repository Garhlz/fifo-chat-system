# CLAUDE.md

This repository follows the conventions documented in [AGENTS.md](AGENTS.md). Read that file before making any changes.

## 项目概述

基于 POSIX FIFO 命名管道的即时聊天系统，使用 C 语言 + pthread 库实现。系统由三个独立程序组成：`chatserver`（服务器）、`client`（客户端）、`bot_manager`（机器人管理器）。

## 构建

```bash
make          # 编译所有目标
make clean    # 清理 bin/
```

编译选项：`-Wall -Wextra -g -Iinclude -lpthread`。产物在 `bin/` 目录。

## 模块说明

| 头文件 | 源文件 | 职责 |
|--------|--------|------|
| `chat.h` | `chat.c` | 请求/响应结构体，枚举类型，工具函数 |
| `config.h` | `config.c` | 读取 `config/server.conf`，`~` 展开，路径拼接 |
| `fifo.h` | `fifo.c` | 目录/管道创建，`write_full`/`read_full`，响应发送 |
| `log.h` | `log.c` | 线程安全日志，`log_server()` 业务日志，`log_thread()` 调度日志 |
| `daemon.h` | `daemon.c` | 双 fork daemonize |
| `thread_pool.h` | `thread_pool.c` | 固定大小线程池，LIFO 空闲栈 |
| `user_store.h` | `user_store.c` | 注册表、在线表、离线消息表（mutex 保护）|
| `handlers.h` | `handlers.c` | 注册/登录/消息/退出四种请求处理 |
| - | `server.c` | 服务器入口：初始化 → poll() → 分派 |
| - | `client.c` | 客户端：register/login 命令 + 交互模式 |
| - | `bot_manager.c` | 机器人：add/del 命令 + 自动回复 |

## 通信协议

固定长度结构体定义在 `include/chat.h`：

- `chat_request_t` — 请求（类型、用户名、密码、目标、消息、reply FIFO、user FIFO、pid、is_bot）
- `chat_response_t` — 响应（成功标志、类型、时间戳、发送者、接收者、消息文本）

4 种请求类型：`REQ_REGISTER`、`REQ_LOGIN`、`REQ_MSG`、`REQ_LOGOUT`。

## 代码风格

- C99 标准，POSIX 兼容
- 错误处理：返回 -1 表示失败，0 表示成功
- 共享数据必须加 mutex
- 日志使用 `log_server()` / `log_thread()` 统一接口
- 不要在结构体中放指针（FIFO 传输安全）
