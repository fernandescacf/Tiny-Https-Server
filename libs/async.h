#ifndef _ASYNC_H_
#define _ASYNC_H_

#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>

typedef struct asyncTask_t asyncTask_t;
typedef struct asyncYield_t asyncYield_t;
typedef void* (*async_func_t)(asyncTask_t*, void*);

typedef enum {
    AsyncUnborn,
    AsyncDead,
    AsyncRunning,
    AsyncSuspended,
    AsyncDetached,
}asyncState_t;

typedef enum {
    EAsync_Success = 0,
    EAsync_Busy,
    EAsync_Mem,
    EAsync_Error
}EAsync_t;

typedef union {
    void*       v;
    int64_t     i64;
    uint64_t    u64;
    int64_t*    p_i64;
    uint64_t*   p_u64;
    int32_t     i32;
    uint32_t    u32;
    int32_t*    p_i32;
    uint32_t*   p_u32;
    int16_t     i16;
    uint16_t    u16;
    int16_t*    p_i16;
    uint16_t*   p_u16;
    int8_t      i8;
    uint8_t     u8;
    int8_t*     p_i8;
    uint8_t*    p_u8;
}yield_t;

struct asyncYield_t {
    bool valid;
    yield_t yield;
};

EAsync_t async_engine_start(size_t threads);

EAsync_t async_engine_stop();

asyncTask_t* async(void* (*func)(asyncTask_t*, void*), void* arg);

void suspend(asyncTask_t* task, void* yield);

void resume(asyncTask_t* task);

asyncYield_t wait_yield(asyncTask_t** task, asyncState_t* state);

asyncYield_t get_yield(asyncTask_t** task, asyncState_t* state);

asyncYield_t await(asyncTask_t** task);

void async_detach(asyncTask_t** task);

asyncYield_t wait_time(asyncTask_t** task, int ms, asyncState_t* state);

#endif