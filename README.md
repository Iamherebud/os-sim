# ossim: CPU Scheduling & Memory Management Simulator

A command-line simulator, written in C on Linux, of how an operating system:

- **schedules processes on the CPU**: FCFS, SJF, SRTF (preemptive SJF), Round Robin, Priority
- **manages memory with demand paging**: FIFO, LRU and Optimal page replacement, plus a Belady's anomaly demo
- **runs work in parallel**: a POSIX-threads worker pool, and multiple processes communicating through pipes (`fork()` + `pipe()`)

Every algorithm is checked against the worked examples in Silberschatz's *Operating System Concepts* (`make test`).

## Build and run

Needs Linux or **WSL** on Windows, because `fork()`, pipes and pthreads are POSIX APIs.

```bash
make            # builds ./ossim
make test       # 27 checks against textbook answers
```

```bash
./ossim sched --file examples/workload.txt --quantum 3   # all 5 schedulers + Gantt charts
./ossim sched --algo rr --random 8 --seed 1
./ossim mem --frames 3                                   # classic 20-reference example
./ossim mem --frames 3 --algo lru --trace                # frame contents after every reference
./ossim belady                                           # more frames -> more faults (FIFO)
./ossim ipc                                              # 3 algorithms in 3 child processes
./ossim bench --jobs 200 --threads 4                     # 1 thread vs N threads, real timing
```

Workload file format: one process per line, `pid arrival burst priority` (`#` starts a comment).

## Project layout

```
include/            headers (one per module)
src/scheduler.c     FCFS, SJF, SRTF, RR, Priority + Gantt chart + statistics
src/memory.c        FIFO, LRU, OPT page replacement
src/workload.c      seeded random workloads (thread-safe RNG) + file loader
src/bench.c         pthreads worker pool (mutex-protected job queue)
src/ipc.c           fork() + pipes: one child process per algorithm
src/main.c          command-line interface
tests/test_sim.c    unit tests (textbook examples)
```

## Design notes (the "why")

**Scheduling**
- Non-preemptive algorithms (FCFS, SJF, Priority) choose a ready process whenever the CPU becomes free and run it to completion.
- SRTF decides again every tick: whichever process has the least work remaining runs.
- Round Robin uses a FIFO ready queue. Processes that *arrive* during a time slice are queued *before* the process that was just preempted, the same convention as the textbook.
- Ties are broken by arrival time and then PID, so results are deterministic.
- Metrics: waiting time = turnaround − burst, turnaround = finish − arrival, response = first run − arrival, plus CPU utilization and context switches.

**Paging**
- FIFO evicts the oldest-loaded page, LRU the least-recently-used one, and OPT the page whose next use is furthest in the future.
- A naive OPT rescans the future on every fault, which is O(n²). This one precomputes `next_use[i]` in a single backwards pass, so OPT runs in O(n × frames) and handles millions of references.
- Random reference strings are generated with **locality** (a working set that shifts occasionally). With uniformly random references, every algorithm would perform about the same.

**Threads (`bench`)**
- The only shared, mutable state is the `next_job` counter, protected by a `pthread_mutex_t`. Each job writes to its own result slot, so no lock is needed there.
- `rand()` is not thread-safe because it keeps hidden global state, so every job gets its own xorshift RNG seeded from its job number.
- This makes the output identical for 1 thread and N threads. The program prints a checksum to prove there are no race conditions.

**Processes (`ipc`)**
- For each algorithm, the parent creates two pipes (parent→child and child→parent), `fork()`s, and sends the reference string.
- Each child runs one algorithm and writes its result back.
- Pipes are byte streams, so reads and writes loop until all bytes are transferred. Unused pipe ends are closed so EOF works. The parent calls `waitpid()` on every child so no zombies are left behind.
- Children call `_exit()`, not `exit()`, so they don't flush the parent's copied stdio buffers a second time.

## Measured results

Measured on a 2-core Linux VM. Speedup depends on your core count, so run `./ossim bench` on your own machine.

See `RESULTS.md`.

## Possible extensions

- Multilevel feedback queue scheduling, and aging to prevent starvation in Priority scheduling
- Clock (second-chance) page replacement, and working-set / page-fault-frequency allocation
- Contiguous allocation (first-fit / best-fit / worst-fit) with fragmentation statistics
