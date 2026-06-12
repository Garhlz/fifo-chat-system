# 基于 FIFO 的即时聊天系统

系统编程课程实验项目，使用 C 语言、POSIX 命名管道（FIFO）和 pthread 库实现的即时聊天系统。

## 项目结构

```
├── Makefile
├── include/           # 公共头文件
│   ├── chat.h         # 通信协议定义
│   ├── config.h       # 配置文件读取
│   ├── daemon.h       # 守护进程
│   ├── fifo.h         # FIFO 创建与通信
│   ├── handlers.h     # 业务处理
│   ├── log.h          # 日志模块
│   ├── thread_pool.h  # 线程池
│   └── user_store.h   # 用户数据管理
├── src/               # 源文件
│   ├── server.c       # 服务器主程序
│   ├── client.c       # 普通客户端
│   ├── bot_manager.c  # 机器人管理器
│   ├── chat.c         # 协议工具函数
│   ├── config.c       # 配置读取实现
│   ├── daemon.c       # 守护进程实现
│   ├── fifo.c         # FIFO 实现
│   ├── handlers.c     # 业务处理实现
│   ├── log.c          # 日志实现
│   ├── thread_pool.c  # 线程池实现
│   └── user_store.c   # 用户存储实现
├── config/
│   └── server.conf    # 服务器配置文件
└── docs/
    ├── requirement.md # 实验需求文档
    └── TODO.md        # 分阶段实现任务清单
```

## 编译

```bash
make          # 编译所有程序
make clean    # 清理编译产物
```

编译生成：

- `bin/chatserver` — 服务器程序
- `bin/client` — 普通客户端
- `bin/bot_manager` — 聊天机器人管理器

## 运行

### 启动服务器

```bash
./bin/chatserver config/server.conf
```

服务器默认以守护进程模式运行。调试时可用 `--foreground` 参数保持前台运行：

```bash
./bin/chatserver --foreground config/server.conf
```

### 客户端注册

```bash
./bin/client register <用户名> <密码>
```

### 客户端登录

```bash
./bin/client login <用户名> <密码>
```

登录后进入交互模式，支持以下命令：

- `send <目标用户> <消息>` — 发送消息
- `online` — 查看在线用户
- `logout` — 退出登录
- `quit` — 退出程序

### 机器人管理

```bash
./bin/bot_manager add <数量>   # 增加机器人
./bin/bot_manager del <数量>   # 减少机器人
```

## 技术要点

- **通信协议**: 固定长度 C 结构体，通过 FIFO 传输
- **多路复用**: 服务器主线程使用 `poll()` 同时监听 4 个公共 FIFO
- **线程池**: 固定大小（默认 100），LIFO 空闲栈调度
- **守护进程**: 标准双 fork daemonize
- **线程安全**: 用户存储和日志模块均使用 mutex 保护
- **离线消息**: 支持离线消息保存和重新登录后推送
- **机器人**: 动态增减，自动回复普通用户消息
