/*
 * ossim - CPU scheduling and memory management simulator.
 *
 * Usage:
 *   ./ossim sched  [--algo fcfs|sjf|srtf|rr|priority|all] [--quantum 4]
 *                  [--file workload.txt | --random 8 --seed 42]
 *   ./ossim mem    [--algo fifo|lru|opt|all] [--frames 3]
 *                  [--refs "7 0 1 2 0 3"] [--random 20 --pages 10 --seed 1] [--trace]
 *   ./ossim belady                    # demonstrate Belady's anomaly
 *   ./ossim ipc    [--random 2000000 --frames 64]
 *   ./ossim bench  [--jobs 400 --threads 4]
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "concurrency.h"
#include "memory.h"
#include "scheduler.h"
#include "workload.h"

#define MAX_PROCS 1000
#define MAX_REFS 5000000

/* ---------- tiny argument helpers ---------- */

static const char *opt_str(int argc, char **argv, const char *name, const char *def) {
    for (int i = 2; i < argc - 1; i++)
        if (strcmp(argv[i], name) == 0) return argv[i + 1];
    return def;
}

static int opt_int(int argc, char **argv, const char *name, int def) {
    const char *s = opt_str(argc, argv, name, NULL);
    return s ? atoi(s) : def;
}

static int has_flag(int argc, char **argv, const char *name) {
    for (int i = 2; i < argc; i++)
        if (strcmp(argv[i], name) == 0) return 1;
    return 0;
}

/* Returns index of algorithm name in names[], -1 for "all", -2 if unknown. */
static int parse_algo(const char *s, const char *const *names, int count) {
    if (strcmp(s, "all") == 0) return -1;
    for (int i = 0; i < count; i++)
        if (strcmp(s, names[i]) == 0) return i;
    return -2;
}

/* ---------- commands ---------- */

static int cmd_sched(int argc, char **argv) {
    static const char *const names[] = {"fcfs", "sjf", "srtf", "rr", "priority"};
    int algo = parse_algo(opt_str(argc, argv, "--algo", "all"), names, 5);
    if (algo == -2) { fprintf(stderr, "unknown --algo\n"); return 1; }
    int quantum = opt_int(argc, argv, "--quantum", 4);

    Process base[MAX_PROCS];
    int n;
    const char *file = opt_str(argc, argv, "--file", NULL);
    if (file) {
        n = load_processes(file, base, MAX_PROCS);
        if (n <= 0) return 1;
    } else {
        Rng rng;
        rng_seed(&rng, (unsigned)opt_int(argc, argv, "--seed", 42));
        n = opt_int(argc, argv, "--random", 6);
        if (n < 1 || n > MAX_PROCS) { fprintf(stderr, "--random must be 1..%d\n", MAX_PROCS); return 1; }
        random_processes(&rng, base, n, n * 3, 10);
    }

    printf("%-12s %9s %11s %9s %8s %9s\n", "Algorithm", "AvgWait", "AvgTurnar.",
           "AvgResp", "CPU%", "CtxSw");
    for (int a = 0; a < SCHED_ALGO_COUNT; a++) {
        if (algo != -1 && algo != a) continue;
        Process p[MAX_PROCS];
        memcpy(p, base, sizeof(Process) * n);       /* each algo gets a fresh copy */
        Gantt g;
        gantt_init(&g);
        schedule((SchedAlgo)a, p, n, quantum, &g);
        SchedStats s = compute_stats(p, n, &g);
        printf("%-12s %9.2f %11.2f %9.2f %7.1f%% %9d\n", sched_algo_name(a),
               s.avg_waiting, s.avg_turnaround, s.avg_response, s.cpu_utilization,
               s.context_switches);
        if (n <= 20) {                               /* details for small inputs */
            print_gantt(&g);
            print_process_table(p, n);
            printf("\n");
        }
        gantt_free(&g);
    }
    return 0;
}

static int parse_refs(const char *s, int *out, int max_n) {
    int n = 0;
    char *copy = strdup(s), *save = NULL;
    for (char *tok = strtok_r(copy, " ,", &save); tok && n < max_n; tok = strtok_r(NULL, " ,", &save))
        out[n++] = atoi(tok);
    free(copy);
    return n;
}

static int cmd_mem(int argc, char **argv) {
    static const char *const names[] = {"fifo", "lru", "opt"};
    int algo = parse_algo(opt_str(argc, argv, "--algo", "all"), names, 3);
    if (algo == -2) { fprintf(stderr, "unknown --algo\n"); return 1; }
    int frames = opt_int(argc, argv, "--frames", 3);
    int trace = has_flag(argc, argv, "--trace");

    static int refs[MAX_REFS];
    int n;
    const char *ref_str = opt_str(argc, argv, "--refs", NULL);
    if (ref_str) {
        n = parse_refs(ref_str, refs, MAX_REFS);
    } else if (opt_str(argc, argv, "--random", NULL)) {
        Rng rng;
        rng_seed(&rng, (unsigned)opt_int(argc, argv, "--seed", 1));
        n = opt_int(argc, argv, "--random", 20);
        if (n < 1 || n > MAX_REFS) { fprintf(stderr, "--random too large\n"); return 1; }
        random_references(&rng, refs, n, opt_int(argc, argv, "--pages", 10));
    } else {
        /* Classic textbook reference string (Silberschatz). */
        n = parse_refs("7 0 1 2 0 3 0 4 2 3 0 3 2 1 2 0 1 7 0 1", refs, MAX_REFS);
    }

    printf("References: %d, frames: %d\n", n, frames);
    for (int a = 0; a < PAGE_ALGO_COUNT; a++) {
        if (algo != -1 && algo != a) continue;
        if (trace) printf("\n%s trace:\n", page_algo_name(a));
        PageStats s = simulate_paging((PageAlgo)a, refs, n, frames, trace && n <= 100);
        printf("%-8s faults=%-8d hits=%-8d hit ratio=%.1f%%\n", page_algo_name(a),
               s.faults, s.hits, 100.0 * s.hits / n);
    }
    return 0;
}

