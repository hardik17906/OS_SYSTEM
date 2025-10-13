#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <signal.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/wait.h>

#define MAX_JOBS 1024
#define MAX_NAME 256

typedef struct {
    pid_t pid;
    char name[MAX_NAME];
    int submit_q;
    int run_quanta;
    int completion_q;
    int finished;
} job_info_t;

typedef struct {
    int max_jobs;
    int n_jobs;
    int global_quantum;
    job_info_t jobs[MAX_JOBS];
} shm_table_t;

//simple circular queue for ready indices
typedef struct {
    int data[MAX_JOBS];
    int head, tail;
    int size;
} rq_t;

static void rq_init(rq_t *q){ q->head = q->tail = q->size = 0; }
static int rq_empty(rq_t *q){ return q->size == 0; }
static int rq_push(rq_t *q, int v){
    if (q->size == MAX_JOBS) return -1;
    q->data[q->tail] = v;
    q->tail = (q->tail + 1) % MAX_JOBS;
    q->size++;
    return 0;
}
static int rq_pop(rq_t *q, int *out){
    if (q->size == 0) return -1;
    *out = q->data[q->head];
    q->head = (q->head + 1) % MAX_JOBS;
    q->size--;
    return 0;
}

//read exactly n bytes (or EOF/short)
static ssize_t read_n(int fd, void *buf, size_t n){
    size_t r = 0;
    char *p = buf;
    while (r < n){
        ssize_t s = read(fd, p + r, n - r);
        if (s < 0){
            if (errno == EINTR) continue;
            return -1;
        } else if (s == 0){
            return (ssize_t)r; //EOF (partial or full)
        }
        r += (size_t)s;
    }
    return (ssize_t)r;
}

static int is_idx_in_running(int *running_idxs, int running_count, int idx){
    for (int i = 0; i < running_count; i++) if (running_idxs[i] == idx) return 1;
    return 0;
}
static int is_idx_in_ready(rq_t *q, int idx){
    for (int i = 0, p = q->head; i < q->size; i++, p = (p+1)%MAX_JOBS){
        if (q->data[p] == idx) return 1;
    }
    return 0;
}
static int pid_exists(pid_t pid){
    if (pid <= 0) return 0;
    if (kill(pid, 0) == -1) {
        if (errno == ESRCH) return 0;
    }
    return 1;
}

