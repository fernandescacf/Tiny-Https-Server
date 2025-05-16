#ifndef _QUEUE_H_
#define _QUEUE_H_

#include <stdlib.h>
#include <stdbool.h>

typedef struct queue_t queue_t;

bool empty(queue_t* queue);

void* pop(queue_t* queue);

void push(queue_t* queue, void* data);

queue_t* queue_create(size_t size);

void queue_destroy(queue_t** queue);

#endif