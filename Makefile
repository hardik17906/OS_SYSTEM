# Compiler and flags
CC = gcc
CFLAGS = -Wall -g
LDFLAGS = -lrt

# Executables
SHELL_EXE = new_shell
SCHED_EXE = scheduler
Pros1_EXE = Pros1
Pros2_EXE = Pros2
Pros3_EXE = Pros3

# Source files
SHELL_SRC = new_shell.c
SCHED_SRC = scheduler.c
Pros1_SRC = pros1.c
Pros2_SRC = pros.c
Pros3_SRC = pros3.c

# Object files
SHELL_OBJ = $(SHELL_SRC:.c=.o)
SCHED_OBJ = $(SCHED_SRC:.c=.o)
Pros1_OBJ = $(Pros1_SRC:.c=.o)
Pros2_OBJ = $(Pros2_SRC:.c=.o)
Pros3_OBJ = $(Pros3_SRC:.c=.o)

# Default target
.PHONY: all clean
all: $(SHELL_EXE) $(SCHED_EXE) $(Pros1_EXE) $(Pros2_EXE) $(Pros3_EXE)

# Build new_shell executable
$(SHELL_EXE): $(SHELL_OBJ)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)
	rm -f $(SHELL_OBJ)

# Build scheduler executable
$(SCHED_EXE): $(SCHED_OBJ)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)
	rm -f $(SCHED_OBJ)

# Build Pros executables
$(Pros1_EXE): $(Pros1_OBJ)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)
	rm -f $(Pros1_OBJ)

$(Pros2_EXE): $(Pros2_OBJ)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)
	rm -f $(Pros2_OBJ)

$(Pros3_EXE): $(Pros3_OBJ)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)
	rm -f $(Pros3_OBJ)

# Generic rule for compiling .c -> .o
%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

# Clean everything
clean:
	rm -f *.o $(SHELL_EXE) $(SCHED_EXE) $(Pros1_EXE) $(Pros2_EXE) $(Pros3_EXE)
