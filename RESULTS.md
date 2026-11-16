# Measured results

Machine: 2-core Intel Xeon @ 2.10 GHz Linux VM, gcc 13.3 `-O2`. Each benchmark was run 3 times.

## Multithreading (`./ossim bench --jobs 200 --threads 2`)

Each job runs 400 processes through 5 schedulers and 30,000 page references through 3 algorithms.

| Run | 1 thread | 2 threads | Speedup |
|---|---|---|---|
| 1 | 1.519 s | 0.743 s | 2.04x |
| 2 | 1.368 s | 0.708 s | 1.93x |
| 3 | 1.340 s | 0.718 s | 1.87x |

**About 1.9x faster on 2 cores (about 48% less time).** The checksum was identical in every run, so there are no race conditions.

## Multiple processes + pipes (`./ossim ipc`)

2,000,000 page references and 64 frames, with FIFO, LRU and OPT each in its own child process:

| Run | Sequential | fork + pipes | Speedup |
|---|---|---|---|
| 1 | 1.033 s | 0.565 s | 1.83x |
| 2 | 1.083 s | 0.572 s | 1.89x |
| 3 | 1.043 s | 0.577 s | 1.81x |

**About 1.8x faster** with 3 processes on 2 cores. Fault counts match the single-process run exactly.

## Correctness (`make test`)

27/27 checks against *Operating System Concepts* worked examples:
- FCFS average wait 17, SJF 7, SRTF 6.5, RR (q=4) 5.67, Priority 8.2
- FIFO 15 / LRU 12 / OPT 9 faults on the classic reference string
- Belady's anomaly: FIFO 9 → 10 faults when going from 3 → 4 frames

Speedup depends on core count. Run `./ossim bench` on your own machine and quote that number.
