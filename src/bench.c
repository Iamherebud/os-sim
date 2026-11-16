/*
 * bench.c - Multithreaded batch simulation with pthreads.
 *
 * Each "job" = generate a random workload (seeded by the job number), run
 * all 5 scheduling algorithms and all 3 page replacement algorithms on it.
 * Jobs are independent, so they can run in parallel.
 *
 * Synchronisation:
 *   - next_job counter: shared and modified by all threads -> protected
 *     by a mutex (without it, two threads could grab the same job).
 *   - results[job]: each job writes only its own slot -> no lock needed.
 *   - Each job seeds its OWN Rng from the job number, so the output is
 *     identical whether you use 1 thread or 16. The checksum proves it.
 */
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "concurrency.h"
#include "memory.h"
#include "scheduler.h"
#include "workload.h"

#define PROCS_PER_JOB 400
#define REFS_PER_JOB 30000
#define PAGES 256
#define FRAMES 16

double now_seconds(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec / 1e9;
}

typedef struct {
    int next_job;               /* shared: protected by lock */
    int total_jobs;
    JobResult *results;         /* results[i] written only by the thread doing job i */
    pthread_mutex_t lock;
} WorkQueue;

static void run_job(int job, JobResult *out) {
    Rng rng;
    rng_seed(&rng, 1000u + (unsigned)job);

    Process *procs = malloc(sizeof(Process) * PROCS_PER_JOB);
    int *refs = malloc(sizeof(int) * REFS_PER_JOB);
    if (!procs || !refs) { perror("malloc"); exit(1); }

    random_processes(&rng, procs, PROCS_PER_JOB, PROCS_PER_JOB * 4, 20);
    for (int a = 0; a < SCHED_ALGO_COUNT; a++) {
        Gantt g;
        gantt_init(&g);
        schedule((SchedAlgo)a, procs, PROCS_PER_JOB, 4, &g);
        out->avg_waiting[a] = compute_stats(procs, PROCS_PER_JOB, &g).avg_waiting;
        gantt_free(&g);
    }

    random_references(&rng, refs, REFS_PER_JOB, PAGES);
    for (int a = 0; a < PAGE_ALGO_COUNT; a++)
        out->page_faults[a] = simulate_paging((PageAlgo)a, refs, REFS_PER_JOB, FRAMES, 0).faults;

    free(procs);
    free(refs);
}

static void *worker(void *arg) {
    WorkQueue *q = arg;
    for (;;) {
        pthread_mutex_lock(&q->lock);          /* --- critical section --- */
        int job = q->next_job++;
        pthread_mutex_unlock(&q->lock);        /* ------------------------ */

        if (job >= q->total_jobs) break;
        run_job(job, &q->results[job]);
    }
    return NULL;
}

BenchResult run_bench(int jobs, int threads) {
    BenchResult r;
    memset(&r, 0, sizeof r);
    r.jobs = jobs;
    r.threads = threads;

    WorkQueue q = {.next_job = 0, .total_jobs = jobs};
    q.results = calloc(jobs, sizeof(JobResult));
    pthread_t *tids = malloc(sizeof(pthread_t) * threads);
    if (!q.results || !tids) { perror("malloc"); exit(1); }
    pthread_mutex_init(&q.lock, NULL);

    double start = now_seconds();
    for (int t = 0; t < threads; t++)
        if (pthread_create(&tids[t], NULL, worker, &q) != 0) { perror("pthread_create"); exit(1); }
    for (int t = 0; t < threads; t++)
        pthread_join(tids[t], NULL);
    r.seconds = now_seconds() - start;

    /* Aggregate after all threads are joined (single-threaded again). */
    unsigned long checksum = 1469598103UL;
    for (int j = 0; j < jobs; j++) {
        for (int a = 0; a < 5; a++) {
            r.mean_waiting[a] += q.results[j].avg_waiting[a] / jobs;
            checksum = checksum * 31 + (unsigned long)(q.results[j].avg_waiting[a] * 1000);
        }
        for (int a = 0; a < 3; a++) {
            r.mean_faults[a] += (double)q.results[j].page_faults[a] / jobs;
            checksum = checksum * 31 + (unsigned long)q.results[j].page_faults[a];
        }
    }
    r.checksum = checksum;

    pthread_mutex_destroy(&q.lock);
    free(q.results);
    free(tids);
    return r;
}
