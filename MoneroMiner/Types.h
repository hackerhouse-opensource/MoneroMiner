#pragma once

#include <cstdint>
#include <array>

// 256-bit unsigned integer type for target calculations
struct uint256_t {
    uint64_t data[4]; // Little-endian: data[0] is LSW
    
    uint256_t() : data{0, 0, 0, 0} {}
    
    // Factory method: Convert difficulty to target
    static uint256_t fromDifficulty(uint64_t difficulty) {
        uint256_t result;
        if (difficulty == 0) {
            // Max target (all bits set)
            result.data[0] = 0xFFFFFFFFFFFFFFFFULL;
            result.data[1] = 0xFFFFFFFFFFFFFFFFULL;
            result.data[2] = 0xFFFFFFFFFFFFFFFFULL;
            result.data[3] = 0xFFFFFFFFFFFFFFFFULL;
        } else {
            // target = 0xFFFFFFFFFFFFFFFF / difficulty (64-bit threshold)
            result.data[0] = 0xFFFFFFFFFFFFFFFFULL / difficulty;
            result.data[1] = 0;
            result.data[2] = 0;
            result.data[3] = 0;
        }
        return result;
    }
    
    // Comparison operators
    bool operator<(const uint256_t& other) const {
        for (int i = 3; i >= 0; i--) {
            if (data[i] < other.data[i]) return true;
            if (data[i] > other.data[i]) return false;
        }
        return false;
    }
};
