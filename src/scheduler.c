/*
 * scheduler.c - FCFS, SJF, SRTF, Round Robin and Priority scheduling.
 *
 * Non-preemptive (FCFS, SJF, PRIORITY): when the CPU is free, pick one
 *     ready process by some rule and run it until it finishes.
 * Preemptive (SRTF, RR): the running process can be interrupted, either
 *     by a shorter job arriving (SRTF) or by its time slice ending (RR).
 */
#include "scheduler.h"

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>

const char *sched_algo_name(SchedAlgo algo) {
    switch (algo) {
        case FCFS:     return "FCFS";
        case SJF:      return "SJF";
        case SRTF:     return "SRTF";
        case RR:       return "Round Robin";
        case PRIORITY: return "Priority";
        default:       return "?";
    }
}

/* ------------------------------------------------------------------ */
/* Gantt chart helpers                                                 */
/* ------------------------------------------------------------------ */

void gantt_init(Gantt *g) {
    g->count = 0;
    g->capacity = 16;
    g->slices = malloc(sizeof(Slice) * g->capacity);
    if (!g->slices) { perror("malloc"); exit(1); }
}

void gantt_free(Gantt *g) {
    free(g->slices);
    g->slices = NULL;
    g->count = g->capacity = 0;
}

/* Append [start, end) for pid, merging with the previous slice if it's the
 * same pid and contiguous (keeps SRTF's tick-by-tick output readable). */
static void gantt_add(Gantt *g, int pid, int start, int end) {
    if (end <= start) return;
    if (g->count > 0) {
        Slice *last = &g->slices[g->count - 1];
        if (last->pid == pid && last->end == start) {
            last->end = end;
            return;
        }
    }
    if (g->count == g->capacity) {
        g->capacity *= 2;
        Slice *bigger = realloc(g->slices, sizeof(Slice) * g->capacity);
        if (!bigger) { perror("realloc"); exit(1); }
        g->slices = bigger;
    }
    g->slices[g->count++] = (Slice){pid, start, end};
}

/* ------------------------------------------------------------------ */
/* Shared helpers                                                      */
/* ------------------------------------------------------------------ */

static void reset(Process *p, int n) {
    for (int i = 0; i < n; i++) {
        p[i].remaining = p[i].burst;
        p[i].start = -1;
        p[i].finish = -1;
    }
}

/* Earliest arrival among unfinished processes (used to skip idle time). */
static int next_arrival(const Process *p, int n) {
    int t = INT_MAX;
    for (int i = 0; i < n; i++)
        if (p[i].remaining > 0 && p[i].arrival < t) t = p[i].arrival;
    return t;
}

/* Is candidate `a` a better pick than current best `b` under `algo`?
 * Ties are broken by arrival time, then pid, so results are deterministic. */
static int better(SchedAlgo algo, const Process *a, const Process *b) {
    int ka, kb;
    switch (algo) {
        case SJF:      ka = a->burst;     kb = b->burst;     break;
        case SRTF:     ka = a->remaining; kb = b->remaining; break;
        case PRIORITY: ka = a->priority;  kb = b->priority;  break;
        default:       ka = kb = 0;       break;  /* FCFS: arrival only */
    }
    if (ka != kb) return ka < kb;
    if (a->arrival != b->arrival) return a->arrival < b->arrival;
    return a->pid < b->pid;
}

/* Index of the best ready process at time t, or -1 if none is ready. */
static int pick(SchedAlgo algo, const Process *p, int n, int t) {
    int best = -1;
    for (int i = 0; i < n; i++) {
        if (p[i].remaining == 0 || p[i].arrival > t) continue;
        if (best == -1 || better(algo, &p[i], &p[best])) best = i;
    }
    return best;
}

/* ------------------------------------------------------------------ */
/* Non-preemptive: FCFS, SJF, PRIORITY                                 */
/* ------------------------------------------------------------------ */

static void run_non_preemptive(SchedAlgo algo, Process *p, int n, Gantt *g) {
    int t = 0, done = 0;
    while (done < n) {
        int i = pick(algo, p, n, t);
        if (i == -1) {                       /* nobody ready: CPU idles */
            int next = next_arrival(p, n);
            gantt_add(g, -1, t, next);
            t = next;
            continue;
        }
        p[i].start = t;
        gantt_add(g, p[i].pid, t, t + p[i].burst);
        t += p[i].burst;
        p[i].remaining = 0;
        p[i].finish = t;
        done++;
    }
}

/* ------------------------------------------------------------------ */
/* SRTF: Shortest Remaining Time First (preemptive SJF)                */
/* Re-decide every tick: whoever has the least work left runs.         */
/* ------------------------------------------------------------------ */

static void run_srtf(Process *p, int n, Gantt *g) {
    int t = 0, done = 0;
    while (done < n) {
        int i = pick(SRTF, p, n, t);
        if (i == -1) {
            int next = next_arrival(p, n);
            gantt_add(g, -1, t, next);
            t = next;
            continue;
        }
        if (p[i].start == -1) p[i].start = t;
        gantt_add(g, p[i].pid, t, t + 1);
        p[i].remaining--;
        t++;
        if (p[i].remaining == 0) {
            p[i].finish = t;
            done++;
        }
    }
}

