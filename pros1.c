/*
 * This program is designed to run for a significant amount of time without
 * terminating or blocking for I/O. Its purpose is to test the SimpleScheduler's
 * time-slicing and wait-time calculation. When more instances of this job
 * are submitted than there are available CPUs (NCPU), some instances will
 * be forced to wait in the ready queue, resulting in a non-zero wait time.
 */

#include <stdio.h>
#include <unistd.h>

// This header is essential for the job to be controlled by the scheduler.
// It intercepts the main function to make the process pause itself on startup.
#include "dummy_main.h"

int main(int argc, char **argv) {
    // Using 'volatile' tells the compiler not to optimize away this variable,
    volatile unsigned long long sum = 0;

    for (unsigned long long i = 0; i < 2000000000ULL; i++) {
        sum += i;
    }
    for (unsigned long long i = 0; i < 2000000000ULL; i++) {
        sum += i;
    }
    for (unsigned long long i = 0; i < 2000000000ULL; i++) {
        sum += i;
    }
    for (unsigned long long i = 0; i < 2000000000ULL; i++) {
        sum += i;
    }
    
    return 0;
}
