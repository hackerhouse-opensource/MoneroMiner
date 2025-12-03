#pragma once

#include <vector>
#include <string>
#include <mutex>
#include <shared_mutex>
#include <unordered_map>
#include "randomx.h"
#include "Types.h"

class MiningThreadData;

class RandomXManager {
public:
    // Initialization
    static bool initialize(const std::string& seedHash);
    static bool initializeVM(int threadId);
    static bool initializeCache(const std::string& seedHash);
    
    // VM management
    static bool createVM(int threadId);
    static randomx_vm* getVM(int threadId);
    static void cleanupVM(int threadId);
    static void cleanup();
    static void destroyVM(randomx_vm* vm);
    
    // Hash calculation - PRIMARY METHOD for mining threads
    static bool calculateHashForThread(int threadId, const std::vector<uint8_t>& input, uint64_t nonce);
    
    // Target/difficulty
    static bool setTargetAndDifficulty(const std::string& targetHex);
    static bool checkTarget(const uint8_t* hash);
    static double getDifficulty() { return currentDifficulty; }
    static double getCurrentDifficulty() { return currentDifficulty; }
    
    // Last hash result
    static std::vector<uint8_t> getLastHash();
    static std::string getLastHashHex();
    
    // Dataset management
    static bool createDataset();
    static bool loadDataset(const std::string& filename);
    static bool saveDataset(const std::string& filename);
    static std::string getDatasetPath(const std::string& seedHash);
    
    // Utility
    static void handleSeedHashChange(const std::string& newSeedHash);
    
    // Getters
    static bool isInitialized() { return initialized; }
    static const std::string& getCurrentSeedHash() { return currentSeedHash; }

private:
    static std::shared_mutex vmMutex;
    static std::mutex initMutex;
    static std::mutex hashMutex;
    static std::mutex cacheMutex;
    static std::mutex seedHashMutex;
    
    static std::unordered_map<int, randomx_vm*> vms;
    static randomx_cache* cache;
    static randomx_dataset* dataset;
    static std::string currentSeedHash;
    static bool initialized;
    static bool useLightMode;
    
    static std::vector<uint8_t> lastHash;
    static uint256_t expandedTarget;
    static double currentDifficulty;
    static int flags;
};