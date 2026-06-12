#ifndef THREAD_POOL_H
#define THREAD_POOL_H

#include <pthread.h>
#include "chat.h"

typedef struct thread_pool thread_pool_t;

typedef struct {
    int id;
    pthread_t tid;
    worker_state_t state;
    int has_task;
    chat_request_t task;
    pthread_mutex_t mutex;
    pthread_cond_t cond;
    thread_pool_t *pool;
} worker_t;

struct thread_pool {
    worker_t *workers;
    int size;
    int *idle_stack;
    int idle_top;
    volatile int stopping;
    pthread_mutex_t mutex;
    pthread_cond_t idle_cond;
};

int thread_pool_init(thread_pool_t *pool, int size);
int thread_pool_dispatch(thread_pool_t *pool, const chat_request_t *req);
void thread_pool_destroy(thread_pool_t *pool);

#endif
