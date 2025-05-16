#ifndef _THREADPOOL_H_
#define _THREADPOOL_H_

#include <stdbool.h>

typedef struct threadPool_t threadPool_t;
typedef struct task_t task_t;

threadPool_t* threadPool_create(size_t workqueue_size, size_t threads_count);

void threadPool_destroy(threadPool_t** pool, bool force);

void threadPool_dispach(threadPool_t* pool);

void threadPool_pushWork(threadPool_t* pool, void* (*work)(void*), void* arg);

#endif