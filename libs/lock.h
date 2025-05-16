#ifndef _LOCK_H_
#define _LOCK_H_

#include <pthread.h>

#define LOCK_INITIALIZER      (lock_t)PTHREAD_MUTEX_INITIALIZER
#define SIGNAL_INITIALIZER    (signal_t)PTHREAD_COND_INITIALIZER
#define WRLOCK_INITIALIZER    (wrlock_t)PTHREAD_RWLOCK_INITIALIZER;

typedef pthread_mutex_t lock_t;
typedef pthread_cond_t  signal_t;
typedef pthread_rwlock_t rwlock_t;

static inline int lock_init(lock_t* lock) {
    return pthread_mutex_init((pthread_mutex_t*) lock, NULL);
}

static inline int lock_destroy(lock_t* lock) {
    return pthread_mutex_destroy((pthread_mutex_t*) lock);
}

static inline int trylock(lock_t* lock) {
    return pthread_mutex_trylock((pthread_mutex_t*)lock);
}

static inline void lock(lock_t* lock) {
    pthread_mutex_lock((pthread_mutex_t*)lock);
}

static inline void lock_wait(lock_t* lock, signal_t* signal) {
    pthread_cond_wait((pthread_cond_t*)signal, (pthread_mutex_t*)lock);
}

static inline void lock_timedwait(lock_t* lock, signal_t* signal, int wait_ms) {
    if(wait_ms > 0) {
        struct timespec time;
        long sec = wait_ms / 1000;
        const long nsec = (wait_ms % 1000) * 1000000;
        clock_gettime(CLOCK_REALTIME, &time);
        const long t = 1000000000 - time.tv_nsec;
        if(t > nsec) {
            time.tv_sec += sec;
            time.tv_nsec += nsec;
        }
        else {
            time.tv_sec += (sec + 1);
            time.tv_nsec = nsec - t;
        }
        pthread_cond_timedwait((pthread_cond_t*)signal, (pthread_mutex_t*)lock, &time);
    }
    else lock_wait(lock, signal);
}

static inline void unlock(lock_t* lock) {
    pthread_mutex_unlock((pthread_mutex_t*)lock);
}

static inline int signal_init(signal_t* signal) {
    return pthread_cond_init((pthread_cond_t*) signal, NULL);
}

static inline int signal_destroy(signal_t* signal) {
    return pthread_cond_destroy((pthread_cond_t*) signal);
}

static inline void unlock_signal(lock_t* lock, signal_t* signal) {
    unlock(lock);
    pthread_cond_signal((pthread_cond_t*)signal);
}

static inline void signal_broacast(signal_t* signal) {
    pthread_cond_broadcast((pthread_cond_t*)signal);
}

static inline int rwlock_init(rwlock_t* lock) {
    return pthread_rwlock_init((pthread_rwlock_t*) lock, NULL);
}

static inline int rwlock_destroy(rwlock_t* lock) {
    return pthread_rwlock_destroy((pthread_rwlock_t*) lock);
}

static inline int rwlock_read_lock(rwlock_t* lock) {
    return pthread_rwlock_rdlock((pthread_rwlock_t*)lock);
}

static inline int rwlock_write_lock(rwlock_t* lock) {
    return pthread_rwlock_wrlock((pthread_rwlock_t*)lock);
}

static inline int rwlock_unlock(rwlock_t* lock) {
    return pthread_rwlock_unlock((pthread_rwlock_t*)lock);
}

#endif