int main(int argc, char **argv){
    if (argc < 5){
        fprintf(stderr, "scheduler usage: %s <ctrl_fd> <NCPU> <TSLICE_ms> <shm_name>\n", argv[0]);
        return 1;
    }
    int ctrl_fd = atoi(argv[1]);
    int NCPU = atoi(argv[2]);
    int TSLICE_ms = atoi(argv[3]);
    const char *shm_name = argv[4];

    if (NCPU <= 0 || TSLICE_ms <= 0){
        fprintf(stderr, "scheduler: invalid NCPU/TSLICE\n");
        return 1;
    }

    int shm_fd = shm_open(shm_name, O_RDWR, 0);
    if (shm_fd == -1){
        perror("scheduler shm_open");
        return 1;
    }
    size_t sz = sizeof(shm_table_t);
    void *map = mmap(NULL, sz, PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
    if (map == MAP_FAILED){
        perror("scheduler mmap");
        close(shm_fd);
        return 1;
    }
    shm_table_t *shm_tbl = (shm_table_t *)map;

    rq_t ready; rq_init(&ready);
    int running_idxs[128];
    int running_count = 0;
    int shutdown_requested = 0;

    /* Make ctrl_fd non-blocking so we can poll it with small sleeps
       instead of using poll/select. This is the minimal change to
       remove poll/select while keeping behavior similar. */
    int flags = fcntl(ctrl_fd, F_GETFL, 0);
    if (flags != -1) {
        (void)fcntl(ctrl_fd, F_SETFL, flags | O_NONBLOCK);
    }

    char prev_finished[MAX_JOBS] = {0};

    //enqueue any jobs already registered in shm before exec 
    for (int i = 0; i < shm_tbl->max_jobs; ++i){
        if (shm_tbl->jobs[i].pid != 0 && !shm_tbl->jobs[i].finished){
            if (!is_idx_in_ready(&ready, i)) rq_push(&ready, i);
        }
        if (shm_tbl->jobs[i].finished) prev_finished[i] = 1;
    }

    while (1){
        // starting the quantum
        shm_tbl->global_quantum++;
        int my_q = shm_tbl->global_quantum;

        // here we are picking up NCPU Proccess from the ready queue to work upon
        running_count = 0;
        for (int i = 0; i < NCPU; ++i){
            int idx;
            if (rq_pop(&ready, &idx) != 0) break;
            pid_t pid = shm_tbl->jobs[idx].pid;
            if (pid <= 0 || shm_tbl->jobs[idx].finished) continue;

            if (!pid_exists(pid)){
                shm_tbl->jobs[idx].finished = 1;
                shm_tbl->jobs[idx].completion_q = my_q;
                if (!prev_finished[idx]){
                    printf("Scheduler: pid %d (%s) finished at quantum %d\n", (int)pid,
                           shm_tbl->jobs[idx].name[0] ? shm_tbl->jobs[idx].name : "<unnamed>",
                           shm_tbl->jobs[idx].completion_q);
                    fflush(stdout);
                    prev_finished[idx] = 1;
                }
                continue;
            }

            if (kill(pid, SIGCONT) == -1) {
                if (errno == ESRCH) {
                    shm_tbl->jobs[idx].finished = 1;
                    shm_tbl->jobs[idx].completion_q = my_q;
                    if (!prev_finished[idx]){
                        printf("Scheduler: pid %d (%s) finished at quantum %d\n", (int)pid,
                               shm_tbl->jobs[idx].name[0] ? shm_tbl->jobs[idx].name : "<unnamed>",
                               shm_tbl->jobs[idx].completion_q);
                        fflush(stdout);
                        prev_finished[idx] = 1;
                    }
                }
            } else {
                // started this quantum 
                // printf("Scheduler: starting pid %d (%s) at quantum %d\n", (int)pid,
                //        shm_tbl->jobs[idx].name[0] ? shm_tbl->jobs[idx].name : "<unnamed>", my_q);
                fflush(stdout);
                running_idxs[running_count++] = idx;
            }
        }

        /*************************************************************
         * Replace poll/select: use non-blocking read loop with usleep
         * Wait up to TSLICE_ms milliseconds. If data is available on
         * ctrl_fd, read the message. Otherwise, simply timeout.
         *************************************************************/
        {
            int elapsed = 0;
            const int step_ms = 1; /* check every 1 ms to stay responsive */
            int handled_input = 0;

            while (elapsed < TSLICE_ms && !shutdown_requested) {
                /* Try to read a pid32 (non-blocking). If no data: EAGAIN/EWOULDBLOCK */
                int32_t pid32;
                ssize_t r = read(ctrl_fd, &pid32, sizeof(pid32));
                if (r == sizeof(pid32)) {
                    /* we got pid32, now read the rest (namelen + name) using blocking read_n.
                       The writer writes the whole message atomically for these small sizes,
                       so the following read_n should not block indefinitely. */
                    int32_t namelen;
                    if (read_n(ctrl_fd, &namelen, sizeof(namelen)) != sizeof(namelen)) {
                        shutdown_requested = 1;
                        handled_input = 1;
                        break;
                    } else {
                        if (pid32 == 0 && namelen == 0) {
                            shutdown_requested = 1;
                        } else {
                            char namebuf[MAX_NAME];
                            if (namelen > 0) {
                                if (namelen >= MAX_NAME) namelen = MAX_NAME - 1;
                                ssize_t got = read_n(ctrl_fd, namebuf, namelen);
                                if (got != namelen) {
                                    shutdown_requested = 1;
                                    handled_input = 1;
                                    break;
                                }
                                namebuf[namelen] = '\0';
                            } else {
                                namebuf[0] = '\0';
                            }
                            /* register job in shared table (same logic as before) */
                            if (!shutdown_requested) {
                                int found = -1;
                                for (int i = 0; i < shm_tbl->max_jobs; ++i) {
                                    if (shm_tbl->jobs[i].pid == pid32) { found = i; break; }
                                }
                                if (found == -1) {
                                    for (int i = 0; i < shm_tbl->max_jobs; ++i) {
                                        if (shm_tbl->jobs[i].pid == 0) { found = i; break; }
                                    }
                                }
                                if (found == -1) {
                                    fprintf(stderr, "scheduler: job table full, dropping job pid=%d\n", (int)pid32);
                                } else {
                                    shm_tbl->jobs[found].pid = pid32;
                                    strncpy(shm_tbl->jobs[found].name, namebuf, MAX_NAME-1);
                                    shm_tbl->jobs[found].name[MAX_NAME-1] = '\0';
                                    shm_tbl->jobs[found].submit_q = my_q + 1;
                                    shm_tbl->jobs[found].run_quanta = 0;
                                    shm_tbl->jobs[found].completion_q = -1;
                                    shm_tbl->jobs[found].finished = 0;
                                    shm_tbl->n_jobs++;
                                    if (!is_idx_in_ready(&ready, found) && !is_idx_in_running(running_idxs, running_count, found)){
                                        rq_push(&ready, found);
                                    }
                                    prev_finished[found] = shm_tbl->jobs[found].finished ? 1 : 0;
                                }
                            }
                        }
                    }
                    handled_input = 1;
                    /* After handling an input we keep checking until quantum expires so
                       other writers can also deliver messages in this quantum. */
                } else if (r == -1) {
                    if (errno == EAGAIN || errno == EWOULDBLOCK) {
                        /* no data now; sleep a tiny bit and retry */
                        usleep(step_ms * 1000);
                        elapsed += step_ms;
                        continue;
                    } else if (errno == EINTR) {
                        /* interrupted, try again */
                        continue;
                    } else {
                        /* unexpected error reading pipe */
                        perror("read ctrl_fd");
                        shutdown_requested = 1;
                        break;
                    }
                } else if (r == 0) {
                    /* pipe closed by writer -> treat as shutdown request */
                    shutdown_requested = 1;
                    break;
                } else {
                    /* partial read (less than sizeof(pid32)) - unlikely for an int but handle conservatively:
                       attempt to read remaining bytes using read_n (non-blocking behavior preserved by O_NONBLOCK).
                       For simplicity, treat this as no-data and continue (rare path). */
                    /* we won't attempt complex partial-reads here to keep changes minimal */
                    usleep(step_ms * 1000);
                    elapsed += step_ms;
                    continue;
                }
            }
            (void)handled_input;
        }

        // stopping the currently running process at quantum end and re-enqueue if process is still alive
        for (int r = 0; r < running_count; ++r){
            int idx = running_idxs[r];
            pid_t pid = shm_tbl->jobs[idx].pid;
            if (shm_tbl->jobs[idx].finished) {
                if (!prev_finished[idx]){
                    printf("Scheduler: pid %d (%s) finished at quantum %d\n", (int)pid,
                           shm_tbl->jobs[idx].name[0] ? shm_tbl->jobs[idx].name : "<unnamed>",
                           shm_tbl->jobs[idx].completion_q >= 0 ? shm_tbl->jobs[idx].completion_q : shm_tbl->global_quantum);
                    fflush(stdout);
                    prev_finished[idx] = 1;
                }
                continue;
            }
            if (!pid_exists(pid)) {
                shm_tbl->jobs[idx].finished = 1;
                shm_tbl->jobs[idx].completion_q = my_q;
                if (!prev_finished[idx]){
                    printf("Scheduler: pid %d (%s) finished at quantum %d\n", (int)pid,
                           shm_tbl->jobs[idx].name[0] ? shm_tbl->jobs[idx].name : "<unnamed>",
                           shm_tbl->jobs[idx].completion_q);
                    fflush(stdout);
                    prev_finished[idx] = 1;
                }
                continue;
            }
            if (kill(pid, SIGSTOP) == -1){
                if (errno == ESRCH){
                    shm_tbl->jobs[idx].finished = 1;
                    shm_tbl->jobs[idx].completion_q = my_q;
                    if (!prev_finished[idx]){
                        printf("Scheduler: pid %d (%s) finished at quantum %d\n", (int)pid,
                               shm_tbl->jobs[idx].name[0] ? shm_tbl->jobs[idx].name : "<unnamed>",
                               shm_tbl->jobs[idx].completion_q);
                        fflush(stdout);
                        prev_finished[idx] = 1;
                    }
                }
            } else {
                shm_tbl->jobs[idx].run_quanta++;
                if (!shm_tbl->jobs[idx].finished){
                    if (!is_idx_in_ready(&ready, idx)) rq_push(&ready, idx);
                }
            }
        }
        // here trying to print if any process has finished during the quantum
        for (int i = 0; i < shm_tbl->max_jobs; ++i){
            if (!prev_finished[i] && shm_tbl->jobs[i].pid != 0 && shm_tbl->jobs[i].finished){
                printf("Scheduler: pid %d (%s) finished at quantum %d\n", (int)shm_tbl->jobs[i].pid,
                       shm_tbl->jobs[i].name[0] ? shm_tbl->jobs[i].name : "<unnamed>",
                       shm_tbl->jobs[i].completion_q >= 0 ? shm_tbl->jobs[i].completion_q : shm_tbl->global_quantum);
                fflush(stdout);
                prev_finished[i] = 1;
            }
        }
        // here trying to exit whether the process is running or not(when requested)
        if (shutdown_requested && rq_empty(&ready)) {
            int any_unfinished = 0;
            for (int i = 0; i < shm_tbl->max_jobs; ++i){
                if (shm_tbl->jobs[i].pid != 0 && !shm_tbl->jobs[i].finished) {
                    any_unfinished = 1; break;
                }
            }
            if (!any_unfinished) break;
        }
    }

    munmap(shm_tbl, sz);
    close(shm_fd);
    close(ctrl_fd);
    return 0;
}
