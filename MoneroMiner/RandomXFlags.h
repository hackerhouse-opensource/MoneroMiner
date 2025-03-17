#pragma once

#include "randomx.h"

// Wrapper class for safe flag operations specific to RandomX
class RandomXFlags {
private:
    randomx_flags flags;

public:
    explicit RandomXFlags(int f) : flags(static_cast<randomx_flags>(f)) {}
    
    // Get the underlying flags
    randomx_flags get() const { return flags; }
    
    // Static methods for flag operations
    static randomx_flags combine(randomx_flags a, randomx_flags b) {
        return static_cast<randomx_flags>(static_cast<int>(a) | static_cast<int>(b));
    }
    
    static randomx_flags intersect(randomx_flags a, randomx_flags b) {
        return static_cast<randomx_flags>(static_cast<int>(a) & static_cast<int>(b));
    }
    
    void add(randomx_flags f) {
        flags = combine(flags, f);
    }
}; 