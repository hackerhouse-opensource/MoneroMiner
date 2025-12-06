# MoneroMiner - Linux Makefile
# Compiles RandomX library and MoneroMiner from Windows codebase

CXX = g++
CC = gcc

# Compiler flags
CXXFLAGS = -std=c++17 -O3 -march=native -Wall -Wextra -pthread
CFLAGS = -O3 -march=native -Wall -Wextra
LDFLAGS = -pthread -ldl

# Directories
SRC_DIR = MoneroMiner
BUILD_DIR = build
BIN_DIR = bin
RANDOMX_DIR = randomx
RANDOMX_BUILD = $(RANDOMX_DIR)/build

# Include paths
INCLUDES = -I$(SRC_DIR) -I$(RANDOMX_DIR)/src

# Source files (excluding Windows-specific and duplicates)
SOURCES = $(wildcard $(SRC_DIR)/*.cpp)
SOURCES := $(filter-out $(SRC_DIR)/framework.cpp $(SRC_DIR)/pch.cpp $(SRC_DIR)/main.cpp, $(SOURCES))

# Add Platform.cpp explicitly
SOURCES += $(SRC_DIR)/Platform.cpp

# Object files
OBJECTS = $(patsubst $(SRC_DIR)/%.cpp,$(BUILD_DIR)/%.o,$(SOURCES))

# Target executable
TARGET = $(BIN_DIR)/monerominer

# RandomX library
RANDOMX_LIB = $(RANDOMX_BUILD)/librandomx.so

# Default target
all: directories randomx $(TARGET)

# Create build directories
directories:
	@mkdir -p $(BUILD_DIR)
	@mkdir -p $(BIN_DIR)
	@mkdir -p $(RANDOMX_BUILD)

# Build RandomX library
randomx: directories
	@echo "Building RandomX library..."
	@cd $(RANDOMX_DIR) && mkdir -p build && cd build && \
	cmake -DCMAKE_BUILD_TYPE=Release \
	      -DBUILD_SHARED_LIBS=ON \
	      -DARCH=native \
	      .. && \
	$(MAKE) -j$(nproc)
	@echo "RandomX library built successfully"

# Build MoneroMiner
$(TARGET): $(OBJECTS)
	@echo "Linking $(TARGET)..."
	$(CXX) $(OBJECTS) -o $(TARGET) $(LDFLAGS) -L$(RANDOMX_BUILD) -lrandomx -Wl,-rpath,'$$ORIGIN/../../$(RANDOMX_BUILD)'
	@echo "Build complete: $(TARGET)"

# Compile source files
$(BUILD_DIR)/%.o: $(SRC_DIR)/%.cpp
	@echo "Compiling $<..."
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

# Install target (optional)
install: all
	@echo "Installing MoneroMiner..."
	install -d /usr/local/bin
	install -m 755 $(TARGET) /usr/local/bin/
	install -d /usr/local/lib
	install -m 755 $(RANDOMX_LIB) /usr/local/lib/
	ldconfig
	@echo "Installation complete"

# Clean build artifacts
clean:
	@echo "Cleaning build artifacts..."
	rm -rf $(BUILD_DIR)
	rm -rf $(BIN_DIR)

# Clean everything including RandomX
distclean: clean
	@echo "Cleaning RandomX build..."
	rm -rf $(RANDOMX_BUILD)

# Run the miner
run: all
	@echo "Starting MoneroMiner..."
	LD_LIBRARY_PATH=$(RANDOMX_BUILD):$$LD_LIBRARY_PATH $(TARGET)

# Debug build
debug: CXXFLAGS += -g -DDEBUG -O0
debug: CFLAGS += -g -DDEBUG -O0
debug: clean all

# Show build information
info:
	@echo "Compiler: $(CXX) $(shell $(CXX) --version | head -n1)"
	@echo "Flags: $(CXXFLAGS)"
	@echo "RandomX: $(RANDOMX_LIB)"
	@echo "Sources: $(words $(SOURCES)) files"
	@echo "Build directory: $(BUILD_DIR)"
	@echo "Output: $(TARGET)"

# Help target
help:
	@echo "MoneroMiner Linux Build System"
	@echo ""
	@echo "Targets:"
	@echo "  all        - Build everything (default)"
	@echo "  randomx    - Build RandomX library only"
	@echo "  clean      - Remove build artifacts"
	@echo "  distclean  - Remove all build files including RandomX"
	@echo "  install    - Install to system (/usr/local)"
	@echo "  run        - Build and run the miner"
	@echo "  debug      - Build with debug symbols"
	@echo "  info       - Show build configuration"
	@echo "  help       - Show this help message"

.PHONY: all directories randomx clean distclean install run debug info help
