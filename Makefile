# Compiler and flags
CC = gcc
# Using C11 with GNU Extensions for `struct sigaction`
CFLAGS = -std=gnu11 -g3 -Wall -Wextra -pedantic -O2 -g

# Directories
SRC_DIR = src
OBJ_DIR = obj
BIN_DIR = bin

# Binary name
BINARY_NAME := lvs

# Source files and object files
SRC_FILES = $(wildcard $(SRC_DIR)/*.c)
OBJ_FILES = $(patsubst $(SRC_DIR)/%.c,$(OBJ_DIR)/%.o,$(SRC_FILES))

# Executable name
EXECUTABLE = $(BIN_DIR)/$(BINARY_NAME)

# Exec parameters
EXEC_PARAMS := --listen 8080 --dir .

.PHONY: all clean run

# Default target
all: $(EXECUTABLE)

# Rule to build the execute // NOTE: Address this targetable
$(EXECUTABLE): $(OBJ_FILES)
	$(CC) $(CFLAGS) $^ -o $@

# Rule to compile source files into object files
$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c
	$(CC) $(CFLAGS) -c $< -o $@

# Clean the project
clean:
	rm -f $(OBJ_DIR)/*.o $(EXECUTABLE)

# Compile and run compiled binary
run: all
	$(EXECUTABLE) $(EXEC_PARAMS)
