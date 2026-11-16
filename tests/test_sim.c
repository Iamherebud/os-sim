/*
 * Unit tests against textbook examples (Silberschatz, "Operating System
 * Concepts"), whose answers are well known. Run with: make test
 */
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "concurrency.h"
#include "memory.h"
#include "scheduler.h"
#include "workload.h"

static int failures = 0, checks = 0;

/* Designated initializers: the scheduler fills in the remaining fields. */
#define P(id, arr, bur, prio) {.pid = (id), .arrival = (arr), .burst = (bur), .priority = (prio)}

#define CHECK(cond, msg) do { checks++; if (!(cond)) { failures++; \
    printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, msg); } } while (0)
#define CHECK_NEAR(a, b, msg) CHECK(fabs((a) - (b)) < 1e-6, msg)

static SchedStats run(SchedAlgo algo, Process *p, int n, int q) {
    Gantt g;
    gantt_init(&g);
    schedule(algo, p, n, q, &g);
    SchedStats s = compute_stats(p, n, &g);
    gantt_free(&g);
    return s;
}

static void test_fcfs(void) {
    /* P1=24, P2=3, P3=3 all at t=0: waits 0, 24, 27 -> avg 17 */
    Process p[] = {P(1, 0, 24, 0), P(2, 0, 3, 0), P(3, 0, 3, 0)};
    SchedStats s = run(FCFS, p, 3, 0);
    CHECK_NEAR(s.avg_waiting, 17.0, "FCFS avg waiting = 17");
    CHECK_NEAR(s.avg_turnaround, 27.0, "FCFS avg turnaround = 27");
    CHECK(s.context_switches == 2, "FCFS 2 context switches");
}

static void test_sjf(void) {
    /* bursts 6, 8, 7, 3 at t=0 -> order P4 P1 P3 P2 -> avg wait 7 */
    Process p[] = {P(1, 0, 6, 0), P(2, 0, 8, 0), P(3, 0, 7, 0), P(4, 0, 3, 0)};
    SchedStats s = run(SJF, p, 4, 0);
    CHECK_NEAR(s.avg_waiting, 7.0, "SJF avg waiting = 7");
    CHECK(p[3].start == 0 && p[1].finish == 24, "SJF order P4 first, P2 last");
}

static void test_srtf(void) {
    /* (arrival, burst): P1(0,8) P2(1,4) P3(2,9) P4(3,5) -> avg wait 6.5 */
    Process p[] = {P(1, 0, 8, 0), P(2, 1, 4, 0), P(3, 2, 9, 0), P(4, 3, 5, 0)};
    SchedStats s = run(SRTF, p, 4, 0);
    CHECK_NEAR(s.avg_waiting, 6.5, "SRTF avg waiting = 6.5");
    CHECK(p[0].finish == 17 && p[1].finish == 5 && p[3].finish == 10 && p[2].finish == 26,
          "SRTF finish times 17, 5, 26, 10");
}

static void test_rr(void) {
    /* q=4: P1=24, P2=3, P3=3 -> waits 6, 4, 7 -> avg 17/3 */
    Process p[] = {P(1, 0, 24, 0), P(2, 0, 3, 0), P(3, 0, 3, 0)};
    SchedStats s = run(RR, p, 3, 4);
    CHECK_NEAR(s.avg_waiting, 17.0 / 3.0, "RR(q=4) avg waiting = 5.67");
    CHECK(p[1].finish == 7 && p[2].finish == 10 && p[0].finish == 30, "RR finish times");

    /* Arrivals during a slice are queued BEFORE the preempted process. */
    Process r[] = {P(1, 0, 5, 0), P(2, 1, 3, 0)};
    run(RR, r, 2, 2);
    /* P1 0-2, P2 2-4, P1 4-6, P2 6-7, P1 7-8 */
    CHECK(r[1].finish == 7 && r[0].finish == 8, "RR queue order with arrivals");
}

