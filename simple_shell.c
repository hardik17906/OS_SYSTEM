#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdint.h>
#include <pthread.h>
#include <signal.h> 
#include <fcntl.h>  
#include <ctype.h>
#include <sys/wait.h>
#include <sys/time.h>
#include <sys/stat.h>

#define Timeval struct timeval 
// Creating struct so that we can use a single variable with it's multiple attributes
// this struct will be for command history 
typedef struct commandHistory{
    char* command ;
    pid_t pid ;
    Timeval startTime ;
    Timeval endTime ;

} cmdHis ;


// Declaring a GlobaL array For command History storing 
// MAX History Size --> Let's define it as 100 
#define MAX_SIZE_OF_HISTORY 100 
#define MAX_INPUT_LENGTH 1024
cmdHis CMDHIS[MAX_SIZE_OF_HISTORY] ;
// let's declare the global variable which will keep the index updated on which we are in our command array 
int CMDHIS_ind = 0 ; 

// Creating function for displaying the history of the commands 
void displayCommands(){
    if ( CMDHIS_ind == 0 ) return ;
    char* prevCMD = strdup(CMDHIS[0].command) ;
    int displayIndex = 1 ;
    printf("%d. %s\n", displayIndex++, CMDHIS[0].command) ; 
    for ( int i = 1  ; i<CMDHIS_ind ; i++){
        if ( strcmp(CMDHIS[i].command,prevCMD) !=0 ){  // Since two continuous same commands does not make sense , so no need to print them 
            printf("%d. %s\n", displayIndex++, CMDHIS[i].command) ; 
            free(prevCMD) ;
            prevCMD = strdup(CMDHIS[i].command) ;
        }
    }
    free(prevCMD) ; 
}
// Creating function which will be adding the commands to our CMDHIS array 
void addCommandtoHis(char*command , pid_t pid , Timeval startTime , Timeval endTime ){
    if ( pid !=-1 ){  // This implies that the process ID is valid 

        if ( CMDHIS_ind < MAX_SIZE_OF_HISTORY ){ // have enough space to add more command to history 
            CMDHIS[CMDHIS_ind].command = strdup(command) ; // use strdup for making copy of command we are not using strcpy because we want to point to the new one
            CMDHIS[CMDHIS_ind].pid = pid ;
            CMDHIS[CMDHIS_ind].startTime = startTime ;
            CMDHIS[CMDHIS_ind].endTime = endTime ;
            CMDHIS_ind++ ;
        }
        else {
            // This implies 
            free(CMDHIS[0].command) ;  // Here freeing the oldest command form history 
            for ( int i = 1 ;i<CMDHIS_ind ; i++){
                CMDHIS[i-1] = CMDHIS[i] ; // shifting everyone one step behind 
            }
            CMDHIS[CMDHIS_ind-1].command = strdup(command) ;
            CMDHIS[CMDHIS_ind-1].pid = pid ;
            CMDHIS[CMDHIS_ind-1].startTime = startTime ;
            CMDHIS[CMDHIS_ind-1].endTime = endTime ;
        }
    }
}

