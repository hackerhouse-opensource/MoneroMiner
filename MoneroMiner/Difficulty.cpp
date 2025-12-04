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
        // Use uint256_t from Types.h
        uint256_t targetValue = uint256_t::fromDifficulty(difficulty);
        
        // Convert to little-endian bytes
        for (int i = 0; i < 4; i++) {
            for (int j = 0; j < 8; j++) {
                target[i * 8 + j] = (targetValue.data[i] >> (j * 8)) & 0xFF;
            }
        }
    }
    
    bool meetsTarget(const uint8_t* hash, const uint8_t* target) {
        // CRITICAL FIX: Compare as little-endian 256-bit integers
        // Start from the MOST significant word (bytes 24-31) and work down
        for (int i = 3; i >= 0; i--) {
            uint64_t hashWord = 0;
            uint64_t targetWord = 0;
            
            // Extract 64-bit words in little-endian
            for (int j = 0; j < 8; j++) {
                hashWord |= static_cast<uint64_t>(hash[i * 8 + j]) << (j * 8);
                targetWord |= static_cast<uint64_t>(target[i * 8 + j]) << (j * 8);
            }
            
            if (hashWord < targetWord) return true;
            if (hashWord > targetWord) return false;
        }
        return true; // hash == target (valid)
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
