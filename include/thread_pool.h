/*
 * 固定大小线程池。
 *
 * 设计要点：
 *   1. 启动时一次性创建 pool_size 个 worker 线程，请求到达时分派而不是新建；
 *   2. 空闲线程用 LIFO 栈管理，刚回收的 worker 优先下一次被分派；
 *   3. 每个 worker 有独立的 mutex + condition variable，避免惊群效应；
 *   4. 分派和回收操作记录到 threads.log，便于验证 LIFO 调度策略。
 */

#ifndef THREAD_POOL_H
#define THREAD_POOL_H

#include <pthread.h>
#include "chat.h"

typedef struct thread_pool thread_pool_t;

typedef struct {
    int id;
    pthread_t tid;
    worker_state_t state;
    int has_task;                    /* 是否有待处理任务 */
    chat_request_t task;             /* 任务请求副本，分派时拷贝 */
    pthread_mutex_t mutex;           /* 保护本 worker 的 has_task / task */
    pthread_cond_t cond;             /* 唤醒正在等待任务的 worker */
    thread_pool_t *pool;
} worker_t;

struct thread_pool {
    worker_t *workers;               /* 固定大小 worker 数组 */
    int size;
    int *idle_stack;                 /* LIFO 空闲栈，存储空闲 worker 的 id */
    int idle_top;                    /* 栈顶指针，-1 表示空 */
    volatile int stopping;           /* 退出标志，volatile 确保 worker 线程可见 */
    pthread_mutex_t mutex;           /* 保护 idle_stack / idle_top / stopping */
    pthread_cond_t idle_cond;        /* 通知分派函数有空闲 worker */
};

/*
 * 初始化线程池。
 * 创建 size 个 worker 线程并全部标记为 IDLE 压入空闲栈。
 */
int thread_pool_init(thread_pool_t *pool, int size);

/*
 * 分派一个请求给空闲 worker。
 * 从 LIFO 空闲栈顶弹出 worker，复制请求，唤醒 worker 处理。
 * 如果没有空闲 worker 则阻塞等待。
 */
int thread_pool_dispatch(thread_pool_t *pool, const chat_request_t *req);

/* 销毁线程池，通知所有 worker 退出并 join。 */
void thread_pool_destroy(thread_pool_t *pool);

#endif
