#include <unistd.h>
#include <lock.h>
#include <queue.h>

struct queue_t {
    bool alive;
    size_t head;
    size_t tail;
    size_t size;
    size_t pending;
    lock_t lock;
    signal_t pop_signal;
    signal_t push_signal;
    void* array[1];
};

bool empty(queue_t* queue) {
    lock(&queue->lock);
    bool is_empty = (queue->head == queue->tail);
    unlock(&queue->lock);
    return is_empty;
}

void* pop(queue_t* queue) {
    if(!queue->alive) return NULL;
    lock(&queue->lock);
    queue->pending += 1;
    while(queue->head == queue->tail && queue->array[queue->head] == NULL) {
        lock_wait(&queue->lock, &queue->push_signal);
        if(!queue->alive) {
            queue->pending -= 1;
            unlock(&queue->lock);
            return NULL;
        }
    }
    void* data = queue->array[queue->head];
    queue->array[queue->head++] = NULL;
    if(queue->head >= queue->size) queue->head = 0;
    queue->pending -= 1;
    unlock_signal(&queue->lock, &queue->pop_signal);
    return data;
}

void push(queue_t* queue, void* data) {
    if(!queue->alive) return;
    lock(&queue->lock);
    queue->pending += 1;
    while(queue->tail == queue->head && queue->array[queue->tail] != NULL) {
        lock_wait(&queue->lock, &queue->pop_signal);
        if(!queue->alive) {
            queue->pending -= 1;
            unlock(&queue->lock);
            return;
        }
    }
    queue->array[queue->tail++] = data;
    if(queue->tail >= queue->size) queue->tail = 0;
    queue->pending -= 1;
    unlock_signal(&queue->lock, &queue->push_signal);
}

queue_t* queue_create(size_t size) {
    queue_t* queue = calloc(1, sizeof(*queue) + (sizeof(void*) * size - sizeof(void*)));
    queue->size = size;
    queue->lock = LOCK_INITIALIZER;
    queue->pop_signal = SIGNAL_INITIALIZER;
    queue->push_signal = SIGNAL_INITIALIZER;
    queue->alive = true;
    queue->pending = 0;
    return queue;
}

void queue_destroy(queue_t** queue) {
    (*queue)->alive = false;
    while((*queue)->pending > 0) {
        signal_broacast(&(*queue)->pop_signal);
        signal_broacast(&(*queue)->push_signal);
        usleep(100);
    }
    lock_destroy(&(*queue)->lock);
    signal_destroy(&(*queue)->pop_signal);
    signal_destroy(&(*queue)->push_signal);
    free(*queue);
    *queue = NULL;
}
