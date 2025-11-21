#ifndef SIMPLE_MULTITHREADER_H
#define SIMPLE_MULTITHREADER_H

#include <iostream>
#include <functional>
#include <stdlib.h>
#include <cstring>
#include <pthread.h>
#include <sys/time.h>
#include <algorithm> // Required for std::fill and std::min
#include <errno.h>

// Shared Helper Functions (Modularity)                      
// Helper to calculate the start and end index for a specific thread
// evenly distributing the remainder.
inline void compute_chunk_range(int t_idx, int range, int numThreads, int base_low, int* out_low, int* out_high) {
    int chunk = range / numThreads;
    int remainder = range % numThreads;
    
    // Calculate starting offset using std::min for cleaner logic
    int offset = (t_idx * chunk) + std::min(t_idx, remainder);
    
    int my_size = chunk + (t_idx < remainder ? 1 : 0);
    
    *out_low = base_low + offset;
    *out_high = *out_low + my_size;
}

// Helper to measure and print execution time
inline void print_elapsed_time(struct timeval start) {
    struct timeval end;
    gettimeofday(&end, NULL);
    double time_taken = (end.tv_sec - start.tv_sec) * 1e6;
    time_taken = (time_taken + (end.tv_usec - start.tv_usec)) * 1e-6;
    std::cout << "Total execution time: " << time_taken << " seconds" << std::endl;
}

// Helper for error checking
inline void check_pthread_error(int rc, const char* msg) {
    if (rc != 0) {
        std::cerr << msg << " Error code: " << rc << std::endl;
        exit(1);
    }
}

// 1D Implementation   

typedef struct {
    int low, high;
    std::function<void(int)>* lambda; 
} ThreadArgs1D;

void* thread_worker_1d(void* arg) {
    ThreadArgs1D* args = (ThreadArgs1D*)arg;
    for (int i = args->low; i < args->high; ++i) {
        (*args->lambda)(i);
    }
    return NULL;
}

void parallel_for(int low, int high, std::function<void(int)> &&lambda, int numThreads) {
    struct timeval start;
    gettimeofday(&start, NULL);

    // 1. Handle Invalid or Empty Ranges Gracefully
    if (high <= low) {
        print_elapsed_time(start);
        return;
    }

    // 2. Secure Lambda Lifetime
    std::function<void(int)> func = std::move(lambda);

    // 3. Clamp numThreads to Range (Prevent empty-work threads)
    int range = high - low;
    if (numThreads > range) numThreads = range;
    if (numThreads < 1) numThreads = 1;

    // 4. Allocate Resources
    int num_helpers = numThreads - 1;
    pthread_t* threads = nullptr;
    if (num_helpers > 0) {
        threads = new pthread_t[num_helpers];
    }
    
    ThreadArgs1D* args = new ThreadArgs1D[numThreads];

    // 5. Launch Helper Threads
    for (int i = 0; i < num_helpers; ++i) {
        compute_chunk_range(i, range, numThreads, low, &args[i].low, &args[i].high);
        args[i].lambda = &func;
        
        int rc = pthread_create(&threads[i], NULL, thread_worker_1d, &args[i]);
        check_pthread_error(rc, "Failed to create 1D thread.");
    }

    // 6. Main Thread Work (Last Chunk)
    int main_idx = numThreads - 1;
    compute_chunk_range(main_idx, range, numThreads, low, &args[main_idx].low, &args[main_idx].high);
    args[main_idx].lambda = &func;
    thread_worker_1d(&args[main_idx]);

    // 7. Join Helper Threads
    for (int i = 0; i < num_helpers; ++i) {
        int rc = pthread_join(threads[i], NULL);
        check_pthread_error(rc, "Failed to join 1D thread.");
    }

    // 8. Cleanup
    if (threads) delete[] threads;
    delete[] args;
    
    print_elapsed_time(start);
}

// 2D Implementation                                         

typedef struct {
    int low1, high1;
    int low2, high2;
    std::function<void(int, int)>* lambda;
} ThreadArgs2D;

void* thread_worker_2d(void* arg) {
    ThreadArgs2D* args = (ThreadArgs2D*)arg;
    for (int i = args->low1; i < args->high1; ++i) {
        for (int j = args->low2; j < args->high2; ++j) {
            (*args->lambda)(i, j);
        }
    }
    return NULL;
}

void parallel_for(int low1, int high1, int low2, int high2, std::function<void(int, int)> &&lambda, int numThreads) {
    struct timeval start;
    gettimeofday(&start, NULL);

    if (high1 <= low1) {
        print_elapsed_time(start);
        return;
    }

    std::function<void(int, int)> func = std::move(lambda);

    int range = high1 - low1;
    if (numThreads > range) numThreads = range;
    if (numThreads < 1) numThreads = 1;

    int num_helpers = numThreads - 1;
    pthread_t* threads = nullptr;
    if (num_helpers > 0) {
        threads = new pthread_t[num_helpers];
    }

    ThreadArgs2D* args = new ThreadArgs2D[numThreads];

    for (int i = 0; i < num_helpers; ++i) {
        compute_chunk_range(i, range, numThreads, low1, &args[i].low1, &args[i].high1);
        args[i].low2 = low2;
        args[i].high2 = high2;
        args[i].lambda = &func;

        int rc = pthread_create(&threads[i], NULL, thread_worker_2d, &args[i]);
        check_pthread_error(rc, "Failed to create 2D thread.");
    }

    int main_idx = numThreads - 1;
    compute_chunk_range(main_idx, range, numThreads, low1, &args[main_idx].low1, &args[main_idx].high1);
    args[main_idx].low2 = low2;
    args[main_idx].high2 = high2;
    args[main_idx].lambda = &func;
    thread_worker_2d(&args[main_idx]);

    for (int i = 0; i < num_helpers; ++i) {
        int rc = pthread_join(threads[i], NULL);
        check_pthread_error(rc, "Failed to join 2D thread.");
    }

    if (threads) delete[] threads;
    delete[] args;

    print_elapsed_time(start);
}


int user_main(int argc, char **argv);

void demonstration(std::function<void()> && lambda) {
  lambda();
}

int main(int argc, char **argv) {
  int x=5,y=1;
  auto lambda1 = [ x, &y ](void) {
    y = 5;
    std::cout<<"====== Welcome to Assignment-"<<y<<" of the CSE231(A) ======\n";
  };
  demonstration(lambda1);

  int rc = user_main(argc, argv);
 
  auto lambda2 = []() {
    std::cout<<"====== Hope you enjoyed CSE231(A) ======\n";
  };
  demonstration(lambda2);
  return rc;
}
#define main user_main
#endif // SIMPLE_MULTITHREADER_H