static int cmd_belady(void) {
    int refs[] = {1, 2, 3, 4, 1, 2, 5, 1, 2, 3, 4, 5};
    int n = sizeof refs / sizeof refs[0];
    printf("Belady's anomaly: FIFO can get MORE faults with MORE frames.\n");
    printf("Reference string: 1 2 3 4 1 2 5 1 2 3 4 5\n");
    for (int frames = 1; frames <= 5; frames++) {
        printf("  frames=%d  FIFO faults=%2d   LRU faults=%2d\n", frames,
               simulate_paging(PR_FIFO, refs, n, frames, 0).faults,
               simulate_paging(PR_LRU, refs, n, frames, 0).faults);
    }
    printf("LRU is a 'stack algorithm', so it never shows the anomaly.\n");
    return 0;
}

static int cmd_ipc(int argc, char **argv) {
    int n = opt_int(argc, argv, "--random", 2000000);
    int frames = opt_int(argc, argv, "--frames", 64);
    if (n < 1 || n > MAX_REFS) { fprintf(stderr, "--random must be 1..%d\n", MAX_REFS); return 1; }

    static int refs[MAX_REFS];
    Rng rng;
    rng_seed(&rng, 7);
    random_references(&rng, refs, n, 1024);

    /* Sequential, in this process. */
    double t0 = now_seconds();
    int seq[3];
    for (int a = 0; a < PAGE_ALGO_COUNT; a++)
        seq[a] = simulate_paging((PageAlgo)a, refs, n, frames, 0).faults;
    double seq_time = now_seconds() - t0;

    /* Parallel, one child process per algorithm, talking through pipes. */
    int par[3];
    double par_time;
    if (run_ipc(refs, n, frames, par, &par_time) != 0) return 1;

    printf("Page replacement on %d references, %d frames (parent pid %d)\n", n, frames, (int)getpid());
    printf("%-8s %14s %18s\n", "Algo", "faults (1 proc)", "faults (3 procs)");
    for (int a = 0; a < PAGE_ALGO_COUNT; a++)
        printf("%-8s %14d %18d %s\n", page_algo_name(a), seq[a], par[a],
               seq[a] == par[a] ? "match" : "MISMATCH!");
    printf("Sequential: %.3fs | fork+pipes: %.3fs | speedup %.2fx\n",
           seq_time, par_time, seq_time / par_time);
    return 0;
}

static int cmd_bench(int argc, char **argv) {
    int jobs = opt_int(argc, argv, "--jobs", 400);
    long cores = sysconf(_SC_NPROCESSORS_ONLN);
    int threads = opt_int(argc, argv, "--threads", cores > 0 ? (int)cores : 4);
    if (jobs < 1 || threads < 1) { fprintf(stderr, "--jobs and --threads must be >= 1\n"); return 1; }

    printf("Running %d simulation jobs (each: 400 processes x 5 schedulers + "
           "30k page refs x 3 algorithms)\n", jobs);
    printf("CPU cores available: %ld\n\n", cores);

    BenchResult one = run_bench(jobs, 1);
    printf("1 thread : %.3fs  checksum %lu\n", one.seconds, one.checksum);
    BenchResult many = run_bench(jobs, threads);
    printf("%d threads: %.3fs  checksum %lu  %s\n", threads, many.seconds, many.checksum,
           one.checksum == many.checksum ? "(identical results)" : "(RESULTS DIFFER - RACE!)");

    double speedup = one.seconds / many.seconds;
    printf("\nSpeedup: %.2fx  (time saved: %.1f%%)\n", speedup,
           100.0 * (1.0 - many.seconds / one.seconds));

    printf("\nMean avg-waiting time per scheduler across all jobs:\n");
    for (int a = 0; a < SCHED_ALGO_COUNT; a++)
        printf("  %-12s %8.2f\n", sched_algo_name(a), many.mean_waiting[a]);
    printf("Mean page faults per algorithm (16 frames):\n");
    for (int a = 0; a < PAGE_ALGO_COUNT; a++)
        printf("  %-12s %8.1f\n", page_algo_name(a), many.mean_faults[a]);
    return one.checksum == many.checksum ? 0 : 1;
}

static void usage(void) {
    printf("ossim - CPU scheduling & memory management simulator\n\n"
           "  ossim sched  [--algo fcfs|sjf|srtf|rr|priority|all] [--quantum Q]\n"
           "               [--file workload.txt | --random N --seed S]\n"
           "  ossim mem    [--algo fifo|lru|opt|all] [--frames F] [--trace]\n"
           "               [--refs \"7 0 1 2\" | --random N --pages P --seed S]\n"
           "  ossim belady\n"
           "  ossim ipc    [--random N] [--frames F]\n"
           "  ossim bench  [--jobs J] [--threads T]\n");
}

int main(int argc, char **argv) {
    if (argc < 2) { usage(); return 1; }
    const char *cmd = argv[1];
    if (strcmp(cmd, "sched") == 0)  return cmd_sched(argc, argv);
    if (strcmp(cmd, "mem") == 0)    return cmd_mem(argc, argv);
    if (strcmp(cmd, "belady") == 0) return cmd_belady();
    if (strcmp(cmd, "ipc") == 0)    return cmd_ipc(argc, argv);
    if (strcmp(cmd, "bench") == 0)  return cmd_bench(argc, argv);
    usage();
    return 1;
}
