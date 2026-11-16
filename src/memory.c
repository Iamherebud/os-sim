/*
 * memory.c - FIFO, LRU and Optimal (Belady's) page replacement.
 *
 * FIFO: evict the page that has been in memory the longest.
 *       Simple, but suffers from Belady's anomaly (more frames can mean
 *       MORE faults).
 * LRU:  evict the page that was used least recently. Good approximation
 *       of OPT because programs have temporal locality.
 * OPT:  evict the page that won't be used for the longest time in the
 *       future. Impossible in a real OS (needs the future), but it's the
 *       theoretical best, so it's used as a benchmark.
 */
#include "memory.h"

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>

const char *page_algo_name(PageAlgo algo) {
    switch (algo) {
        case PR_FIFO: return "FIFO";
        case PR_LRU:  return "LRU";
        case PR_OPT:  return "Optimal";
        default:      return "?";
    }
}

static int find_frame(const int *frame_page, int frames, int page) {
    for (int f = 0; f < frames; f++)
        if (frame_page[f] == page) return f;
    return -1;
}

static void print_frames(const int *frame_page, int frames, int page, int fault) {
    printf("  ref %3d: [", page);
    for (int f = 0; f < frames; f++) {
        if (frame_page[f] == -1) printf("  .");
        else                     printf(" %2d", frame_page[f]);
    }
    printf(" ]%s\n", fault ? "  <- fault" : "");
}

/*
 * OPT needs "when is this page used next?". Scanning the future on every
 * fault is O(n) per fault -> O(n^2) total. Instead, precompute
 * next_use[i] = index of the next reference to refs[i] after position i
 * (or INT_MAX if never), in one backwards pass. Then OPT is O(n * frames).
 */
static int *compute_next_use(const int *refs, int n) {
    int max_page = 0;
    for (int i = 0; i < n; i++)
        if (refs[i] > max_page) max_page = refs[i];

    int *next_use = malloc(sizeof(int) * n);
    int *seen_at = malloc(sizeof(int) * (max_page + 1));
    if (!next_use || !seen_at) { perror("malloc"); exit(1); }

    for (int p = 0; p <= max_page; p++) seen_at[p] = INT_MAX;
    for (int i = n - 1; i >= 0; i--) {
        next_use[i] = seen_at[refs[i]];
        seen_at[refs[i]] = i;
    }
    free(seen_at);
    return next_use;
}

PageStats simulate_paging(PageAlgo algo, const int *refs, int n, int frames, int trace) {
    PageStats stats = {0, 0};
    if (frames <= 0 || n <= 0) return stats;

    int *frame_page = malloc(sizeof(int) * frames);  /* which page is in frame f   */
    long *frame_info = malloc(sizeof(long) * frames);/* FIFO: load time
                                                        LRU:  last use time
                                                        OPT:  next use index      */
    if (!frame_page || !frame_info) { perror("malloc"); exit(1); }
    for (int f = 0; f < frames; f++) { frame_page[f] = -1; frame_info[f] = 0; }

    int *next_use = (algo == PR_OPT) ? compute_next_use(refs, n) : NULL;
    int used = 0;                                     /* frames filled so far */

    for (int i = 0; i < n; i++) {
        int page = refs[i];
        int f = find_frame(frame_page, frames, page);

        if (f != -1) {                                /* ---- HIT ---- */
            stats.hits++;
            if (algo == PR_LRU) frame_info[f] = i;
            if (algo == PR_OPT) frame_info[f] = next_use[i];
            if (trace) print_frames(frame_page, frames, page, 0);
            continue;
        }

        stats.faults++;                               /* ---- FAULT ---- */
        int victim;
        if (used < frames) {
            victim = used++;                          /* free frame available */
        } else {
            victim = 0;
            for (int k = 1; k < frames; k++) {
                if (algo == PR_OPT) {
                    /* evict the page used FURTHEST in the future */
                    if (frame_info[k] > frame_info[victim]) victim = k;
                } else {
                    /* FIFO: oldest load time. LRU: oldest use time. */
                    if (frame_info[k] < frame_info[victim]) victim = k;
                }
            }
        }
        frame_page[victim] = page;
        frame_info[victim] = (algo == PR_OPT) ? next_use[i] : i;
        if (trace) print_frames(frame_page, frames, page, 1);
    }

    free(frame_page);
    free(frame_info);
    free(next_use);
    return stats;
}
