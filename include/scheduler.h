/*
 * scheduler.h - CPU scheduling algorithms.
 *
 * All times are in abstract "time units" (ticks).
 */
#ifndef SCHEDULER_H
#define SCHEDULER_H

typedef struct {
    int pid;
    int arrival;     /* when the process enters the ready queue        */
    int burst;       /* total CPU time it needs                         */
    int priority;    /* lower number = higher priority                  */

    /* Filled in by the scheduler: */
    int remaining;   /* CPU time still needed                           */
    int start;       /* first time it got the CPU (-1 = not yet)        */
    int finish;      /* completion time                                 */
} Process;

/* One block of the Gantt chart: pid ran from start to end. pid -1 = idle. */
typedef struct {
    int pid;
    int start;
    int end;
} Slice;

typedef struct {
    Slice *slices;
    int count;
    int capacity;
} Gantt;

typedef struct {
    double avg_waiting;      /* time spent in the ready queue           */
    double avg_turnaround;   /* finish - arrival                        */
    double avg_response;     /* first run - arrival                     */
    double cpu_utilization;  /* busy time / total time, in %            */
    double throughput;       /* processes finished per time unit        */
    int context_switches;    /* CPU switched from one process to another */
    int makespan;            /* time when the last process finished      */
} SchedStats;

typedef enum { FCFS, SJF, SRTF, RR, PRIORITY, SCHED_ALGO_COUNT } SchedAlgo;

const char *sched_algo_name(SchedAlgo algo);

void gantt_init(Gantt *g);
void gantt_free(Gantt *g);

/*
 * Run `algo` on procs[0..n-1]. procs is modified in place (remaining, start,
 * finish are filled in), so pass a copy if you want to keep the original.
 * `quantum` is only used by Round Robin.
 */
void schedule(SchedAlgo algo, Process *procs, int n, int quantum, Gantt *gantt);

SchedStats compute_stats(const Process *procs, int n, const Gantt *gantt);

void print_gantt(const Gantt *g);
void print_process_table(const Process *procs, int n);

#endif
