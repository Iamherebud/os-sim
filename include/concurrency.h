/*
 * concurrency.h - Running many simulations in parallel.
 *
 * bench: multithreading with POSIX threads. A shared work queue (just a
 *        "next job" counter protected by a mutex) hands out simulation
 *        jobs to worker threads.
 * ipc:   multiple PROCESSES with fork(). The parent sends a page reference
 *        string to each child through a pipe; each child runs one page
 *        replacement algorithm and sends its result back through another
 *        pipe.
 */
#ifndef CONCURRENCY_H
#define CONCURRENCY_H

typedef struct {
    double avg_waiting[5];      /* per scheduling algorithm  */
    int page_faults[3];         /* per page algorithm        */
} JobResult;

typedef struct {
    int jobs;
    int threads;
    double seconds;
    unsigned long checksum;     /* same for any thread count => no races */
    double mean_waiting[5];
    double mean_faults[3];
} BenchResult;

/* Run `jobs` independent simulation jobs on `threads` worker threads. */
BenchResult run_bench(int jobs, int threads);

/* Run FIFO/LRU/OPT in 3 child processes connected by pipes.
 * Returns 0 on success. Results written to faults_out[3]. */
int run_ipc(const int *refs, int n, int frames, int faults_out[3], double *seconds_out);

double now_seconds(void);

#endif
