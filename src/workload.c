#include "workload.h"

#include <stdio.h>

/* xorshift32: tiny, fast, good enough for simulations (not crypto!). */
void rng_seed(Rng *r, uint32_t seed) {
    r->state = seed ? seed : 0x9E3779B9u;   /* state must never be 0 */
}

uint32_t rng_next(Rng *r) {
    uint32_t x = r->state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    r->state = x;
    return x;
}

int rng_range(Rng *r, int lo, int hi) {
    return lo + (int)(rng_next(r) % (uint32_t)(hi - lo + 1));
}

void random_processes(Rng *r, Process *out, int n, int max_arrival, int max_burst) {
    for (int i = 0; i < n; i++) {
        out[i].pid = i + 1;
        out[i].arrival = rng_range(r, 0, max_arrival);
        out[i].burst = rng_range(r, 1, max_burst);
        out[i].priority = rng_range(r, 1, 10);
    }
}

void random_references(Rng *r, int *out, int n, int num_pages) {
    int working_set = num_pages / 16 > 2 ? num_pages / 16 : 2;
    int base = 0;
    for (int i = 0; i < n; i++) {
        if (rng_range(r, 1, 100) <= 3)            /* 3%: jump to a new region */
            base = rng_range(r, 0, num_pages - working_set);
        if (rng_range(r, 1, 100) <= 10)           /* 10%: random far access   */
            out[i] = rng_range(r, 0, num_pages - 1);
        else
            out[i] = base + rng_range(r, 0, working_set - 1);
    }
}

int load_processes(const char *path, Process *out, int max_n) {
    FILE *fp = fopen(path, "r");
    if (!fp) { perror(path); return -1; }

    char line[256];
    int n = 0;
    while (n < max_n && fgets(line, sizeof line, fp)) {
        if (line[0] == '#' || line[0] == '\n') continue;     /* comments */
        Process p = {0};
        if (sscanf(line, "%d %d %d %d", &p.pid, &p.arrival, &p.burst, &p.priority) < 3) {
            fprintf(stderr, "Bad line in %s: %s", path, line);
            fclose(fp);
            return -1;
        }
        out[n++] = p;
    }
    fclose(fp);
    return n;
}
