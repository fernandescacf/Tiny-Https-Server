#include <stdlib.h>
#include <string.h>
#include <sys/sysinfo.h>
#include <lock.h>
#include <queue.h>
#include <threadpool.h>

#include <stdio.h>

struct threadPool_t {
    queue_t* workQueue;
    queue_t* tasksQueue;
    volatile bool running;
    task_t* tasks;
    size_t threads_count;
    pthread_t threads[1];
};

struct task_t {
    void* (*func)(void*);
    void* arg;
};

static void* thread_entry(void* arg) {
    threadPool_t* pool = (threadPool_t*)arg;
    task_t* task = NULL;

    while(pool->running) {
        if((task = pop(pool->workQueue)) == NULL) break;
        (void)task->func(task->arg);
        memset(task, 0, sizeof(*task));
        if(pool->running) push(pool->tasksQueue, task);
    }
    return NULL;
}

static inline task_t* threadPool_getTask(threadPool_t* pool) {
    return pop(pool->tasksQueue);
}

static inline void threadPool_pushTask(threadPool_t* pool, task_t* task) {
    push(pool->workQueue, task);
}

threadPool_t* threadPool_create(size_t workqueue_size, size_t threads_count) {
    if(threads_count == 0) threads_count = (get_nprocs() - 1);
    if(workqueue_size == 0) workqueue_size = threads_count * 2;
    threadPool_t* pool = calloc(1, sizeof(*pool) + (sizeof(pthread_t) * threads_count - sizeof(pthread_t)));
    pool->workQueue = queue_create(workqueue_size);
    pool->tasksQueue = queue_create(workqueue_size);
    pool->threads_count = threads_count;

    pool->tasks = calloc(workqueue_size, sizeof(*pool->tasks));
    int i = 0;
    for(; i < workqueue_size; ++i) {
        push(pool->tasksQueue, &pool->tasks[i]);
    }

    return pool;
}

void threadPool_destroy(threadPool_t** pool, bool force) {
    (*pool)->running = false;

    queue_destroy(&(*pool)->tasksQueue);
    queue_destroy(&(*pool)->workQueue);

    if(force) {
        int i;
        for(i = 0; i < (*pool)->threads_count; ++i) {
            pthread_cancel((*pool)->threads[i]);
            pthread_join((*pool)->threads[i], NULL);
        }
    }
    free((*pool)->tasks);
    free((*pool));
    *pool = NULL;
}

void threadPool_dispach(threadPool_t* pool) {
    int i = 0;
    pool->running = true;
    for(; i < pool->threads_count; ++i) {
        pthread_create(&pool->threads[i], NULL, thread_entry, pool);
    }
}

void threadPool_pushWork(threadPool_t* pool, void* (*work)(void*), void* arg) {
    task_t* task = threadPool_getTask(pool);
    task->func = work;
    task->arg = arg;
    threadPool_pushTask(pool, task);
}
