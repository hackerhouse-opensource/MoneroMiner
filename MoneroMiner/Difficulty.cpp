#include "Difficulty.h"
#include "Types.h"
#include <algorithm>
#include <cstring>

namespace Difficulty {
    uint64_t targetToDifficulty(const uint8_t* target) {
        // Read the least significant 64-bit word (little-endian)
        uint64_t lsw = 0;
        for (int i = 0; i < 8; i++) {
            lsw |= static_cast<uint64_t>(target[i]) << (i * 8);
        }
        
        if (lsw == 0) return 1;
        return 0xFFFFFFFFFFFFFFFFULL / lsw;
    }
    
    void difficultyToTarget(uint64_t difficulty, uint8_t* target) {
        // Calculate target = 0xFFFFFFFFFFFFFFFF / difficulty (in first 64 bits)
        std::memset(target, 0, 32);
        
        if (difficulty == 0) difficulty = 1;
        uint64_t targetValue = 0xFFFFFFFFFFFFFFFFULL / difficulty;
        
        // Store as little-endian in first 8 bytes
        for (int j = 0; j < 8; j++) {
            target[j] = static_cast<uint8_t>((targetValue >> (j * 8)) & 0xFF);
        }
        // Bytes 8-31 remain zero
    }
    
    bool meetsTarget(const uint8_t* hash, const uint8_t* target) {
        // Use uint256_t for proper comparison
        uint256_t hashValue(hash);
        uint256_t targetValue(target);
        return hashValue < targetValue;
    }
    
    void expandTarget(const std::string& compactTarget, uint8_t* expandedTarget) {
        std::memset(expandedTarget, 0, 32);
        
        if (compactTarget.length() == 8) {
            uint32_t compact = static_cast<uint32_t>(std::stoul(compactTarget, nullptr, 16));
            uint64_t diff = (compact & 0x00FFFFFF) * (1ULL << (8 * ((compact >> 24) - 3)));
            difficultyToTarget(diff, expandedTarget);
        } else if (compactTarget.length() == 16) {
            uint64_t diff = std::stoull(compactTarget, nullptr, 16);
            difficultyToTarget(diff, expandedTarget);
        } else if (compactTarget.length() == 64) {
            for (size_t i = 0; i < 32; ++i) {
                std::string byteStr = compactTarget.substr(i * 2, 2);
                expandedTarget[i] = static_cast<uint8_t>(std::strtoul(byteStr.c_str(), nullptr, 16));
            }
        }
    }
}
