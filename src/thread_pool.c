#include "thread_pool.h"
#include "handlers.h"
#include "log.h"
#include <stdlib.h>
#include <string.h>

static void recycle_worker(worker_t *worker) {
    thread_pool_t *pool = worker->pool;
    pthread_mutex_lock(&pool->mutex);
    worker->state = WORKER_IDLE;
    pool->idle_stack[++pool->idle_top] = worker->id;
    log_thread("worker=%d state=IDLE recycled idle_top=%d", worker->id, pool->idle_top);
    pthread_cond_signal(&pool->idle_cond);
    pthread_mutex_unlock(&pool->mutex);
}

static void *worker_main(void *arg) {
    worker_t *worker = arg;
    for (;;) {
        pthread_mutex_lock(&worker->mutex);
        while (!worker->has_task && !worker->pool->stopping) {
            pthread_cond_wait(&worker->cond, &worker->mutex);
        }
        if (worker->pool->stopping) {
            pthread_mutex_unlock(&worker->mutex);
            break;
        }
        chat_request_t req = worker->task;
        worker->has_task = 0;
        pthread_mutex_unlock(&worker->mutex);

        log_thread("worker=%d start type=%s user=%s", worker->id, request_type_name(req.type), req.username);
        handle_request(&req);
        log_thread("worker=%d finish type=%s user=%s", worker->id, request_type_name(req.type), req.username);
        recycle_worker(worker);
    }
    return NULL;
}

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
        pool->idle_stack[++pool->idle_top] = i;
        if (pthread_create(&w->tid, NULL, worker_main, w) != 0) return -1;
        log_thread("worker=%d created state=IDLE", i);
    }
    return 0;
}

int thread_pool_dispatch(thread_pool_t *pool, const chat_request_t *req) {
    pthread_mutex_lock(&pool->mutex);
    while (pool->idle_top < 0 && !pool->stopping) {
        pthread_cond_wait(&pool->idle_cond, &pool->mutex);
    }
    if (pool->stopping) {
        pthread_mutex_unlock(&pool->mutex);
        return -1;
    }
    int id = pool->idle_stack[pool->idle_top--];
    worker_t *w = &pool->workers[id];
    w->state = WORKER_BUSY;
    log_thread("worker=%d dispatched type=%s user=%s state=BUSY idle_top=%d",
               id, request_type_name(req->type), req->username, pool->idle_top);
    pthread_mutex_unlock(&pool->mutex);

    pthread_mutex_lock(&w->mutex);
    w->task = *req;
    w->has_task = 1;
    pthread_cond_signal(&w->cond);
    pthread_mutex_unlock(&w->mutex);
    return 0;
}

void thread_pool_destroy(thread_pool_t *pool) {
    if (!pool || !pool->workers) return;
    pthread_mutex_lock(&pool->mutex);
    pool->stopping = 1;
    pthread_mutex_unlock(&pool->mutex);
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
