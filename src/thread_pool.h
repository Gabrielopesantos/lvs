#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

typedef struct job {
    void (*function)(void *, int);
    void *arg;
    int arg1;
    struct job *next;
} job_t;

typedef struct {
    job_t *jobs;
    pthread_mutex_t mutex;
    pthread_cond_t cond;
    bool shutdown;
} threadpool_t;

// FIXME
void *worker_thread(void *arg);
void init_threadpool(threadpool_t *pool, int num_threads);
void submit_job(threadpool_t *pool, void (*function)(void *, int), void *arg,
                int arg1);
void shutdown_threadpool(threadpool_t *pool);
