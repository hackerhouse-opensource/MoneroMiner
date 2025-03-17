#pragma once

#include <vector>
#include <cstdint>
#include <cstring>
#include "Constants.h"

// Forward declare RandomX constants
#ifndef RANDOMX_HASH_SIZE
#define RANDOMX_HASH_SIZE 32
#endif

// Class for managing aligned memory for RandomX operations
class HashBuffers {
private:
    alignas(64) uint64_t tempHash[8];  // 64-byte aligned buffer for intermediate hash
    alignas(64) uint8_t hash[RANDOMX_HASH_SIZE];  // 64-byte aligned buffer for final hash
    alignas(64) uint8_t scratchpad[RandomXConstants::SCRATCHPAD_L3];  // 64-byte aligned scratchpad

public:
    HashBuffers() {
        // Initialize buffers to zero
        std::memset(tempHash, 0, sizeof(tempHash));
        std::memset(hash, 0, sizeof(hash));
        std::memset(scratchpad, 0, sizeof(scratchpad));
        inputBuffer.resize(76);  // Standard Monero input size
        outputBuffer.resize(32); // Standard hash output size
    }

    uint64_t* getTempHash() { return tempHash; }
    uint8_t* getHash() { return hash; }
    uint8_t* getScratchpad() { return scratchpad; }

    std::vector<uint8_t> inputBuffer;
    std::vector<uint8_t> outputBuffer;
}; 