static void test_priority(void) {
    /* (burst, priority): P1(10,3) P2(1,1) P3(2,4) P4(1,5) P5(5,2) -> avg 8.2 */
    Process p[] = {P(1, 0, 10, 3), P(2, 0, 1, 1), P(3, 0, 2, 4), P(4, 0, 1, 5), P(5, 0, 5, 2)};
    SchedStats s = run(PRIORITY, p, 5, 0);
    CHECK_NEAR(s.avg_waiting, 8.2, "Priority avg waiting = 8.2");
}

static void test_idle_cpu(void) {
    /* Gap between processes: CPU idles 3-5. */
    Process p[] = {P(1, 0, 3, 0), P(2, 5, 2, 0)};
    Gantt g;
    gantt_init(&g);
    schedule(FCFS, p, 2, 0, &g);
    SchedStats s = compute_stats(p, 2, &g);
    CHECK(g.count == 3 && g.slices[1].pid == -1, "idle slice recorded");
    CHECK_NEAR(s.cpu_utilization, 100.0 * 5 / 7, "CPU utilization with idle time");
    gantt_free(&g);
}

static void test_paging(void) {
    int refs[] = {7, 0, 1, 2, 0, 3, 0, 4, 2, 3, 0, 3, 2, 1, 2, 0, 1, 7, 0, 1};
    int n = sizeof refs / sizeof refs[0];
    CHECK(simulate_paging(PR_FIFO, refs, n, 3, 0).faults == 15, "FIFO 15 faults");
    CHECK(simulate_paging(PR_LRU, refs, n, 3, 0).faults == 12, "LRU 12 faults");
    CHECK(simulate_paging(PR_OPT, refs, n, 3, 0).faults == 9, "OPT 9 faults");
    PageStats s = simulate_paging(PR_LRU, refs, n, 3, 0);
    CHECK(s.faults + s.hits == n, "faults + hits = references");
}

static void test_belady(void) {
    int refs[] = {1, 2, 3, 4, 1, 2, 5, 1, 2, 3, 4, 5};
    int n = sizeof refs / sizeof refs[0];
    CHECK(simulate_paging(PR_FIFO, refs, n, 3, 0).faults == 9, "FIFO 3 frames: 9 faults");
    CHECK(simulate_paging(PR_FIFO, refs, n, 4, 0).faults == 10, "FIFO 4 frames: 10 faults (anomaly)");
    CHECK(simulate_paging(PR_LRU, refs, n, 4, 0).faults <=
          simulate_paging(PR_LRU, refs, n, 3, 0).faults, "LRU has no anomaly");
}

static void test_opt_is_best(void) {
    static int refs[20000];
    Rng rng;
    rng_seed(&rng, 99);
    random_references(&rng, refs, 20000, 64);
    int opt = simulate_paging(PR_OPT, refs, 20000, 8, 0).faults;
    CHECK(opt <= simulate_paging(PR_LRU, refs, 20000, 8, 0).faults, "OPT <= LRU");
    CHECK(opt <= simulate_paging(PR_FIFO, refs, 20000, 8, 0).faults, "OPT <= FIFO");
}

static void test_ipc_matches_sequential(void) {
    static int refs[50000];
    Rng rng;
    rng_seed(&rng, 5);
    random_references(&rng, refs, 50000, 128);
    int par[3];
    double secs;
    CHECK(run_ipc(refs, 50000, 16, par, &secs) == 0, "run_ipc succeeds");
    for (int a = 0; a < 3; a++)
        CHECK(par[a] == simulate_paging((PageAlgo)a, refs, 50000, 16, 0).faults,
              "child process result matches in-process result");
}

static void test_threads_deterministic(void) {
    BenchResult a = run_bench(12, 1);
    BenchResult b = run_bench(12, 4);
    CHECK(a.checksum == b.checksum, "1 thread and 4 threads give identical results");
}

int main(void) {
    test_fcfs();
    test_sjf();
    test_srtf();
    test_rr();
    test_priority();
    test_idle_cpu();
    test_paging();
    test_belady();
    test_opt_is_best();
    test_ipc_matches_sequential();
    test_threads_deterministic();
    printf("%d/%d checks passed\n", checks - failures, checks);
    return failures ? 1 : 0;
}