int process_Creation_and_Running(char* command ){
        Timeval startTime , endTime ; // declaring startTime and endTime variables
        int status ;
        char**cmds = NULL ; // creating a array of char poniters 
        char*token = strtok(command,"|") ;
        int capacity = 4 ;
        cmds = malloc(capacity*sizeof(char*)) ; 
        if ( cmds == NULL ){
         perror("Memory allocation failed");
         exit(EXIT_FAILURE);   
        }
        int indexforcmds = 0 ;
        int count = -1 ;
        while (token != NULL) {
            if ( indexforcmds >= capacity ){
                capacity*=2 ;
                char**tmp = realloc(cmds,capacity*(sizeof(char*))) ;
                if ( tmp == NULL ){
                 perror("Memory reallocation failed");
                 free(cmds);
                 exit(EXIT_FAILURE);
                }
                cmds = tmp ;
            }
            while (*token == ' ') token++;
            char* end = token + strlen(token)-1 ;
            while ( end > token && (*end)==' '){
                *end='\0' ;
                 end-- ;
            }
            cmds[indexforcmds++] = token ;
            count++ ;
            token = strtok(NULL, "|") ; 
        }
        if ( indexforcmds == 0 ){
            return 0 ;
        }
        gettimeofday(&startTime , NULL ) ; // Putting the starting time inside the variable startTime
        if ( count == 0 ){
            // single command 
            pid_t f_0 = fork() ;
            if ( f_0 < 0 ){
                perror("Fork Failed" ) ;
                free(cmds) ;
                exit(EXIT_FAILURE) ;
            } 
            else if (f_0 == 0) {
                // Child process
                char* cmd_0[MAX_INPUT_LENGTH];
                int idc = 0;
                char* tok_1 = strtok(command, " ");
                while (tok_1 != NULL) {
                    cmd_0[idc] = tok_1;
                    tok_1 = strtok(NULL, " ");
                    idc++;
                }
                cmd_0[idc] = NULL;
                if (execvp(cmd_0[0], cmd_0) == -1) {
                    perror("Failed to execute the command");
                    exit(EXIT_FAILURE);
                }
             }
             else {
                int child_status;
                wait(&child_status);
                gettimeofday(&endTime,NULL);
                addCommandtoHis(command, f_0, startTime, endTime);
            }
        }
        else {
            // num_cmds commands, create num_cmds pipes so last child writes to a pipe that parent reads
            int num_cmds = count + 1;
            int num_pipes = num_cmds; // follow original code pattern: N pipes for N commands (last pipe used for parent read)
            int (*pipes)[2] = malloc(num_pipes * sizeof(int[2]));
            if (pipes == NULL) {
                perror("Pipe array allocation failed");
                free(cmds);
                exit(EXIT_FAILURE);
            }
            for (int p = 0; p < num_pipes; p++) {
                if (pipe(pipes[p]) == -1) {
                    perror("Pipe failed");
                    // close already opened
                    for (int q = 0; q < p; q++) {
                        close(pipes[q][0]);
                        close(pipes[q][1]);
                    }
                    free(pipes);
                    free(cmds);
                    exit(EXIT_FAILURE);
                }
            }

            pid_t *child_pids = malloc(num_cmds * sizeof(pid_t));
            if (child_pids == NULL) {
                perror("Child pids allocation failed");
                for (int p = 0; p < num_pipes; p++) {
                    close(pipes[p][0]); close(pipes[p][1]);
                }
                free(pipes);
                free(cmds);
                exit(EXIT_FAILURE);
            }

            for (int cmd_i = 0; cmd_i < num_cmds; cmd_i++) {
                pid_t pid = fork();
                if (pid < 0) {
                    perror("Fork failed");
                    // cleanup
                    for (int p = 0; p < num_pipes; p++) { close(pipes[p][0]); close(pipes[p][1]); }
                    free(pipes);
                    free(child_pids);
                    free(cmds);
                    exit(EXIT_FAILURE);
                } 
                else if (pid == 0) {
                    // Child process
                    // If not first command, redirect stdin to previous pipe read end
                    if (cmd_i > 0) {
                        if (dup2(pipes[cmd_i - 1][0], STDIN_FILENO) == -1) {
                            perror("dup2 stdin failed");
                            exit(EXIT_FAILURE);
                        }
                    }
                    // Always redirect stdout to current pipe write end (including last), so parent can read final output
                    if (dup2(pipes[cmd_i][1], STDOUT_FILENO) == -1) {
                        perror("dup2 stdout failed");
                        exit(EXIT_FAILURE);
                    }
                    // Close all pipe fds in child
                    for (int p = 0; p < num_pipes; p++) {
                        close(pipes[p][0]);
                        close(pipes[p][1]);
                    }
                    // Exec the command
                    execlp("sh", "sh", "-c", cmds[cmd_i], NULL);
                    perror("Execlp failed");
                    exit(EXIT_FAILURE);
                } 
                else {
                    child_pids[cmd_i] = pid;
                }
            }

            // Parent: close all write ends, keep final read end open to capture output
            for (int p = 0; p < num_pipes; p++) {
                close(pipes[p][1]);
                // keep pipes[p][0] open; we'll read only pipes[num_pipes-1][0]
            }

            // Read final output from last pipe (pipes[num_pipes-1][0])
            int final_read_fd = pipes[num_pipes - 1][0];
            char buffer[1024];
            ssize_t bytes_read;
            while ((bytes_read = read(final_read_fd, buffer, sizeof(buffer))) > 0) {
                write(STDOUT_FILENO, buffer, bytes_read);
            }
            close(final_read_fd);

            // Close other read ends
            for (int p = 0; p < num_pipes - 1; p++) {
                close(pipes[p][0]);
            }

            // Wait for all children
            for (int w = 0; w < num_cmds; w++) {
                waitpid(child_pids[w], &status, 0);
            }

            // Build command string for history: cmd0|cmd1|...|cmdN
            size_t total_len = 1;
            for (int j = 0; j < num_cmds; j++) {
                total_len += strlen(cmds[j]);
                if (j < num_cmds - 1) total_len += 1;
            }
            char *cmdd = malloc(total_len);
            if (cmdd == NULL) {
                perror("Memory allocation failed");
                free(pipes);
                free(child_pids);
                free(cmds);
                exit(EXIT_FAILURE);
            }
            cmdd[0] = '\0';
            for (int j = 0; j < num_cmds; j++) {
                strcat(cmdd, cmds[j]);
                if (j < num_cmds - 1) strcat(cmdd, "|");
            }

            gettimeofday(&endTime,NULL);
            addCommandtoHis(cmdd, child_pids[num_cmds - 1], startTime, endTime);

            free(cmdd);
            free(pipes);
            free(child_pids);
        }

        free(cmds);
        return 0;
}

