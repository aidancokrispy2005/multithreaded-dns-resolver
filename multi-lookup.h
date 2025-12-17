#ifndef MULTI_LOOKUP_H
#define MULTI_LOOKUP_H

#include <string.h>
#include <sys/time.h>
#include "util.h"
#include "array.h"

#define MAX_INPUT_FILES 100
#define MAX_REQUESTER_THREADS 10
#define MAX_RESOLVER_THREADS 10
#define MAX_IP_LENGTH INET6_ADDRSTRLEN
#define EXIT_FAILURE 1
#define EXIT_SUCCESS 0

// we need a strictly consumer based queue that allows us to tell requester threads
// to terminate
typedef struct {
    char filenames[MAX_INPUT_FILES][MAX_NAME_LENGTH];
    int count;
    int next_to_process;
    pthread_mutex_t lock;
} filename_queue;

/* Thread parameter structure for passing data to threads */
typedef struct {
    array* hostname_array;
    filename_queue* file_queue;
    FILE* log_file;
    pthread_mutex_t* log_mutex;
    pthread_mutex_t* stdout_mutex;
} thread_params;

/*delegate filename_queue functions for every "singular action" we want to accomplish*/
int filename_queue_init(filename_queue* fq);
int filename_queue_add(filename_queue* fq, const char* filename);
char* filename_queue_get(filename_queue* fq);
void filename_queue_cleanup(filename_queue* fq);

double calculate_delta(struct timeval* start_time, struct timeval* end_time);

void* requester_thread(void* arg);
void* resolver_thread(void* arg);

#endif