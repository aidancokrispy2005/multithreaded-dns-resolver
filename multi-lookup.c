#include "multi-lookup.h"

// helper function to calculate delta between a start and end time
double calculate_delta(struct timeval* start_time, struct timeval* end_time) {
    double start_sec = (start_time->tv_sec) + (start_time->tv_usec / 1000000.0);
    double end_sec = (end_time->tv_sec) + (end_time->tv_usec / 1000000.0);
    return (end_sec - start_sec);
}

int filename_queue_init(filename_queue* fq) {
    if (fq == NULL) return 1;

    if (pthread_mutex_init(&fq->lock, NULL)) {
        return 1;
    }
    fq->count = 0;
    fq->next_to_process = 0;
    return 0;
}

//copy the given filename to the queue at the count index, then increment count
int filename_queue_add(filename_queue *fq, const char* filename) {
    if (fq == NULL) return 1;
    if (fq->count >= MAX_INPUT_FILES) return 1;

    //adding 2 to strlen call to ensure n is greater than src; this way resulting string always null-terminated
    strncpy(fq->filenames[fq->count], filename, strlen(filename)+2);
    fq->count++;
    return 0;
}

char* filename_queue_get(filename_queue *fq) {
    pthread_mutex_lock(&fq->lock);
    if (fq->next_to_process >= fq->count) {
        // all files have been claimed by a thread
        pthread_mutex_unlock(&fq->lock);
        return NULL;
        // null will be our first "poison pill": tells requesters to kill themselves
    }

    char* filename = fq->filenames[fq->next_to_process];
    fq->next_to_process++;

    pthread_mutex_unlock(&fq->lock);
    return filename;
}

void filename_queue_cleanup(filename_queue* fq) {
    pthread_mutex_destroy(&fq->lock);
}

void* requester_thread(void* arg) {
    // we can type cast a void pointer to any type; cast to a thread_params struct
    thread_params* params = (thread_params*)arg;
    struct timeval start_time;
    gettimeofday(&start_time, NULL);
    char* filename;
    int files_serviced = 0;
    while ((filename = filename_queue_get(params->file_queue)) != NULL) {
        FILE* input_file = fopen(filename, "r");
        if (input_file == NULL) {
            fprintf(stderr, "invalid input file\n");
            // non-fatal error
            continue;
        }
        char hostname[MAX_NAME_LENGTH];
        while (fgets(hostname, sizeof(hostname), input_file) != NULL) {

            // strip newline: https://stackoverflow.com/a/28462221
            hostname[strcspn(hostname, "\n")] = 0;
            
            // encountered bug where empty strings were added to queue; added length requirement
            if ((strlen(hostname)) > 0) {
                if (array_put(params->hostname_array, hostname) < 0) {
                    // array_put handles blocking, so error would be unusual
                    fprintf(stderr, "failed to send hostname to buffer\n");
                    continue;
                }
            
                // critical section for requester logfile
                if (pthread_mutex_lock(params->log_mutex)) {
                    fprintf(stderr, "requester logfile mutex failed\n");
                    return NULL;
                }
                if (fprintf(params->log_file, "%s\n", hostname) < 0) {
                    fprintf(stderr, "failed to print hostname to logfile\n");
                    continue;
                }
                if (pthread_mutex_unlock(params->log_mutex)) {
                    fprintf(stderr, "failed to unlock requester logfile mutex\n");
                    return NULL;
                }
            }

            // end of critical section
        }
        fclose(input_file);
        files_serviced++;
    }
    struct timeval end_time;
    gettimeofday(&end_time, NULL);
    double time_elapsed = calculate_delta(&start_time, &end_time);
    //critical section for stdout
    if (pthread_mutex_lock(params->stdout_mutex)) {
        fprintf(stderr, "stdout mutex failed to lock\n");
    }
    // %.6f is a precision operator to match the writeup output precision: https://cplusplus.com/reference/cstdio/fprintf/
    fprintf(stdout, "thread %lx serviced %i files in %.6f seconds\n", pthread_self(), files_serviced, time_elapsed);

    if (pthread_mutex_unlock(params->stdout_mutex)) {
        fprintf(stderr, "stdout mutex failed to unlock\n");
    }
    //end of critical section for stdout
    return NULL;
}

