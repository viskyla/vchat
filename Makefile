# ==========================================================================
# Makefile for Starlight Project
# ==========================================================================

#
#  Project Structure:
#
#  - bin/        : Output binaries
#  - build/      : Intermediate object files and libraries
#  - include/    : Header files
#  - lib/        : Library sources (xdg-shell in this case)
#  - src/        : Source files
#  - xdg-shell/  : xdg-shell protocol definition (assumed location)
#

# --------------------------------------------------------------------------
# Compiler and Flags
# --------------------------------------------------------------------------

CC = gcc
CXX = g++
CFLAGS = -Wall -std=c17 -I./include -fPIC -I/usr/include -MMD -MP
CXXFLAGS = -Wall -std=c++17 -I./include -I/usr/include -MMD -MP
LDFLAGS = -lenet -lncurses


# --------------------------------------------------------------------------
# Directories
# --------------------------------------------------------------------------

SRC_DIR = src
LIB_DIR = lib
BUILD_DIR = build
INCLUDE_DIR = include

EXEC = vchat

# Sources
C_SOURCES := $(shell find $(SRC_DIR) $(LIB_DIR) -type f -name '*.c')
CPP_SOURCES := $(shell find $(SRC_DIR) -type f -name '*.cpp')

# Object files (preserve path)
C_OBJECTS := $(patsubst %.c, $(BUILD_DIR)/%.o, $(C_SOURCES))
CPP_OBJECTS := $(patsubst $(SRC_DIR)/%.cpp, $(BUILD_DIR)/%.o, $(CPP_SOURCES))
OBJECTS := $(C_OBJECTS) $(CPP_OBJECTS)

# Include the dependency files.  This is what makes the magic happen.
DEPENDS := $(OBJECTS:.o=.d)
-include $(DEPENDS)

all: $(EXEC)

# Generic C build rule
$(BUILD_DIR)/%.o: %.c
	@echo "Compiling C: $<"
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

# Generic C++ build rule
$(BUILD_DIR)/%.o: $(SRC_DIR)/%.cpp
	@echo "Compiling C++: $<"
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@

# Static library from protocol source
$(LIBRARY): $(BUILD_DIR)/lib/xdg-shell-protocol.o
	ar rcs $@ $^

# Linking
$(EXEC): $(OBJECTS) $(LIBRARY) $(MAIN_OBJECT)
	@echo "Linking executable: $@"
	@mkdir -p bin
	$(CXX) $(OBJECTS) $(MAIN_OBJECT) -o $@ $(LDFLAGS)

# Cleanup
clean:
	@echo "Cleaning"
	rm -rf $(BUILD_DIR) $(EXEC)

re: clean all

run: $(EXEC)
	./$(EXEC)

.PHONY: all clean re run