#include "array.h"

//all functions ret 0 on success, -1 on failure (except array_free())

int array_init(array *s) {
    if (s == NULL) return -1;
    
    //FIFO or queue implementation
    //need to track array's head, tail and count
    s->head = 0;
    s->tail = 0;
    s->count = 0;

    if (pthread_mutex_init(&s->lock, NULL)) {
        return -1;
    }
    
    if (pthread_cond_init(&s->not_full, NULL)) {
        pthread_mutex_destroy(&s->lock);
        return -1;
    }
    
    if (pthread_cond_init(&s->not_empty, NULL)) {
        pthread_mutex_destroy(&s->lock);
        pthread_cond_destroy(&s->not_full);
        return -1;
    }
    return 0;
}

/*
IMPORTANT: any time shared state accessed, LOCK the mutex
in my array implementation:
s->head, s->tail, s->count, s->buffer[][] all shared state
*/

//producer: adds hostnames to the array
int array_put(array *s, char *hostname) {
    if (s == NULL || hostname == NULL) {
        return -1;
    }

    pthread_mutex_lock(&s->lock);
    
    // while array is full, pthread wait
    while (s->count == ARRAY_SIZE) {
        pthread_cond_wait(&s->not_full, &s->lock);
    }
    //copy hostname AT TAIL; names enter at tail, exit at head; FIFO
    strncpy(s->buffer[s->tail], hostname, MAX_NAME_LENGTH);
    
    s->tail = (s->tail + 1) % ARRAY_SIZE;
    s->count++;
    
    pthread_cond_signal(&s->not_empty);
    pthread_mutex_unlock(&s->lock);
    
    return 0;
}

//consumer: remove a hostname from the array
int array_get(array *s, char **hostname) {
    if (s == NULL || hostname == NULL) {
        return -1;
    }
    pthread_mutex_lock(&s->lock);

    while (s->count == 0) {
        pthread_cond_wait(&s->not_empty, &s->lock);
    }

    *hostname = (char *)malloc(MAX_NAME_LENGTH);
    if (*hostname == NULL) {
        pthread_mutex_unlock(&s->lock);
        return -1;
    }
    
    //copy from the buffer at HEAD; FIFO
    strncpy(*hostname, s->buffer[s->head], MAX_NAME_LENGTH);

    s->head = (s->head + 1) % ARRAY_SIZE;
    s->count--;
    
    pthread_cond_signal(&s->not_full);
    pthread_mutex_unlock(&s->lock);
    
    return 0;
}

void array_free(array *s) {
    if (s == NULL) return;
    
    pthread_mutex_destroy(&s->lock);
    pthread_cond_destroy(&s->not_full);
    pthread_cond_destroy(&s->not_empty);
}