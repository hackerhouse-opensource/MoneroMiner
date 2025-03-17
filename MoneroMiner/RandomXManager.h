#pragma once

#include "randomx.h"
#include "MiningThreadData.h"
#include "Utils.h"
#include <string>
#include <vector>
#include <mutex>

// Forward declaration
class MiningThreadData;

// Error handling macros
#define CHECK_NULL(x, msg) if(x == nullptr) { \
    threadSafePrint(msg); \
    return false; \
}

namespace RandomXManager {
    // Global RandomX state
    extern randomx_cache* currentCache;
    extern randomx_dataset* currentDataset;
    extern std::string currentSeedHash;
    extern std::mutex cacheMutex;
    extern std::mutex seedHashMutex;
    extern std::vector<MiningThreadData*> threadData;

    // Core RandomX functions
    bool initializeRandomX(const std::string& seedHash);
    void cleanupRandomX();
    bool isDatasetValid(const std::string& filename, const std::string& currentSeedHash);
    void saveDataset(randomx_dataset* dataset, const std::string& filename, const std::string& seedHash);
    void loadDataset(randomx_dataset* dataset, const std::string& filename);

    // VM Management functions
    randomx_vm* createVM();
    void calculateHash(randomx_vm* vm, const uint8_t* input, size_t inputSize, uint8_t* output);
    void destroyVM(randomx_vm* vm);
    void handleSeedHashChange(const std::string& newSeedHash);
    std::vector<MiningThreadData*> getThreadData();
} 