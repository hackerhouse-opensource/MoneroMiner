#pragma once

#include <cstdint>
#include <string>
#include "Types.h"  // FIX: Include Types.h to get uint256_t definition

namespace Difficulty {
    // Convert 32-byte target to difficulty
    uint64_t targetToDifficulty(const uint8_t* target);
    
    // Convert difficulty to 32-byte target
    void difficultyToTarget(uint64_t difficulty, uint8_t* target);
    
    // Check if hash meets target
    bool meetsTarget(const uint8_t* hash, const uint8_t* target);
    
    // Expand compact target format to 32-byte target
    void expandTarget(const std::string& compactTarget, uint8_t* expandedTarget);
}
