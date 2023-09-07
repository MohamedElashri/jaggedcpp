# Compiler settings
CC = g++
CFLAGS_11 = -std=c++11
CFLAGS_17 = -std=c++17

# Source files and target executables
EXAMPLES = JaggedArray stat reduction concanate boolean_fancy sort mask pad_none fill_none drop_none is_none
SRC_DIR = examples
OBJ_DIR = $(SRC_DIR)/obj

# Mapping from source files to C++ standard
CPP11_SOURCES =  
CPP17_SOURCES =  JaggedArray stat reduction concanate boolean_fancy  mask pad_none fill_none drop_none is_none sort
CPP23_SOURCES =

# Rule to build all examples
all: $(EXAMPLES)

# Generic rule to build a C++11 example
$(CPP11_SOURCES): %: $(SRC_DIR)/%.cpp
	$(CC) $(CFLAGS_11) $< -o $(OBJ_DIR)/$@

# Generic rule to build a C++17 example
$(CPP17_SOURCES): %: $(SRC_DIR)/%.cpp
	$(CC) $(CFLAGS_17) $< -o $(OBJ_DIR)/$@

# Generic rule to build a C++23 example
$(CPP23_SOURCES): %: $(SRC_DIR)/%.cpp
	$(CC) $(CFLAGS_23) $< -o $(OBJ_DIR)/$@	
 
# Add the EXAMPLES dependencies
$(EXAMPLES): | $(OBJ_DIR)

# Rule to clean up
clean:
	rm -f $(OBJ_DIR)/*

# Create obj directory if it doesn't exist
$(OBJ_DIR):
	mkdir -p $@
