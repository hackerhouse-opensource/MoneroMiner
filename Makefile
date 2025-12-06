# MoneroMiner - Linux Makefile
# Compiles RandomX library and MoneroMiner from Windows codebase

CXX = g++
CC = gcc

# Detect CPU vendor for architecture-specific flags
CPU_VENDOR := $(shell grep -m1 'vendor_id' /proc/cpuinfo 2>/dev/null | grep -o 'AMD\|Intel' || echo unknown)

# Compiler flags with AMD Zen optimizations and LTO
ifeq ($(CPU_VENDOR),AMD)
CXXFLAGS = -std=c++17 -O3 -march=znver2 -mtune=znver2 -mavx2 -mbmi2 -maes -Wall -Wextra -pthread -flto
CFLAGS = -O3 -march=znver2 -mtune=znver2 -mavx2 -mbmi2 -maes -Wall -Wextra -flto
$(info Building with AMD Zen optimizations + LTO)
else ifeq ($(CPU_VENDOR),Intel)
CXXFLAGS = -std=c++17 -O3 -march=native -Wall -Wextra -pthread -flto
CFLAGS = -O3 -march=native -Wall -Wextra -flto
$(info Building with Intel optimizations + LTO)
else
CXXFLAGS = -std=c++17 -O3 -march=native -Wall -Wextra -pthread -flto
CFLAGS = -O3 -march=native -Wall -Wextra -flto
$(info Building with generic optimizations + LTO)
endif

LDFLAGS = -pthread -ldl -flto

# Directories
SRC_DIR = MoneroMiner
BUILD_DIR = build
BIN_DIR = bin

# Detect RandomX source directory robustly:
# prefer 'RandomX' (capitalized) then 'randomx' to handle different checkouts/filesystems.
RANDOMX_DIR := $(shell if [ -d RandomX ]; then echo RandomX; elif [ -d randomx ]; then echo randomx; else echo ""; fi)
ifeq ($(RANDOMX_DIR),)
$(error RandomX source directory not found. Looked for 'RandomX' and 'randomx' in $(PWD))
endif
RANDOMX_BUILD = $(RANDOMX_DIR)/build

# Absolute RandomX source path and cache file (used to detect mismatched caches)
RANDOMX_SRC_ABS := $(shell cd $(RANDOMX_DIR) && pwd)
RANDOMX_CACHE := $(RANDOMX_BUILD)/CMakeCache.txt

# Include paths
INCLUDES = -I$(SRC_DIR) -I$(RANDOMX_DIR)/src

# Source files (excluding Windows-specific, old/unused files)
SOURCES = $(wildcard $(SRC_DIR)/*.cpp)
SOURCES := $(filter-out $(SRC_DIR)/framework.cpp $(SRC_DIR)/pch.cpp $(SRC_DIR)/main.cpp $(SRC_DIR)/MiningThread.cpp, $(SOURCES))

# Object files
OBJECTS = $(patsubst $(SRC_DIR)/%.cpp,$(BUILD_DIR)/%.o,$(SOURCES))

# Target executable
TARGET = $(BIN_DIR)/monerominer

# RandomX library
RANDOMX_LIB = $(RANDOMX_BUILD)/librandomx.a

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
	@if [ ! -d "$(RANDOMX_DIR)" ]; then \
		echo "Error: RandomX source directory '$(RANDOMX_DIR)' not found."; \
		echo "Looked for 'randomx' and 'RandomX' in $(PWD)."; \
		exit 1; \
	fi
	# If an existing CMakeCache points at a different source path, back it up to avoid path mismatch errors
	@if [ -f "$(RANDOMX_CACHE)" ]; then \
		old_src=$$(sed -n 's/^CMAKE_HOME_DIRECTORY:INTERNAL=//p' "$(RANDOMX_CACHE)" | tr -d '\r'); \
		if [ -n "$$old_src" ] && [ "$$old_src" != "$(RANDOMX_SRC_ABS)" ]; then \
			echo "Detected mismatched CMake cache (was generated for $$old_src)"; \
			backup="$(RANDOMX_BUILD).backup.$$(date +%s)"; \
			echo "Moving $(RANDOMX_BUILD) to $$backup to avoid path mismatch."; \
			mv "$(RANDOMX_BUILD)" "$$backup" || rm -rf "$(RANDOMX_BUILD)"; \
			mkdir -p "$(RANDOMX_BUILD)"; \
		fi; \
	fi
	@cd "$(RANDOMX_DIR)" && mkdir -p build && cd build && \
	cmake -DCMAKE_BUILD_TYPE=Release \
	      -DBUILD_SHARED_LIBS=OFF \
	      -DARCH=native \
	      .. && \
	$(MAKE) -j$(nproc)
	@echo "RandomX library built successfully"

# Build MoneroMiner
$(TARGET): $(OBJECTS)
	@echo "Linking $(TARGET)..."
	# Link static RandomX directly (no rpath required)
	$(CXX) $(OBJECTS) $(RANDOMX_LIB) -o $(TARGET) $(LDFLAGS)
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
	rm -rf build/*.o
	rm -f MoneroMiner

# Deep clean - remove everything including RandomX
distclean: clean
	@echo "Deep cleaning (including RandomX)..."
	rm -rf randomx/build
	rm -rf RandomX/build
	rm -f randomx_dataset_*.bin

# Clean RandomX only
clean-randomx:
	@echo "Cleaning RandomX library..."
	rm -rf randomx/build
	rm -rf RandomX/build
	@if [ -d "randomx/build" ]; then \
		cd randomx/build && $(MAKE) clean 2>/dev/null || true; \
	fi
	@if [ -d "RandomX/build" ]; then \
		cd RandomX/build && $(MAKE) clean 2>/dev/null || true; \
	fi

# Rebuild everything from scratch
rebuild: distclean all

# Run the miner
run: all
	@echo "Starting MoneroMiner..."
	$(TARGET)

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
