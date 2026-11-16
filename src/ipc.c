/*
 * ipc.c - Inter-process communication with fork() and pipes.
 *
 * For each page replacement algorithm the parent:
 *   1. creates two pipes: parent->child (to_child) and child->parent (to_parent)
 *   2. fork()s. The child is a full copy of the parent process, with its
 *      own memory. Nothing is shared except what goes through the pipes.
 *   3. writes the reference string into to_child.
 * Each child reads it, runs ONE algorithm, writes its PageStats into
 * to_parent and exits. The parent reads all results and waitpid()s each
 * child so no zombie processes are left behind.
 *
 * Pipes are byte streams: a single read()/write() may move fewer bytes
 * than asked for (pipe buffers are ~64 KB), so we loop until done.
 */
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <unistd.h>

#include "concurrency.h"
#include "memory.h"

static int write_all(int fd, const void *buf, size_t len) {
    const char *p = buf;
    while (len > 0) {
        ssize_t w = write(fd, p, len);
        if (w < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        p += w;
        len -= (size_t)w;
    }
    return 0;
}

static int read_all(int fd, void *buf, size_t len) {
    char *p = buf;
    while (len > 0) {
        ssize_t r = read(fd, p, len);
        if (r < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        if (r == 0) return -1;            /* other end closed early */
        p += r;
        len -= (size_t)r;
    }
    return 0;
}

/* Runs inside the child process. Never returns. */
static void child_main(int read_fd, int write_fd, PageAlgo algo) {
    int header[2];                        /* n, frames */
    if (read_all(read_fd, header, sizeof header) < 0) _exit(1);
    int n = header[0], frames = header[1];

    int *refs = malloc(sizeof(int) * (size_t)n);
    if (!refs || read_all(read_fd, refs, sizeof(int) * (size_t)n) < 0) _exit(1);

    PageStats stats = simulate_paging(algo, refs, n, frames, 0);
    if (write_all(write_fd, &stats, sizeof stats) < 0) _exit(1);

    free(refs);
    close(read_fd);
    close(write_fd);
    _exit(0);                             /* _exit: don't flush parent's stdio buffers twice */
}

int run_ipc(const int *refs, int n, int frames, int faults_out[3], double *seconds_out) {
    pid_t pids[PAGE_ALGO_COUNT];
    int to_child[PAGE_ALGO_COUNT][2], to_parent[PAGE_ALGO_COUNT][2];
    double start = now_seconds();

    fflush(stdout);                       /* avoid duplicated buffered output after fork */
    for (int a = 0; a < PAGE_ALGO_COUNT; a++) {
        if (pipe(to_child[a]) < 0 || pipe(to_parent[a]) < 0) { perror("pipe"); return -1; }

        pids[a] = fork();
        if (pids[a] < 0) { perror("fork"); return -1; }

        if (pids[a] == 0) {               /* ---------- child ---------- */
            close(to_child[a][1]);        /* child only READS to_child   */
            close(to_parent[a][0]);       /* child only WRITES to_parent */
            /* Close pipe ends inherited from earlier children too. */
            for (int b = 0; b < a; b++) { close(to_child[b][1]); close(to_parent[b][0]); }
            child_main(to_child[a][0], to_parent[a][1], (PageAlgo)a);
        }
                                          /* ---------- parent --------- */
        close(to_child[a][0]);
        close(to_parent[a][1]);
    }

    /* Send the work. All children are already running, so they compute in
     * parallel while we write to the next pipe. */
    for (int a = 0; a < PAGE_ALGO_COUNT; a++) {
        int header[2] = {n, frames};
        if (write_all(to_child[a][1], header, sizeof header) < 0 ||
            write_all(to_child[a][1], refs, sizeof(int) * (size_t)n) < 0) {
            perror("write");
            return -1;
        }
        close(to_child[a][1]);            /* tells the child "no more data" */
    }

    int ok = 0;
    for (int a = 0; a < PAGE_ALGO_COUNT; a++) {
        PageStats stats;
        if (read_all(to_parent[a][0], &stats, sizeof stats) < 0) {
            fprintf(stderr, "failed to read result from child %d\n", (int)pids[a]);
            ok = -1;
        } else {
            faults_out[a] = stats.faults;
        }
        close(to_parent[a][0]);

        int status;
        waitpid(pids[a], &status, 0);     /* reap the child: no zombies */
        if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) ok = -1;
    }
    *seconds_out = now_seconds() - start;
    return ok;
}
