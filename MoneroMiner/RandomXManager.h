#pragma once

#include <vector>
#include <string>
#include <mutex>
#include <memory>
#include <unordered_map>
#include <algorithm>
#include "randomx.h"
#include "Types.h"
#include "MiningThreadData.h"

// Forward declaration
class MiningThreadData;

// Structure to hold 256-bit value as 4 64-bit integers
struct uint256_t {
    uint64_t words[4];  // Most significant word first (big-endian)

    uint256_t() : words{0, 0, 0, 0} {}

    // Shift left by n bits
    void shift_left(int n) {
        if (n <= 0) return;
        if (n >= 256) {
            words[0] = words[1] = words[2] = words[3] = 0;
            return;
        }

        while (n > 0) {
            int shift = (n < 64) ? n : 64;
            uint64_t carry = 0;
            
            // Shift each word left
            for (int i = 3; i >= 0; i--) {
                uint64_t next_carry = words[i] >> (64 - shift);
                words[i] = (words[i] << shift) | carry;
                carry = next_carry;
            }
            
            n -= shift;
        }
    }

    // Compare with another 256-bit value
    bool operator<(const uint256_t& other) const {
        for (int i = 0; i < 4; i++) {
            if (words[i] < other.words[i]) return true;
            if (words[i] > other.words[i]) return false;
        }
        return false;
    }
};

class RandomXManager {
public:
    static bool initialize(const std::string& seedHash);
    static void cleanup();
    static randomx_vm* createVM(int threadId);
    static void destroyVM(randomx_vm* vm);
    static bool calculateHash(randomx_vm* vm, const std::vector<uint8_t>& input, uint64_t nonce);
    static bool isInitialized() { return dataset != nullptr; }
    static std::string getCurrentSeedHash() { return currentSeedHash; }
    static void initializeDataset(const std::string& seedHash);
    static bool loadDataset(const std::string& seedHash);
    static bool saveDataset(const std::string& seedHash);
    static void handleSeedHashChange(const std::string& newSeedHash);
    static std::string currentTargetHex;
    static const std::vector<uint8_t>& getLastHash() { return lastHash; }
    static std::string getLastHashHex();
    static void setTarget(const std::string& targetHex);
    static void setJobInfo(uint64_t height, const std::string& jobId) {
        currentHeight = height;
        currentJobId = jobId;
    }

private:
    static std::mutex vmMutex;
    static std::mutex datasetMutex;
    static std::mutex seedHashMutex;
    static std::mutex initMutex;
    static std::mutex hashMutex;
    static std::unordered_map<int, randomx_vm*> vms;
    static randomx_cache* cache;
    static randomx_dataset* dataset;
    static std::string currentSeedHash;
    static bool initialized;
    static std::string datasetPath;
    static std::vector<MiningThreadData*> threadData;
    static std::string lastHashHex;
    static uint64_t currentHeight;
    static std::string currentJobId;
    static std::vector<uint8_t> lastHash;
    static uint256_t expandedTarget;
    static uint256_t hashValue;
    static uint32_t currentTarget;

    static bool checkHash(const uint8_t* hash, const std::string& targetHex);
    static std::string getDatasetPath(const std::string& seedHash);
}; 