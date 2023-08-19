#include "thread_pool.h"
#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

void *worker_thread(void *arg) {
    threadpool_t *pool = (threadpool_t *)arg;
    while (1) {
        pthread_mutex_lock(&pool->mutex);

        while (pool->jobs == NULL && !pool->shutdown) {
            pthread_cond_wait(&pool->cond, &pool->mutex);
        }

        if (pool->shutdown) {
            pthread_mutex_unlock(&pool->mutex);
            pthread_exit(NULL);
        }

        job_t *job = pool->jobs;
        pool->jobs = job->next;

        pthread_mutex_unlock(&pool->mutex);

        job->function(job->arg, job->arg1);

        free(job);
    }
    return NULL;
}

void init_threadpool(threadpool_t *pool, int num_threads) {
    pthread_mutex_init(&pool->mutex, NULL);
    pthread_cond_init(&pool->cond, NULL);
    pool->jobs = NULL;
    pool->shutdown = false;

    for (int i = 0; i < num_threads; i++) {
        pthread_t tid;
        pthread_create(&tid, NULL, worker_thread, pool);
        pthread_detach(tid);
    }
}

void submit_job(threadpool_t *pool, void (*function)(void *, int), void *arg,
                int arg1) {
    job_t *job = malloc(sizeof(job_t));
    job->function = function;
    job->arg = arg;
    job->arg1 = arg1;
    job->next = NULL;

    pthread_mutex_lock(&pool->mutex);
    job_t *lastJob = pool->jobs;
    if (lastJob) {
        while (lastJob->next)
            lastJob = lastJob->next;
        lastJob->next = job;
    } else {
        pool->jobs = job;
    }

    pthread_cond_signal(&pool->cond);
    pthread_mutex_unlock(&pool->mutex);
}

void shutdown_threadpool(threadpool_t *pool) {
    pthread_mutex_lock(&pool->mutex);
    pool->shutdown = true;
    pthread_cond_broadcast(&pool->cond);
    pthread_mutex_unlock(&pool->mutex);
}
