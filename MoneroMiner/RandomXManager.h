#pragma once

#include <vector>
#include <string>
#include <mutex>
#include <shared_mutex>
#include <memory>
#include <unordered_map>
#include <algorithm>
#include "randomx.h"  // Now found via include path to RandomX/src
#include "Types.h"
#include "MiningThreadData.h"
#include <iomanip>
#include <sstream>
#include <atomic>

// Forward declaration
class MiningThreadData;

class RandomXManager {
public:
    // Core initialization and cleanup
    static bool initialize(const std::string& seedHash);
    static void cleanup();
    static void handleSeedHashChange(const std::string& newSeedHash);

    // VM management
    static bool initializeVM(int threadId);
    static randomx_vm* getVM(int threadId);
    static void destroyVM(randomx_vm* vm);

    // Hash calculation and validation
    static bool calculateHash(randomx_vm* vm, const std::vector<uint8_t>& input, uint64_t nonce);
    static bool checkTarget(const uint8_t* hash);
    static bool checkHash(const uint8_t* hash, const std::string& targetHex);
    static std::string getLastHashHex();

    // Target and difficulty management
    static bool setTargetAndDifficulty(const std::string& targetHex);
    static void setTarget(const std::string& targetHex);
    static bool calculateDifficulty(const std::string& targetHex, double& difficulty);

    // Accessors
    static bool isInitialized() { return initialized; }
    static std::string getCurrentSeedHash() { return currentSeedHash; }
    static const std::vector<uint8_t>& getLastHash() { return lastHash; }
    static double getDifficulty() { return currentDifficulty; }
    static const uint256_t& getExpandedTarget() { return expandedTarget; }
    static std::string getTargetHex() { return currentTargetHex; }
    
    // Job management
    static void setJobInfo(uint64_t height, const std::string& jobId) {
        currentHeight = height;
        currentJobId = jobId;
    }

private:
    // Private helper functions
    static bool initializeCache(const std::string& seedHash);
    static bool createDataset();
    static bool loadDataset(const std::string& seedHash);
    static bool saveDataset(const std::string& seedHash);
    static void initializeDataset(const std::string& seedHash);
    static std::string getDatasetPath(const std::string& seedHash);
    static bool expandTarget(const std::string& targetHex);
    static double calculateDifficulty(uint8_t exponent, uint32_t mantissa);
    static bool createVM(int threadId);
    static void cleanupVM(int threadId);

    // Add these new method declarations
    static bool verifyHashResult(const uint8_t* hashBuffer);
    static bool processHashResult(const uint8_t* hashBuffer);
    static bool verifyVM(randomx_vm* vm);
    static bool prepareInput(const std::vector<uint8_t>& input, uint64_t nonce, std::vector<uint8_t>& localInput);

    // Mutexes for thread safety
    static std::shared_mutex vmMutex;  // Changed from std::mutex to std::shared_mutex
    static std::mutex datasetMutex;
    static std::mutex seedHashMutex;
    static std::mutex initMutex;
    static std::mutex hashMutex;
    static std::mutex cacheMutex;

    // RandomX resources
    static randomx_cache* cache;
    static randomx_dataset* dataset;
    static std::unordered_map<int, randomx_vm*> vms;

    // State tracking
    static std::string currentSeedHash;
    static bool initialized;
    static std::vector<MiningThreadData*> threadData;
    static std::string currentTargetHex;
    static std::vector<uint8_t> lastHash;
    static uint64_t currentHeight;
    static std::string currentJobId;
    static uint256_t expandedTarget;
    static uint256_t hashValue;
    static uint32_t currentTarget;
    static std::string lastHashHex;
    static double currentDifficulty;

    // Add atomic flag for VM initialization status
    static std::atomic<bool> vmInitialized;

    // Add this with the other static members
    static int flags;

    // Add to private section
    static constexpr size_t BUFFER_ALIGNMENT = 64;
    static constexpr size_t INPUT_BUFFER_SIZE = 256;
    
    // Optimization: Pre-warm dataset cache on first use
    static bool warmupDataset();
};