void* resolver_thread(void* arg) {
    thread_params* params = (thread_params*)arg;
    struct timeval start_time;
    gettimeofday(&start_time, NULL);
    int hostnames_resolved = 0;
    // we initialize a char pointer, and pass the address of that pointer into array_get
    char* hostname = NULL;
    while (array_get(params->hostname_array, &hostname) == 0) {
        if (strcmp(hostname, "POISON_PILL") == 0) {
            // poison pill detected; free memory and exit
            free(hostname);
            break;
        }
        char ip[MAX_IP_LENGTH];
        if (dnslookup(hostname, ip, MAX_IP_LENGTH) == UTIL_FAILURE) {
            strncpy(ip, "NOT_RESOLVED", MAX_IP_LENGTH);
        }

        // we now have the hostname and resolved ip and can write to logfile
        //critical section for resolver logfile

        if (pthread_mutex_lock(params->log_mutex)) {
            fprintf(stderr, "failed to lock resolver log mutex\n");
        }
        if (fprintf(params->log_file, "%s, %s\n", hostname, ip) < 0) {
            fprintf(stderr, "failed to write to resolver logfile\n");
        }
        if (pthread_mutex_unlock(params->log_mutex)) {
            fprintf(stderr, "failed to unlock resolver log mutex\n");
        }
        // end of critical section for resolver logfile

        hostnames_resolved++;
        free(hostname);
    }

    //calculate thread time
    struct timeval end_time;
    gettimeofday(&end_time, NULL);
    double time_elapsed = calculate_delta(&start_time, &end_time);
    // start of critical section for stdout

    if (pthread_mutex_lock(params->stdout_mutex)) {
        fprintf(stderr, "failed to lock stdout mutex\n");
    }
    if (fprintf(stdout, "thread %lx resolved %i hosts in %.6f seconds\n", pthread_self(), hostnames_resolved, time_elapsed) < 0) {
        fprintf(stderr, "failed to write to stdout\n");
    }
    if (pthread_mutex_unlock(params->stdout_mutex)) {
        fprintf(stderr, "failed to unlock resolver log mutex\n");
    }
    return NULL;
}