int launchCommand(char* command ){
    return process_Creation_and_Running(command) ;
}

int launch_Script_file(char* bash ){
    FILE* bashFile = fopen(bash,"r") ;
    if (bashFile == NULL) {
        perror("Failed to open script file");
        return 1;
    }
    char line[MAX_INPUT_LENGTH];
    while (fgets(line, sizeof(line), bashFile) != NULL) {
        // Remove trailing newline character
        char* newline = strchr(line, '\n');
        if (newline != NULL) {
            *newline = '\0';
        }
        // Process each line of the script file here
        int status = launchCommand(line);
        if (status != 0) {
            fprintf(stderr, "Command execution failed: %s\n", line);
        }
    }
    fclose(bashFile);
    return 0;
}

char* userInputRead(){
    char* input = malloc(MAX_INPUT_LENGTH); //allocating memory for input
    if (input == NULL) {
        perror("Memory allocation error");
        exit(EXIT_FAILURE);
    }
    // Read user input
    if (fgets(input, MAX_INPUT_LENGTH, stdin) == NULL) {
        free(input);
        return NULL; //end of input or error
    }
    // Removing the trailing newline character if present
    size_t len = strlen(input);
    if (len > 0 && input[len - 1] == '\n') {
        input[len - 1] = '\0';
    }
    return input;
}

void shellLoop(){
    int status;
    char* last_command = NULL; //stores the last entered command
    do {
        printf("$ ");
        char* command = userInputRead();
        if (command != NULL) {
            //check if the entered command is not the same as the previous one
            if (last_command == NULL || strcmp(command, last_command) != 0) {
                addCommandtoHis(command, -1, (struct timeval){0}, (struct timeval){0});  //cmd,pid,start time, endtime
                free(last_command); //free the previous command
                last_command = strdup(command); // Update the last_command
            }
            if (strcmp(command, "exit") == 0) {
                // Display command history with execution details before exiting
                for (int i = 0; i < CMDHIS_ind; i++) {
                    printf("Command: %s\n", CMDHIS[i].command);
                    printf("Process ID: %d\n", CMDHIS[i].pid);
                    printf("Start Time: %ld.%06ld seconds\n", CMDHIS[i].startTime.tv_sec, CMDHIS[i].startTime.tv_usec);
                    printf("End Time: %ld.%06ld seconds\n", CMDHIS[i].endTime.tv_sec, CMDHIS[i].endTime.tv_usec);
                    long sec = CMDHIS[i].endTime.tv_sec - CMDHIS[i].startTime.tv_sec;
                    long usec = CMDHIS[i].endTime.tv_usec - CMDHIS[i].startTime.tv_usec;
                    if (usec < 0) { sec -= 1; usec += 1000000; }
                    printf("Duration: %ld.%06ld seconds\n", sec, usec);
                    printf("\n");
                }
                free(command);
                free(last_command);
                break;
            } else if (strcmp(command, "history") == 0) {
                displayCommands();
            } else if (strncmp(command, "prince", 6) == 0) {
                char* filename = command + 7;
                status = launch_Script_file(filename);
                if (status != 0) {
                    fprintf(stderr, "Failed to execute script: %s\n", filename);
                }
            } else {
                status = launchCommand(command);
                if (status != 0) {
                    fprintf(stderr, "Command execution failed: %s\n", command);
                }
            }
        }free(command); 
    } while (1);
}
int main(){
    shellLoop() ;
    return 0 ;
}
