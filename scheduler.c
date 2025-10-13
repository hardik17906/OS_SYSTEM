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

// Simple circular queue for ready indices
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

// Read exactly n bytes
static ssize_t read_n(int fd, void *buf, size_t n){
    size_t r = 0;
    char *p = buf;
    while (r < n){
        ssize_t s = read(fd, p + r, n - r);
        if (s < 0){
            if (errno == EINTR) continue;
            return -1;
        } else if (s == 0){
            return (ssize_t)r;
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

    int flags = fcntl(ctrl_fd, F_GETFL, 0);
    if (flags != -1) (void)fcntl(ctrl_fd, F_SETFL, flags | O_NONBLOCK);

    char prev_finished[MAX_JOBS] = {0};

    // enqueue any jobs already registered in shm before exec 
    for (int i = 0; i < shm_tbl->max_jobs; ++i){
        if (shm_tbl->jobs[i].pid != 0 && !shm_tbl->jobs[i].finished)
            if (!is_idx_in_ready(&ready, i)) rq_push(&ready, i);
        if (shm_tbl->jobs[i].finished) prev_finished[i] = 1;
    }

    while (1){
        shm_tbl->global_quantum++;
        int my_q = shm_tbl->global_quantum;

        // Pick NCPU processes from ready queue
        running_count = 0;
        for (int i = 0; i < NCPU; ++i){
            int idx;
            if (rq_pop(&ready, &idx) != 0) break;
            pid_t pid = shm_tbl->jobs[idx].pid;
            if (pid <= 0 || shm_tbl->jobs[idx].finished) continue;
            if (!pid_exists(pid)){
                shm_tbl->jobs[idx].finished = 1;
                shm_tbl->jobs[idx].completion_q = my_q;
                continue;
            }
            if (kill(pid, SIGCONT) != -1)
                running_idxs[running_count++] = idx;
            else if (errno == ESRCH){
                shm_tbl->jobs[idx].finished = 1;
                shm_tbl->jobs[idx].completion_q = my_q;
            }
        }

        // Handle input from shell
        int elapsed = 0;
        const int step_ms = 1;
        while (elapsed < TSLICE_ms && !shutdown_requested){
            int32_t pid32;
            ssize_t r = read(ctrl_fd, &pid32, sizeof(pid32));
            if (r == sizeof(pid32)){
                int32_t namelen;
                if (read_n(ctrl_fd, &namelen, sizeof(namelen)) != sizeof(namelen)){
                    shutdown_requested = 1;
                    break;
                }
                if (pid32 == 0 && namelen == 0) shutdown_requested = 1;
                else {
                    char namebuf[MAX_NAME] = {'\0'};
                    if (namelen > 0){
                        if (namelen >= MAX_NAME) namelen = MAX_NAME-1;
                        if (read_n(ctrl_fd, namebuf, namelen) != namelen){
                            shutdown_requested = 1;
                            break;
                        }
                        namebuf[namelen] = '\0';
                    }
                    int found = -1;
                    for (int i = 0; i < shm_tbl->max_jobs; ++i)
                        if (shm_tbl->jobs[i].pid == pid32){ found = i; break; }
                    if (found == -1)
                        for (int i = 0; i < shm_tbl->max_jobs; ++i)
                            if (shm_tbl->jobs[i].pid == 0){ found = i; break; }
                    if (found != -1){
                        shm_tbl->jobs[found].pid = pid32;
                        strncpy(shm_tbl->jobs[found].name, namebuf, MAX_NAME-1);
                        shm_tbl->jobs[found].name[MAX_NAME-1] = '\0';
                        shm_tbl->jobs[found].submit_q = my_q + 1;
                        shm_tbl->jobs[found].run_quanta = 0;
                        shm_tbl->jobs[found].completion_q = -1;
                        shm_tbl->jobs[found].finished = 0;
                        shm_tbl->n_jobs++;
                        if (!is_idx_in_ready(&ready, found) && !is_idx_in_running(running_idxs, running_count, found))
                            rq_push(&ready, found);
                        prev_finished[found] = 0;
                    }
                }
            } else if (r == 0 || (r == -1 && !(errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR))){
                shutdown_requested = 1;
                break;
            }
            usleep(step_ms*1000);
            elapsed += step_ms;
        }

        // Stop running processes and re-enqueue if still alive
        for (int r = 0; r < running_count; ++r){
            int idx = running_idxs[r];
            pid_t pid = shm_tbl->jobs[idx].pid;
            if (shm_tbl->jobs[idx].finished) continue;
            if (!pid_exists(pid)){
                shm_tbl->jobs[idx].finished = 1;
                shm_tbl->jobs[idx].completion_q = my_q;
                continue;
            }
            if (kill(pid, SIGSTOP) == -1 && errno == ESRCH){
                shm_tbl->jobs[idx].finished = 1;
                shm_tbl->jobs[idx].completion_q = my_q;
            } else {
                shm_tbl->jobs[idx].run_quanta++;
                if (!shm_tbl->jobs[idx].finished && !is_idx_in_ready(&ready, idx))
                    rq_push(&ready, idx);
            }
        }

        // Print finished jobs exactly once
        for (int i = 0; i < shm_tbl->max_jobs; ++i){
            if (shm_tbl->jobs[i].pid != 0 && shm_tbl->jobs[i].finished && !prev_finished[i]){
                printf("Scheduler: pid %d (%s) finished\n", (int)shm_tbl->jobs[i].pid, shm_tbl->jobs[i].name);
                fflush(stdout);
                prev_finished[i] = 1;
            }
        }

        // Exit if shutdown requested and all jobs done
        if (shutdown_requested && rq_empty(&ready)){
            int any_unfinished = 0;
            for (int i = 0; i < shm_tbl->max_jobs; ++i)
                if (shm_tbl->jobs[i].pid != 0 && !shm_tbl->jobs[i].finished){ any_unfinished = 1; break; }
            if (!any_unfinished) break;
        }
    }

    munmap(shm_tbl, sz);
    close(shm_fd);
    close(ctrl_fd);
    return 0;
}
