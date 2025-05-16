#include <ucontext.h>
#include <async.h>
#include <lock.h>
#include <threadpool.h>
#include <stdbool.h>
#include <sys/mman.h>

struct asyncTask_t {
    void* (*func)(asyncTask_t*, void*);
    ucontext_t caller_ctx;
    ucontext_t callee_ctx;
    void* ret;
    lock_t lock;
    signal_t signal;
    volatile asyncState_t state;
};

const size_t DEFAULT_STACK_CAPACITY = 4096 * 16;    // Going lower seems to fail when trying to grow the stack

static struct {
    bool running;
    threadPool_t* threads;
    lock_t lock;
    size_t stacks_entries;
    void** sps;
} async_ctrl = {false, NULL, LOCK_INITIALIZER, 0, NULL};

static void* async_get_stack() {
    lock(&async_ctrl.lock);
    void* sp = NULL;
    for(size_t i = 0; i < async_ctrl.stacks_entries; ++i) {
        if(async_ctrl.sps[i] != NULL) {
            sp = async_ctrl.sps[i];
            async_ctrl.sps[i] = NULL;
            break;
        }
    }
    if(sp == NULL) {
        size_t new_entry = async_ctrl.stacks_entries;
        async_ctrl.stacks_entries *= 2;
        async_ctrl.sps = realloc(async_ctrl.sps, sizeof(void*) * async_ctrl.stacks_entries);
        for(size_t i = new_entry + 1; i < async_ctrl.stacks_entries; ++i)
            async_ctrl.sps[i] = mmap(NULL, DEFAULT_STACK_CAPACITY, PROT_WRITE|PROT_READ, MAP_PRIVATE|MAP_STACK|MAP_ANONYMOUS|MAP_GROWSDOWN, -1, 0);
        async_ctrl.sps[new_entry] = NULL;
        sp = mmap(NULL, DEFAULT_STACK_CAPACITY, PROT_WRITE|PROT_READ, MAP_PRIVATE|MAP_STACK|MAP_ANONYMOUS|MAP_GROWSDOWN, -1, 0);
    }
    unlock(&async_ctrl.lock);

    return sp;
}

static void async_free_stack(void* sp) {
    lock(&async_ctrl.lock);
    for(size_t i = 0; i < async_ctrl.stacks_entries; ++i) {
        if(async_ctrl.sps[i] == NULL) {
            async_ctrl.sps[i] = sp;
            break;
        }
    }
    unlock(&async_ctrl.lock);
}

void async_set_state(asyncTask_t* task, asyncState_t state) {
    if(state == AsyncRunning){
        task->state = AsyncRunning;
        return;
    }
    lock(&task->lock);
    if(task->state != AsyncDetached) task->state = state;
    unlock_signal(&task->lock, &task->signal);
}

static asyncState_t async_wait_suspend(asyncTask_t* task, int wait_ms) {
    lock(&task->lock);
    asyncState_t state;
    if(task->state == AsyncRunning) {
        lock_timedwait(&task->lock, &task->signal, wait_ms);
    }
    state = task->state;
    unlock(&task->lock);
    return state;
}

static void asyncTask_clean(asyncTask_t** task) {
    async_free_stack((*task)->callee_ctx.uc_stack.ss_sp);
    lock_destroy(&(*task)->lock);
    signal_destroy(&(*task)->signal);
    free(*task);
    *task = NULL;
}

static void async_entry(asyncTask_t* task) {
    if(task->state != AsyncDetached) task->state = AsyncRunning;
    task->ret = task->func(task, task->ret);
    ucontext_t repor = task->caller_ctx;
    async_set_state(task, AsyncDead);
    setcontext(&repor);
}

static void async_run(asyncTask_t* task) {
    getcontext(&task->callee_ctx);
    task->callee_ctx.uc_stack.ss_sp = async_get_stack();
    task->callee_ctx.uc_stack.ss_size = DEFAULT_STACK_CAPACITY;
    task->callee_ctx.uc_link = 0;
    makecontext(&task->callee_ctx, (void (*)())async_entry, 1, task);
    swapcontext(&task->caller_ctx, &task->callee_ctx);
    // Executed after async_entry returns
    if(task->state == AsyncDetached) {
        asyncTask_clean(&task);
    }
    else if(task->state != AsyncDead && task->state != AsyncSuspended) {
        async_set_state(task, AsyncSuspended);
    }
}

static void async_resume(asyncTask_t* task) {
    swapcontext(&task->caller_ctx, &task->callee_ctx);
}

EAsync_t async_engine_start(size_t threads) {
    if(async_ctrl.running) return EAsync_Busy;
    if((async_ctrl.threads = threadPool_create(0, threads)) == NULL) return EAsync_Mem;

    // Start with two stack available
    async_ctrl.stacks_entries = 2;
    async_ctrl.sps = malloc(sizeof(void*) * async_ctrl.stacks_entries);
    async_ctrl.sps[0] = mmap(NULL, DEFAULT_STACK_CAPACITY, PROT_WRITE|PROT_READ, MAP_PRIVATE|MAP_ANONYMOUS|MAP_GROWSDOWN, -1, 0);
    async_ctrl.sps[1] = mmap(NULL, DEFAULT_STACK_CAPACITY, PROT_WRITE|PROT_READ, MAP_PRIVATE|MAP_ANONYMOUS|MAP_GROWSDOWN, -1, 0);

    if(async_ctrl.sps[0] == NULL || async_ctrl.sps[1] == NULL) {
        threadPool_destroy(&async_ctrl.threads, true);
        free(async_ctrl.sps);
        return EAsync_Mem;
    }
    async_ctrl.lock = LOCK_INITIALIZER;
    threadPool_dispach(async_ctrl.threads);
    async_ctrl.running = true;

    return EAsync_Success;
}

