#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdint.h>
#include <signal.h>
#include <fcntl.h>
#include <ctype.h>
#include <sys/wait.h>
#include <sys/time.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <errno.h>

#define Timeval struct timeval
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

#define MAX_SIZE_OF_HISTORY 100
#define MAX_INPUT_LENGTH 1024

typedef struct {
    char* command;
    pid_t pid;
    Timeval startTime;
    Timeval endTime;
} cmdHis;

static cmdHis CMDHIS[MAX_SIZE_OF_HISTORY];
static int CMDHIS_ind = 0;

static void displayCommands(void){
    if (CMDHIS_ind == 0) return;
    char* prev = strdup(CMDHIS[0].command);
    int idx = 1;
    printf("1. %s\n", CMDHIS[0].command);
    for (int i = 1; i < CMDHIS_ind; ++i){
        if (strcmp(CMDHIS[i].command, prev) != 0){
            ++idx;
            printf("%d. %s\n", idx, CMDHIS[i].command);
            free(prev);
            prev = strdup(CMDHIS[i].command);
        }
    }
    free(prev);
}

static void addCommandtoHis(const char *command, pid_t pid, Timeval startTime, Timeval endTime){
    if (pid == -1) return;
    if (CMDHIS_ind < MAX_SIZE_OF_HISTORY){
        CMDHIS[CMDHIS_ind].command = strdup(command);
        CMDHIS[CMDHIS_ind].pid = pid;
        CMDHIS[CMDHIS_ind].startTime = startTime;
        CMDHIS[CMDHIS_ind].endTime = endTime;
        CMDHIS_ind++;
    } else {
        free(CMDHIS[0].command);
        for (int i = 1; i < CMDHIS_ind; ++i) CMDHIS[i-1] = CMDHIS[i];
        CMDHIS[CMDHIS_ind-1].command = strdup(command);
        CMDHIS[CMDHIS_ind-1].pid = pid;
        CMDHIS[CMDHIS_ind-1].startTime = startTime;
        CMDHIS[CMDHIS_ind-1].endTime = endTime;
    }
}

// No pipes are allowed now , this will only execute submit , exit , etc commands
static int launchCommand(const char *command){
    struct timeval start, end;
    gettimeofday(&start, NULL);

    char *copy = strdup(command);
    if (!copy) return -1;
    char *argv[MAX_INPUT_LENGTH/2];
    int argc = 0;
    char *tok = strtok(copy, " \t");
    while (tok && argc < (int)(sizeof(argv)/sizeof(argv[0]) - 1)){
        argv[argc++] = tok;
        tok = strtok(NULL, " \t");
    }
    argv[argc] = NULL;
    if (argc == 0) { free(copy); return 0; }

    pid_t pid = fork();
    if (pid < 0){
        perror("fork");
        free(copy);
        return -1;
    } else if (pid == 0){
        // child created 
        execvp(argv[0], argv);
        perror("execvp");
        _exit(127);
    } else {
        int st;
        waitpid(pid, &st, 0);
        gettimeofday(&end, NULL);
        addCommandtoHis(command, pid, start, end);
    }
    free(copy);
    return 0;
}

static int launch_Script_file(const char *path){
    FILE *f = fopen(path, "r");
    if (!f){ perror("fopen"); return -1; }
    char line[MAX_INPUT_LENGTH];
    while (fgets(line, sizeof(line), f)){
        char *nl = strchr(line, '\n');
        if (nl) *nl = '\0';
        if (line[0] == '\0') continue;
        int rc = launchCommand(line);
        if (rc != 0) fprintf(stderr, "Failed to run: %s\n", line);
    }
    fclose(f);
    return 0;
}

static char *userInputRead(void){
    char *buf = malloc(MAX_INPUT_LENGTH);
    if (!buf){ perror("malloc"); exit(EXIT_FAILURE); }
    if (fgets(buf, MAX_INPUT_LENGTH, stdin) == NULL){
        free(buf);
        return NULL;
    }
    size_t len = strlen(buf);
    if (len && buf[len-1] == '\n') buf[len-1] = '\0';
    return buf;
}


// Shared Memory
static int ctrl_pipe_w = -1;
static char shm_name[128];
static shm_table_t *shm_tbl = NULL;
static int shm_fd = -1;
static pid_t scheduler_pid = -1;

