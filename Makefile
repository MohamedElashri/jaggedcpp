# Compiler settings
CC = g++
CFLAGS = -std=c++11

# Source files and target executables
EXAMPLES = JaggedArray stat reduction concanate boolean_fancy sort
SRC_DIR = examples
OBJ_DIR = $(SRC_DIR)/obj

# Rule to build all examples
all: $(EXAMPLES)

# Generic rule to build an example
$(EXAMPLES): %: $(SRC_DIR)/%.cpp
	$(CC) $(CFLAGS) $< -o $(OBJ_DIR)/$@

# Rule to clean up
clean:
	rm -f $(OBJ_DIR)/*

# Create obj directory if it doesn't exist
$(shell mkdir -p $(OBJ_DIR))