/* ------------------------------------------------------------------ */
/* Round Robin                                                         */
/* FIFO ready queue; each process runs for at most `quantum`, then     */
/* goes to the back of the queue. Convention (as in Silberschatz):     */
/* processes that ARRIVE during a slice are queued before the process  */
/* that just got preempted.                                            */
/* ------------------------------------------------------------------ */

static int by_arrival(const void *a, const void *b) {
    const Process *x = *(const Process *const *)a, *y = *(const Process *const *)b;
    if (x->arrival != y->arrival) return x->arrival - y->arrival;
    return x->pid - y->pid;
}

static void run_rr(Process *p, int n, int quantum, Gantt *g) {
    /* Circular queue of indices. Each process is in the queue at most once,
     * so capacity n is enough. */
    int *queue = malloc(sizeof(int) * n);
    const Process **order = malloc(sizeof(Process *) * n);
    if (!queue || !order) { perror("malloc"); exit(1); }

    for (int i = 0; i < n; i++) order[i] = &p[i];
    qsort(order, n, sizeof(Process *), by_arrival);

    int head = 0, size = 0;          /* queue state                  */
    int next_in = 0;                 /* next process (by arrival) to enqueue */
    int t = 0, done = 0;

#define ENQUEUE(idx) do { queue[(head + size) % n] = (idx); size++; } while (0)
#define ADMIT_ARRIVALS_UP_TO(time) \
    while (next_in < n && order[next_in]->arrival <= (time)) { \
        ENQUEUE((int)(order[next_in] - p)); next_in++; }

    ADMIT_ARRIVALS_UP_TO(t);
    while (done < n) {
        if (size == 0) {                       /* idle until next arrival */
            int next = order[next_in]->arrival;
            gantt_add(g, -1, t, next);
            t = next;
            ADMIT_ARRIVALS_UP_TO(t);
            continue;
        }
        int i = queue[head];
        head = (head + 1) % n;
        size--;

        if (p[i].start == -1) p[i].start = t;
        int run = p[i].remaining < quantum ? p[i].remaining : quantum;
        gantt_add(g, p[i].pid, t, t + run);
        t += run;
        p[i].remaining -= run;

        ADMIT_ARRIVALS_UP_TO(t);               /* arrivals first ...       */
        if (p[i].remaining > 0) {
            ENQUEUE(i);                        /* ... then the preempted one */
        } else {
            p[i].finish = t;
            done++;
        }
    }
#undef ENQUEUE
#undef ADMIT_ARRIVALS_UP_TO
    free(queue);
    free(order);
}

/* ------------------------------------------------------------------ */
/* Public API                                                          */
/* ------------------------------------------------------------------ */

void schedule(SchedAlgo algo, Process *procs, int n, int quantum, Gantt *gantt) {
    reset(procs, n);
    if (n == 0) return;
    switch (algo) {
        case FCFS:
        case SJF:
        case PRIORITY: run_non_preemptive(algo, procs, n, gantt); break;
        case SRTF:     run_srtf(procs, n, gantt);                 break;
        case RR:       run_rr(procs, n, quantum > 0 ? quantum : 1, gantt); break;
        default:       break;
    }
}

SchedStats compute_stats(const Process *p, int n, const Gantt *g) {
    SchedStats s = {0};
    if (n == 0) return s;

    long waiting = 0, turnaround = 0, response = 0;
    int first_arrival = INT_MAX;
    for (int i = 0; i < n; i++) {
        int tat = p[i].finish - p[i].arrival;
        turnaround += tat;
        waiting += tat - p[i].burst;
        response += p[i].start - p[i].arrival;
        if (p[i].finish > s.makespan) s.makespan = p[i].finish;
        if (p[i].arrival < first_arrival) first_arrival = p[i].arrival;
    }
    s.avg_waiting = (double)waiting / n;
    s.avg_turnaround = (double)turnaround / n;
    s.avg_response = (double)response / n;

    long busy = 0;
    int last_pid = -1;
    for (int k = 0; k < g->count; k++) {
        if (g->slices[k].pid == -1) continue;
        busy += g->slices[k].end - g->slices[k].start;
        if (last_pid != -1 && g->slices[k].pid != last_pid) s.context_switches++;
        last_pid = g->slices[k].pid;
    }
    /* Measure from the first arrival: time before anything arrived isn't
     * the scheduler's fault. */
    int span = s.makespan - first_arrival;
    s.cpu_utilization = span > 0 ? 100.0 * busy / span : 0.0;
    s.throughput = span > 0 ? (double)n / span : 0.0;
    return s;
}

void print_gantt(const Gantt *g) {
    printf("Gantt: ");
    for (int k = 0; k < g->count; k++) {
        const Slice *s = &g->slices[k];
        if (s->pid == -1) printf("| idle %d-%d ", s->start, s->end);
        else              printf("| P%d %d-%d ", s->pid, s->start, s->end);
    }
    printf("|\n");
}

void print_process_table(const Process *p, int n) {
    printf("  PID  Arrival  Burst  Prio  Start  Finish  Waiting  Turnaround  Response\n");
    for (int i = 0; i < n; i++) {
        int tat = p[i].finish - p[i].arrival;
        printf("  P%-3d %7d %6d %5d %6d %7d %8d %11d %9d\n",
               p[i].pid, p[i].arrival, p[i].burst, p[i].priority,
               p[i].start, p[i].finish, tat - p[i].burst, tat,
               p[i].start - p[i].arrival);
    }
}