static int create_shared_table(void){
    pid_t me = getpid();
    snprintf(shm_name, sizeof(shm_name), "/simplesched_shm_%d", (int)me);
    shm_fd = shm_open(shm_name, O_CREAT | O_RDWR, 0600);
    if (shm_fd == -1){ perror("shm_open"); return -1; }
    size_t sz = sizeof(shm_table_t);
    if (ftruncate(shm_fd, sz) == -1){ perror("ftruncate"); close(shm_fd); shm_unlink(shm_name); return -1; }
    void *map = mmap(NULL, sz, PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
    if (map == MAP_FAILED){ perror("mmap"); close(shm_fd); shm_unlink(shm_name); return -1; }
    shm_tbl = (shm_table_t *)map;
    shm_tbl->max_jobs = MAX_JOBS;
    shm_tbl->n_jobs = 0;
    shm_tbl->global_quantum = 0;
    for (int i = 0; i < MAX_JOBS; ++i){
        shm_tbl->jobs[i].pid = 0;
        shm_tbl->jobs[i].name[0] = '\0';
        shm_tbl->jobs[i].submit_q = -1;
        shm_tbl->jobs[i].run_quanta = 0;
        shm_tbl->jobs[i].completion_q = -1;
        shm_tbl->jobs[i].finished = 0;
    }
    return 0;
}

// write message to scheduler  (int32 pid, int32 namelen, name bytes) 
static int send_msg_to_scheduler(pid_t pid, const char *name){
    if (ctrl_pipe_w < 0) return -1;
    int32_t pid32 = (int32_t)pid;
    int32_t namelen = (name ? (int32_t)strlen(name) : 0);
    ssize_t w;
    w = write(ctrl_pipe_w, &pid32, sizeof(pid32));
    if (w != sizeof(pid32)) { perror("write pid"); return -1; }
    w = write(ctrl_pipe_w, &namelen, sizeof(namelen));
    if (w != sizeof(namelen)) { perror("write namelen"); return -1; }
    if (namelen > 0){
        w = write(ctrl_pipe_w, name, namelen);
        if (w != namelen) { perror("write name"); return -1; }
    }
    return 0;
}

// submit one job : fork+exec the job binary, record in shared memory and notify scheduler
static int submit_job_one(const char *path){
    // disallowing pipes in a submission
    if (strstr(path, "|") != NULL){
        fprintf(stderr, "submit: pipes not allowed: %s\n", path);
        return -1;
    }

    // Check if file exists and is executable
    struct stat st;
    if (stat(path, &st) != 0) {
        fprintf(stderr, "submit: %s: No such file or directory\n", path);
        return -1;
    }
    if (!(st.st_mode & S_IXUSR)) {
        fprintf(stderr, "submit: %s: not executable\n", path);
        return -1;
    }

    pid_t pid = fork();
    if (pid < 0){ perror("fork"); return -1; }
    else if (pid == 0){
        if (setpgid(0,0) == -1) { /* ignore */ }
        execl(path, path, (char*)NULL);
        perror("execl");
        _exit(127);
    } else {
        if (shm_tbl != NULL){
            int idx = -1;
            for (int i = 0; i < MAX_JOBS; ++i){
                if (shm_tbl->jobs[i].pid == 0){ idx = i; break; }
            }
            if (idx == -1) {
                fprintf(stderr, "job table full\n");
            } else {
                shm_tbl->jobs[idx].pid = pid;
                strncpy(shm_tbl->jobs[idx].name, path, MAX_NAME-1);
                shm_tbl->jobs[idx].name[MAX_NAME-1] = '\0';
                shm_tbl->jobs[idx].submit_q = shm_tbl->global_quantum;
                shm_tbl->jobs[idx].run_quanta = 0;
                shm_tbl->jobs[idx].completion_q = -1;
                shm_tbl->jobs[idx].finished = 0;
                shm_tbl->n_jobs++;
            }
        }
        if (send_msg_to_scheduler(pid, path) != 0){
            fprintf(stderr, "failed to inform scheduler about new job\n");
        }
    }
    return 0;
}



// Final stat printing 
static void print_final_stats(int tsl_ms){
    if (!shm_tbl) return;
    printf("\nFinal job statistics (TSLICE = %d ms):\n", tsl_ms);
    printf("%-30s %-8s %-12s %-10s\n", "Name", "PID", "Completion", "Wait");
    for (int i = 0; i < MAX_JOBS; ++i){
        job_info_t *j = &shm_tbl->jobs[i];
        if (j->pid == 0) continue;
        int completion_q = (j->completion_q >= 0) ? j->completion_q : shm_tbl->global_quantum;
        int submit_q = (j->submit_q >= 0) ? j->submit_q : 0;
        int completion_time = completion_q - submit_q + 1;
        if (completion_time <= 0) completion_time = 1;
        int wait_time = completion_time - j->run_quanta;
        if (wait_time < 0) wait_time = 0;
        printf("%-30s %-8d %-12d %-10d\n", j->name, (int)j->pid, completion_time, wait_time);
    }
    printf("-------------------------------------------------\n");
}

// SIGCHLD: reap children and mark finished in shared table 
static void sigchld_handler(int sig){
    (void)sig;
    while (1){
        int status;
        pid_t w = waitpid(-1, &status, WNOHANG);
        if (w <= 0) break;
        if (shm_tbl != NULL){
            for (int i = 0; i < shm_tbl->max_jobs; ++i){
                if (shm_tbl->jobs[i].pid == w && !shm_tbl->jobs[i].finished){
                    shm_tbl->jobs[i].run_quanta++;
                    shm_tbl->jobs[i].finished = 1;
                    shm_tbl->jobs[i].completion_q = shm_tbl->global_quantum;
                    break;
                }
            }
        }
    }
}


static void shellLoop(int NCPU, int TSLICE_ms){
    char *last_command = NULL;
    while (1){
        printf("$ ");
        fflush(stdout);
        char *command = userInputRead();
        if (!command) break;

        if (last_command == NULL || strcmp(command, last_command) != 0){
            addCommandtoHis(command, -1, (struct timeval){0}, (struct timeval){0});
            free(last_command);
            last_command = strdup(command);
        }

        if (strcmp(command, "exit") == 0){
            send_msg_to_scheduler(0, NULL);
            if (ctrl_pipe_w >= 0) close(ctrl_pipe_w);
            
            // killing remaining jobs 
            if (shm_tbl){
                for (int i = 0; i < shm_tbl->max_jobs; ++i){
                    pid_t pid = shm_tbl->jobs[i].pid;
                    if (pid > 0 && !shm_tbl->jobs[i].finished){
                        kill(-pid, SIGKILL);
                        shm_tbl->jobs[i].finished = 1;
                    }
                }
            }

            if (scheduler_pid > 0){ int st; waitpid(scheduler_pid, &st, 0); }

            print_final_stats(TSLICE_ms);

            if (shm_tbl){
                size_t sz = sizeof(shm_table_t);
                munmap(shm_tbl, sz);
                close(shm_fd);
                shm_unlink(shm_name);
            }

            free(command);
            free(last_command);
            break;
        } else if (strncmp(command, "submit ", 7) == 0){
            const char *arg = command + 7;
            while (*arg && isspace((unsigned char)*arg)) arg++;
            if (*arg == '\0'){
                fprintf(stderr, "Usage: submit ./a.out\n");
            } else {
                if (submit_job_one(arg) != 0) {
                    fprintf(stderr, "submit failed\n");
                }
            }
        } else if (strcmp(command, "history") == 0){
            displayCommands();
        } else if (strncmp(command, "Hardik", 6) == 0){
            const char *fname = command + 7;
            launch_Script_file(fname);
        } else {
            launchCommand(command);
        }

        free(command);
    }
}

int main(int argc, char **argv){
    if (argc != 3){
        fprintf(stderr, "Usage: %s <NCPU> <TSLICE_ms>\n", argv[0]);
        return 1;
    }
    int NCPU = atoi(argv[1]);
    int TSLICE_ms = atoi(argv[2]);
    if (NCPU <= 0 || TSLICE_ms <= 0){
        fprintf(stderr, "NCPU and TSLICE_ms must be positive\n");
        return 1;
    }

    if (create_shared_table() != 0){
        fprintf(stderr, "Failed to create shared table\n");
        return 1;
    }

    // sigchild handler updater 
    struct sigaction sa = {0};
    sa.sa_handler = sigchld_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;
    if (sigaction(SIGCHLD, &sa, NULL) == -1) perror("sigaction(SIGCHLD)");

    // create control pipe and fork scheduler 
    int ctrl_pipe[2];
    if (pipe(ctrl_pipe) == -1){ perror("pipe"); return 1; }
    ctrl_pipe_w = ctrl_pipe[1];

    scheduler_pid = fork();
    if (scheduler_pid < 0){ perror("fork"); return 1; }
    else if (scheduler_pid == 0){
        close(ctrl_pipe_w);
        char fdarg[32], ncpu_arg[16], ts_arg[16];
        snprintf(fdarg, sizeof(fdarg), "%d", ctrl_pipe[0]);
        snprintf(ncpu_arg, sizeof(ncpu_arg), "%d", NCPU);
        snprintf(ts_arg, sizeof(ts_arg), "%d", TSLICE_ms);
        execl("./scheduler", "scheduler", fdarg, ncpu_arg, ts_arg, shm_name, (char*)NULL);
        perror("execl scheduler");
        _exit(127);
    } else {
        close(ctrl_pipe[0]);
        shellLoop(NCPU, TSLICE_ms);
    }

    return 0;
}
