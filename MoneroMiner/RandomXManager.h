#pragma once

#include "randomx.h"
#include <string>
#include <vector>
#include <mutex>

// Forward declaration
class MiningThreadData;

class RandomXManager {
public:
    static bool initializeDataset();
    static void cleanup();
    static randomx_vm* createVM();
    static void destroyVM(randomx_vm* vm);
    static bool calculateHash(randomx_vm* vm, const uint8_t* input, size_t inputSize, uint8_t* output);
    static bool verifyHash(const std::vector<uint8_t>& input, const uint8_t* expectedHash);
    static void handleSeedHashChange(const std::string& newSeedHash);
    static bool initializeRandomX(const std::string& seedHash);
    static bool isDatasetValid(const std::string& seedHash);
    static bool saveDataset(const std::string& seedHash);
    static bool loadDataset(const std::string& seedHash);

private:
    static randomx_cache* cache;
    static randomx_dataset* dataset;
    static std::mutex mutex;
    static bool initialized;
    static std::string currentSeedHash;
    static std::mutex seedHashMutex;
    static std::mutex initMutex;
    static std::vector<MiningThreadData*> threadData;
    static randomx_vm* currentVM;
}; 