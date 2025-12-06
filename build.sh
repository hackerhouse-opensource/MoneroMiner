#!/bin/bash
# MoneroMiner Linux Build Script

set -e

echo "======================================"
echo "MoneroMiner Linux Build System"
echo "======================================"
echo ""

# Check for required tools
check_dependency() {
    if ! command -v $1 &> /dev/null; then
        echo "ERROR: $1 is not installed"
        echo "Install with: sudo apt install $2"
        exit 1
    fi
}

echo "Checking dependencies..."
check_dependency "g++" "g++"
check_dependency "cmake" "cmake"
check_dependency "make" "build-essential"
check_dependency "git" "git"

echo "✓ All dependencies found"
echo ""

# Check if we're in WSL
if grep -qi microsoft /proc/version; then
    echo "Detected WSL environment"
    echo ""
fi

# Check CPU features
echo "CPU Features:"
if grep -q "aes" /proc/cpuinfo; then
    echo "✓ AES-NI support detected"
else
    echo "⚠ Warning: No AES-NI support"
fi

if grep -q "avx2" /proc/cpuinfo; then
    echo "✓ AVX2 support detected"
fi
echo ""

# Detect number of cores
CORES=$(nproc)
echo "Building with $CORES CPU cores"
echo ""

# Build
echo "Starting build..."
make -j$CORES "$@"

echo ""
echo "======================================"
echo "Build Complete!"
echo "======================================"
echo ""
echo "Run the miner with: /bin/monerominer"
echo "or: make run"
echo ""
