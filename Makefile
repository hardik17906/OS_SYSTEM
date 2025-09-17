CFLAGS = -Wall -Wextra -g -D_XOPEN_SOURCE=700
LDFLAGS = -lpthread
SRC = simple-shell.c
OBJ = $(SRC:.c=.o)
TARGET = simpleshell

# Default target
all: $(TARGET)

# Linking step
$(TARGET): $(OBJ)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

# Compilation step
%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

# Run the shell
run: $(TARGET)
	./$(TARGET)

# Clean build artifacts
clean:
	rm -f $(OBJ) $(TARGET)

# Rebuild from scratch
rebuild: clean all
