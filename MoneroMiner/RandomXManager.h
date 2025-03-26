#pragma once

#include <vector>
#include <string>
#include <mutex>
#include <memory>
#include <unordered_map>
#include "randomx.h"
#include "HashValidation.h"
#include "Types.h"

// Forward declaration
class MiningThreadData;

class RandomXManager {
public:
    static bool initialize(const std::string& seedHash);
    static void cleanup();
    static randomx_vm* createVM(int threadId);
    static void destroyVM(randomx_vm* vm);
    static bool calculateHash(randomx_vm* vm, const std::vector<uint8_t>& blob, uint64_t nonce);
    static bool verifyHash(const uint8_t* input, size_t inputSize, const uint8_t* expectedHash, int threadId);
    static bool isInitialized() { return dataset != nullptr; }
    static std::string getCurrentSeedHash() { return currentSeedHash; }
    static void initializeDataset(const std::string& seedHash);
    static bool loadDataset(const std::string& seedHash);
    static bool saveDataset(const std::string& seedHash);
    static bool validateDataset(const std::string& seedHash);
    static void handleSeedHashChange(const std::string& newSeedHash);
    static std::string currentTargetHex;

private:
    static std::mutex vmMutex;
    static std::mutex datasetMutex;
    static std::mutex seedHashMutex;
    static std::mutex initMutex;
    static std::unordered_map<int, randomx_vm*> vms;
    static randomx_cache* cache;
    static randomx_dataset* dataset;
    static std::string currentSeedHash;
    static bool initialized;
    static std::string datasetPath;
    static std::vector<MiningThreadData*> threadData;
    
    static std::string getDatasetPath(const std::string& seedHash);
}; 