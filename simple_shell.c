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
cmdHis CMDHIS[MAX_SIZE_OF_HISTORY] ;

// let's declare the global variable which will keep the index updated on which we are in our command array 
int CMDHIS_ind = 0 ; 

// Creating function which will be adding the commands to our CMDHIS array 
void addCommandtoHis(char*command , pid_t pid , Timeval startTime , Timeval endTime ){
    if ( pid !=-1 ){  // This implies that the process ID is valid 
        if ( CMDHIS_ind < MAX_SIZE_OF_HISTORY ){ // have enough space to add more command to history 
            CMDHIS[CMDHIS_ind].command = strdup(command) ; // 

        }

    }

}