int main(int argc, char *argv[]) {
    struct timeval program_start;
    gettimeofday(&program_start, NULL);
    if (argc < 6) {
        printf("Not enough args!\n");
        return EXIT_FAILURE;
    }

    //gather and store information from our cmd line args
    int num_req_threads = atoi(argv[1]);
    int num_res_threads = atoi(argv[2]);
    //in multi-lookup, every arg after index 4 is a data file
    int num_files = argc-5;

    //CMDLINE ARG HANDLING
    if (num_req_threads < 1 || num_req_threads > MAX_REQUESTER_THREADS) {
        fprintf(stderr, "number of requester threads must be between 1 and %d\n", MAX_REQUESTER_THREADS);
        return EXIT_FAILURE;
    }
    if (num_res_threads < 1 || num_res_threads > MAX_RESOLVER_THREADS) {
        fprintf(stderr, "number of resolver threads must be between 1 and %d\n", MAX_RESOLVER_THREADS);
        return EXIT_FAILURE;
    }
    if (num_files > MAX_INPUT_FILES) {
        fprintf(stderr, "number of input files exceeds maximum of %d\n", MAX_INPUT_FILES);
        return EXIT_FAILURE;
    }
    if (num_files < 1) {
        fprintf(stderr, "at least one input file must be provided\n");
        return EXIT_FAILURE;
    }
    //"w" perms passed in allow fopen to create file if it doesn't exist already
    FILE* req_log = fopen(argv[3], "w");
    if (req_log == NULL) {
        fprintf(stderr, "unable to open requester log file %s\n", argv[3]);
        return EXIT_FAILURE;
    }
    FILE* res_log = fopen(argv[4], "w");
    if (res_log == NULL) {
        fprintf(stderr, "unable to open resolver log file %s\n", argv[4]);
        fclose(req_log);
        return EXIT_FAILURE;
    }

    //shared array and mutex initalizations
    array req_res_array;
    if (array_init(&req_res_array) != 0) {
        fprintf(stderr, "failed to initialize hostname array\n");
        fclose(req_log);
        fclose(res_log);
        return EXIT_FAILURE;
    }
    filename_queue main_req_fq;
    if (filename_queue_init(&main_req_fq) != 0) {
        fprintf(stderr, "failed to initialize filename queue\n");
        fclose(req_log);
        fclose(res_log);
        array_free(&req_res_array);
        return EXIT_FAILURE;
    }

    pthread_mutex_t res_log_mutex;
    pthread_mutex_t req_log_mutex;
    pthread_mutex_t stdout_mutex;

    if (pthread_mutex_init(&req_log_mutex, NULL) != 0) {
        fprintf(stderr, "failed to initialize requester log mutex\n");
        fclose(req_log);
        fclose(res_log);
        filename_queue_cleanup(&main_req_fq);
        array_free(&req_res_array);
        return EXIT_FAILURE;
    }
    if (pthread_mutex_init(&res_log_mutex, NULL) != 0) {
        fprintf(stderr, "failed to initialize resolver log mutex\n");
        pthread_mutex_destroy(&req_log_mutex);
        fclose(req_log);
        fclose(res_log);
        filename_queue_cleanup(&main_req_fq);
        array_free(&req_res_array);
        return EXIT_FAILURE;
    }
    if (pthread_mutex_init(&stdout_mutex, NULL) != 0) {
        fprintf(stderr, "failed to initialize stdout mutex\n");
        pthread_mutex_destroy(&req_log_mutex);
        pthread_mutex_destroy(&res_log_mutex);
        fclose(req_log);
        fclose(res_log);
        filename_queue_cleanup(&main_req_fq);
        array_free(&req_res_array);
        return EXIT_FAILURE;
    }

    // add data files to the filequeue (shared between main() and requesters)
    for (int i = 5; i < argc; i++) {
        if (filename_queue_add(&main_req_fq, argv[i])) {
            fprintf(stderr, "unable to add filename to queue: %s\n", argv[i]);
            // continue ~gracefully~
        }
    }

    // initialize requester, resolver thread pools

    /*
    - at first, encountered a major synchronization issue with thread_params structs
    - initialized parameters outside the loop, passing same address &params to each thread
    - main thread was overwriting params
    - now, we populate params inside the loop and pass a pointer into pthread_create to that specific element
    */

    // initialize requester pool
    pthread_t req_pool[num_req_threads];
    thread_params req_params[num_req_threads];
    for (int i = 0; i < num_req_threads; i++) {
        req_params[i].hostname_array = &req_res_array;
        req_params[i].file_queue = &main_req_fq;
        req_params[i].log_file = req_log;
        req_params[i].log_mutex = &req_log_mutex;
        req_params[i].stdout_mutex = &stdout_mutex;
        if (pthread_create(&req_pool[i], NULL, requester_thread, &req_params[i])) {
            fprintf(stderr, "failed to create requester thread\n");
            // continue without that thread
        }
    }

    // initialize resolver pools
    pthread_t res_pool[num_res_threads];
    thread_params res_params[num_res_threads];
    for (int i = 0; i < num_res_threads; i++) {
        res_params[i].hostname_array = &req_res_array;
        res_params[i].log_file = res_log;
        res_params[i].log_mutex = &res_log_mutex;
        res_params[i].stdout_mutex = &stdout_mutex;
        if (pthread_create(&res_pool[i], NULL, resolver_thread, &res_params[i])) {
            fprintf(stderr, "failed to create requester thread\n");
            // continue without that thread
        }
    }

    // join requesters
    for (int i = 0; i < num_req_threads; i++) {
        if (pthread_join(req_pool[i], NULL)) {
            fprintf(stderr, "failed to join a requester thread\n");
            return EXIT_FAILURE;
        }
    }

    // key of design: after requesters join, we KNOW there are no more hostnames to add
    // so for every resolver thread, we queue a poison pill
    // resolvers will continuously retrieve them and exit
    for (int i = 0; i < num_res_threads; i++) {
        if (array_put(&req_res_array, "POISON_PILL")) {
            fprintf(stderr, "failed to queue poison pills\n");
            return EXIT_FAILURE;
        }
    }

    // join resolvers
    for (int i = 0; i < num_res_threads; i++) {
        if (pthread_join(res_pool[i], NULL)) {
            fprintf(stderr, "failed to join a resolver thread\n");
            return EXIT_FAILURE;
        }
    }

    // cleanup: destroying mutexes, freeing array, freeing filename queue, closing files

    fclose(req_log);
    fclose(res_log);

    pthread_mutex_destroy(&req_log_mutex);
    pthread_mutex_destroy(&res_log_mutex);
    pthread_mutex_destroy(&stdout_mutex);

    array_free(&req_res_array);
    filename_queue_cleanup(&main_req_fq);

    // print program time
    struct timeval program_end;
    gettimeofday(&program_end, NULL);
    double program_time = calculate_delta(&program_start, &program_end);
    fprintf(stdout, "total time is %.6f seconds\n", program_time);

    return EXIT_SUCCESS;
}
