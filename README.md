# Operating Systems Projects

A team collection of operating systems coursework in C and C++. It includes shell and scheduler implementations, POSIX thread exercises, and multithreaded vector and matrix examples. The history includes work by [Hardik](https://github.com/hardik17906) and [Harsh](https://github.com/Harsh060911).

## Components

| Files | Topic |
| --- | --- |
| [`new_shell.c`](new_shell.c), [`scheduler.c`](scheduler.c) | Shell and job scheduling experiment. |
| [`simple_shell.c`](simple_shell.c) | Earlier shell implementation. |
| [`simple-multithreader.h`](simple-multithreader.h) | Shared thread helpers used by the numerical examples. |
| [`vector.cpp`](vector.cpp), [`matrix.cpp`](matrix.cpp) | Parallel numerical exercises. |
| `pros1.c` to `pros3.c`, `fib.c`, `helloworld.c` | Small process and C examples. |

## Build

The code uses POSIX APIs (`pthread`, `fork`, shared memory, and related headers), so use Linux or WSL with GCC, G++, and Make. The existing [`Makefile`](Makefile) builds the vector and matrix programs:

```bash
make
```

The shell and scheduler have separate source files and are not built by that Makefile. See the included PDF and Word documents for the assignment context and design notes. This repository is coursework and is not a hardened production shell or scheduler.