EAsync_t async_engine_stop() {
    if(!async_ctrl.running && !async_ctrl.threads) return EAsync_Success;

    threadPool_destroy(&async_ctrl.threads, true);
    async_ctrl.running = false;

    for(size_t i = 0; i < async_ctrl.stacks_entries; ++i) {
        if(munmap(async_ctrl.sps[i], DEFAULT_STACK_CAPACITY) == 0) {
//            void* ptr = async_ctrl.sps[i];
//            do {
//                ptr -= 4096;
//            }while(munmap(ptr, DEFAULT_STACK_CAPACITY) == 0);
        }
    }
    free(async_ctrl.sps);
    lock_destroy(&async_ctrl.lock);

    return EAsync_Success;
}

asyncTask_t* async(void* (*func)(asyncTask_t*, void*), void* arg) {
    if(async_ctrl.running == false) {
#ifdef ASYNC_DEFAULT_START
        if(async_engine_start(0) != EAsync_Success) return NULL;
#else
        return NULL;
#endif
    }

    asyncTask_t* task = calloc(1, sizeof(*task));
    task->ret = arg;
    task->func = func;
    task->lock = LOCK_INITIALIZER;
    task->signal = SIGNAL_INITIALIZER;
    task->state = AsyncUnborn;
    
    threadPool_pushWork(async_ctrl.threads, (void* (*)(void*))async_run, task);

    return task;
}

void suspend(asyncTask_t* task, void* yield) {
    lock(&task->lock);
    task->ret = yield;
    if(task->state == AsyncDetached) {
        unlock(&task->lock);
        return;
    }
    task->state = AsyncSuspended;
    getcontext(&task->callee_ctx);
    // Note we will be resumed here so we do need to check the state
    if(task->state == AsyncSuspended) {
        // We have to copy the context because after the unlock it's possible for the task to be cleanup
        ucontext_t restore = task->caller_ctx;
        unlock_signal(&task->lock, &task->signal);
        setcontext(&restore);
    }
}

void resume(asyncTask_t* task) {
    if(task->state == AsyncSuspended) {
        lock(&task->lock);
        task->state = AsyncRunning;
        unlock(&task->lock);
        threadPool_pushWork(async_ctrl.threads, (void* (*)(void*))async_resume, task);
    }
}

asyncYield_t wait_yield(asyncTask_t** task, asyncState_t* state) {
    asyncYield_t yield = {.valid = false, .yield = (yield_t)NULL};

    asyncState_t _state = AsyncUnborn;
    do {
        _state = async_wait_suspend(*task, 0);
        if(_state == AsyncSuspended)  {
            yield.valid = true;
            yield.yield.v = (*task)->ret;
        }
        else if(_state == AsyncDead)
        {
            yield.valid = true;
            yield.yield.v = (*task)->ret;
            asyncTask_clean(task);
        }
    } while(_state == AsyncUnborn || _state == AsyncRunning);

    if(state) *state = _state;

    return yield;
}

asyncYield_t get_yield(asyncTask_t** task, asyncState_t* state) {
    asyncYield_t yield = {.valid = false, .yield = (yield_t)NULL};
    lock(&(*task)->lock);
    asyncState_t _state = (*task)->state;
    void* ret = (*task)->ret;
    unlock(&(*task)->lock);
    
    if(_state == AsyncSuspended) {
        yield.valid = true;
        yield.yield.v = ret;
    }
    else if(_state == AsyncDead)
    {
        yield.valid = true;
        yield.yield.v = ret;
        asyncTask_clean(task);
    }
    
    if(state) *state = _state;

    return yield;
}

asyncYield_t await(asyncTask_t** task) {
    while((*task)->state != AsyncDead) {
        if((*task)->state == AsyncSuspended) {
            (*task)->state = AsyncRunning;
            async_resume((*task));
        }
        else {
            (void)async_wait_suspend((*task), 0);
        }
    }
    asyncYield_t yield = {.valid = false, .yield = (yield_t)(*task)->ret};
    asyncTask_clean(task);
    return yield;
}

asyncYield_t wait_time(asyncTask_t** task, int ms, asyncState_t* state) {
    if(ms < 1) {
        if(state) *state = AsyncDead;
        return await(task);
    }
    if(async_wait_suspend((*task), ms) != AsyncRunning){
        if(state) *state = (*task)->state;
        return (asyncYield_t){.valid = true, .yield = (yield_t)(*task)->ret};
    }
    if(state) *state = (*task)->state;
    return (asyncYield_t){.valid = false, .yield = (yield_t)NULL};
}

void async_detach(asyncTask_t** task) {
    lock(&(*task)->lock);

    if((*task)->state == AsyncDead) {
        unlock(&(*task)->lock);
        asyncTask_clean(task);
        return;
    }

    asyncState_t state = (*task)->state;
    (*task)->state = AsyncDetached;
    unlock(&(*task)->lock);

    if(state == AsyncSuspended) {
        threadPool_pushWork(async_ctrl.threads, (void* (*)(void*))async_resume, *task);
    }
}
