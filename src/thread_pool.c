/*
 * 线程池实现 — 固定大小 + LIFO 空闲栈调度。
 *
 * 核心思想：
 *   服务器启动时预创建 pool_size 个 worker 线程，之后请求到达只做分派，
 *   不做 pthread_create。这样可以避免线程创建/销毁的开销。
 *
 * LIFO 空闲栈：
 *   空闲 worker 的 id 存放在 idle_stack 数组中，idle_top 指向栈顶。
 *   分派时从栈顶弹出（idle_top--），回收时压回栈顶（++idle_top）。
 *   这样"刚回收的 worker 倾向于下一次优先被分派"，缓存热度更高。
 */

#include "thread_pool.h"
#include "handlers.h"
#include "log.h"
#include <stdlib.h>
#include <string.h>

/*
 * 回收 worker — 将处理完任务的工作线程标记为 IDLE 并压回 LIFO 空闲栈。
 * 调用者不持有任何锁；本函数在 pool->mutex 下操作共享数据结构。
 */
static void recycle_worker(worker_t *worker) {
    thread_pool_t *pool = worker->pool;
    pthread_mutex_lock(&pool->mutex);
    worker->state = WORKER_IDLE;
    pool->idle_stack[++pool->idle_top] = worker->id;     /* LIFO 压栈 */
    log_thread("worker=%d state=IDLE recycled idle_top=%d", worker->id, pool->idle_top);
    pthread_cond_signal(&pool->idle_cond);                /* 唤醒可能在等待空闲线程的分派调用 */
    pthread_mutex_unlock(&pool->mutex);
}

/*
 * worker 线程主循环。
 * 每个 worker 启动后进入一个无限循环：
 *   1. 在 worker->cond 上阻塞等待任务（has_task 变为 1）；
 *   2. 被唤醒后检查是否为退出信号（stopping 标志）；
 *   3. 取出任务的本地副本，释放 worker->mutex 后处理任务；
 *   4. 处理完成后将自己回收到空闲栈；
 *   5. 回到步骤 1 继续等待。
 *
 * 任务处理在释放 worker->mutex 之后进行，避免长时间持锁阻塞分派。
 */
static void *worker_main(void *arg) {
    worker_t *worker = arg;
    for (;;) {
        pthread_mutex_lock(&worker->mutex);
        while (!worker->has_task && !worker->pool->stopping) {
            /*
             * pthread_cond_wait 原子性地：释放 worker->mutex → 进入等待
             * → 被 signal 唤醒 → 重新获取 worker->mutex。
             * 使用 while 而非 if 是 pthread 惯用法，防止虚假唤醒。
             */
            pthread_cond_wait(&worker->cond, &worker->mutex);
        }
        if (worker->pool->stopping) {
            pthread_mutex_unlock(&worker->mutex);
            break;
        }
        /* 复制任务后立即释放锁，处理期间不持锁 */
        chat_request_t req = worker->task;
        worker->has_task = 0;
        pthread_mutex_unlock(&worker->mutex);

        log_thread("worker=%d start type=%s user=%s", worker->id,
                   request_type_name(req.type), req.username);
        handle_request(&req);
        log_thread("worker=%d finish type=%s user=%s", worker->id,
                   request_type_name(req.type), req.username);
        recycle_worker(worker);
    }
    return NULL;
}

/*
 * 初始化线程池。
 * 分配 worker 数组和 idle_stack，创建 size 个线程并全部标记为 IDLE。
 */
int thread_pool_init(thread_pool_t *pool, int size) {
    memset(pool, 0, sizeof(*pool));
    pool->workers = calloc((size_t)size, sizeof(worker_t));
    pool->idle_stack = calloc((size_t)size, sizeof(int));
    if (!pool->workers || !pool->idle_stack) return -1;
    pool->size = size;
    pool->idle_top = -1;
    pthread_mutex_init(&pool->mutex, NULL);
    pthread_cond_init(&pool->idle_cond, NULL);

    for (int i = 0; i < size; i++) {
        worker_t *w = &pool->workers[i];
        w->id = i;
        w->state = WORKER_IDLE;
        w->pool = pool;
        pthread_mutex_init(&w->mutex, NULL);
        pthread_cond_init(&w->cond, NULL);
        /* 初始化时所有 worker 都是空闲的，全部压入空闲栈 */
        pool->idle_stack[++pool->idle_top] = i;
        if (pthread_create(&w->tid, NULL, worker_main, w) != 0) return -1;
        log_thread("worker=%d created state=IDLE", i);
    }
    return 0;
}

/*
 * 分派请求 — 主线程调用。
 *
 * 关键流程：
 *   1. 在 pool->mutex 下从 idle_stack 栈顶弹出空闲 worker id；
 *   2. 标记 worker 为 BUSY，记录分派日志；
 *   3. 释放 pool->mutex 后，在 w->mutex 下设置 task 并 signal worker；
 *   4. 如果没有空闲线程，调用者阻塞在 idle_cond 上直到有 worker 回收。
 */
int thread_pool_dispatch(thread_pool_t *pool, const chat_request_t *req) {
    pthread_mutex_lock(&pool->mutex);
    while (pool->idle_top < 0 && !pool->stopping) {
        pthread_cond_wait(&pool->idle_cond, &pool->mutex);
    }
    if (pool->stopping) {
        pthread_mutex_unlock(&pool->mutex);
        return -1;
    }
    int id = pool->idle_stack[pool->idle_top--];    /* LIFO 弹栈 */
    worker_t *w = &pool->workers[id];
    w->state = WORKER_BUSY;
    log_thread("worker=%d dispatched type=%s user=%s state=BUSY idle_top=%d",
               id, request_type_name(req->type), req->username, pool->idle_top);
    pthread_mutex_unlock(&pool->mutex);

    /* 在 worker 自己的锁下设置任务并唤醒，避免惊群 */
    pthread_mutex_lock(&w->mutex);
    w->task = *req;
    w->has_task = 1;
    pthread_cond_signal(&w->cond);
    pthread_mutex_unlock(&w->mutex);
    return 0;
}

/*
 * 销毁线程池。
 * 设置 stopping=1 并逐一 signal 所有 worker，然后 pthread_join 等待退出。
 */
void thread_pool_destroy(thread_pool_t *pool) {
    if (!pool || !pool->workers) return;
    pthread_mutex_lock(&pool->mutex);
    pool->stopping = 1;
    pthread_mutex_unlock(&pool->mutex);

    /* 逐一唤醒所有 worker，它们检查 stopping 后会 break 退出 */
    for (int i = 0; i < pool->size; i++) {
        pthread_mutex_lock(&pool->workers[i].mutex);
        pthread_cond_signal(&pool->workers[i].cond);
        pthread_mutex_unlock(&pool->workers[i].mutex);
    }
    for (int i = 0; i < pool->size; i++) pthread_join(pool->workers[i].tid, NULL);
    free(pool->workers);
    free(pool->idle_stack);
    memset(pool, 0, sizeof(*pool));
}
