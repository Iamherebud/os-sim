/*
 * workload.h - Random workloads (processes and page reference strings).
 *
 * Uses our own small RNG with explicit state instead of rand(), because
 * rand() keeps hidden global state and is NOT thread-safe. Each thread
 * gets its own Rng, so results are reproducible for a given seed no matter
 * how many threads run.
 */
#ifndef WORKLOAD_H
#define WORKLOAD_H

#include <stdint.h>

#include "scheduler.h"

typedef struct { uint32_t state; } Rng;

void rng_seed(Rng *r, uint32_t seed);
uint32_t rng_next(Rng *r);
int rng_range(Rng *r, int lo, int hi);   /* inclusive range [lo, hi] */

/* n processes with random arrival (0..max_arrival), burst and priority. */
void random_processes(Rng *r, Process *out, int n, int max_arrival, int max_burst);

/*
 * Page reference string with LOCALITY: the program works on a small
 * "working set" of pages for a while, then moves to another region.
 * (Uniformly random references would make every algorithm look the same.)
 */
void random_references(Rng *r, int *out, int n, int num_pages);

/* Load "pid arrival burst priority" lines. Returns count, or -1 on error. */
int load_processes(const char *path, Process *out, int max_n);

#endif
