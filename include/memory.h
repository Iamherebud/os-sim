/*
 * memory.h - Page replacement simulation (virtual memory / demand paging).
 *
 * A process references pages in some order (the "reference string").
 * Physical memory has a fixed number of frames. When a referenced page is
 * not in any frame we get a PAGE FAULT and must load it, evicting another
 * page if all frames are full. The algorithm decides WHICH page to evict.
 */
#ifndef MEMORY_H
#define MEMORY_H

typedef enum { PR_FIFO, PR_LRU, PR_OPT, PAGE_ALGO_COUNT } PageAlgo;

typedef struct {
    int faults;
    int hits;
} PageStats;

const char *page_algo_name(PageAlgo algo);

/*
 * Simulate `algo` on refs[0..n-1] with `frames` physical frames.
 * If `trace` is non-zero, print the frame contents after every reference.
 */
PageStats simulate_paging(PageAlgo algo, const int *refs, int n, int frames, int trace);

#endif
