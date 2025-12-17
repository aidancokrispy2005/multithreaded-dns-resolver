#ifndef ARRAY_H
#define ARRAY_H

#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <pthread.h>

#define ARRAY_SIZE 8
// longest hostname found in pa4.zip: "odnoklassniki.ru" - 17 including '\0'
#define MAX_NAME_LENGTH 32

typedef struct {
    //shared state variables for FIFO implementation
    // LOCK mutex before accessing
    char buffer[ARRAY_SIZE][MAX_NAME_LENGTH];
    int head;
    int tail;
    int count;
    
    //mutex lock; one thread can modify shared state at a time
    pthread_mutex_t lock;

    //condition variables to signal to producers/consumers
    pthread_cond_t not_full;
    pthread_cond_t not_empty;

} array;

int  array_init(array *s);
int  array_put(array *s, char *hostname);
int  array_get(array *s, char **hostname);
void array_free(array *s);

#endif