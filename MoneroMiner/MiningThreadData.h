#pragma once

#include <memory>
#include <mutex>
#include <atomic>
#include <chrono>
#include <vector>
#include <string>
#include <cstdint>
#include "Types.h"
#include "Globals.h"

// Forward declarations
class HashBuffers;
struct randomx_vm;
namespace RandomXManager {
    randomx_vm* createVM();
    void calculateHash(randomx_vm* vm, const uint8_t* input, size_t inputSize, uint8_t* output);
    void destroyVM(randomx_vm* vm);
}

class MiningThreadData {
private:
    std::unique_ptr<HashBuffers> hashBuffers;
    randomx_vm* vm;
    std::mutex vmMutex;
    bool vmInitialized;
    int threadId;
    uint64_t hashCount{0};
    uint64_t totalHashCount{0};
    int elapsedSeconds{0};
    std::string currentJobId;
    uint32_t currentNonce{0};

public:
    static const unsigned int BATCH_SIZE = 256;
    std::atomic<bool> isRunning;
    std::chrono::steady_clock::time_point startTime;

    MiningThreadData(int id);
    ~MiningThreadData();

    int getThreadId() const { return threadId; }
    uint64_t getHashCount() const { return hashCount; }
    uint64_t getTotalHashCount() const { return totalHashCount; }
    int getElapsedSeconds() const { return elapsedSeconds; }
    std::string getCurrentJobId() const { return currentJobId; }
    uint32_t getCurrentNonce() const { return currentNonce; }
    double getHashrate() const {
        if (elapsedSeconds > 0) {
            return static_cast<double>(hashCount) / elapsedSeconds;
        }
        return 0.0;
    }

    void setHashCount(uint64_t count) { hashCount = count; }
    void setTotalHashCount(uint64_t count) { totalHashCount = count; }
    void setElapsedSeconds(int seconds) { elapsedSeconds = seconds; }
    void setCurrentJobId(const std::string& jobId) { currentJobId = jobId; }
    void setCurrentNonce(uint32_t nonce) { currentNonce = nonce; }

    bool initializeVM();
    bool calculateHash(const std::vector<uint8_t>& blob, uint8_t* outputHash, uint32_t currentDebugCounter = 0);
    void cleanup();
    randomx_vm* getVM() const { return vm; }

    MiningThreadData(const MiningThreadData&) = delete;
    MiningThreadData& operator=(const MiningThreadData&) = delete;
    MiningThreadData(MiningThreadData&&) = delete;
    MiningThreadData& operator=(MiningThreadData&&) = delete;
}; 