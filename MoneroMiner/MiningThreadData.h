#pragma once

#include <memory>
#include <mutex>
#include <atomic>
#include <chrono>
#include <vector>
#include <string>
#include <cstdint>
#include <thread>
#include "Types.h"
#include "HashBuffers.h"
#include "Job.h"
#include "RandomXManager.h"
#include "randomx.h"

// Forward declarations
struct randomx_vm;

class MiningThreadData {
public:
    MiningThreadData(int id) : threadId(id), vm(nullptr), vmInitialized(false), 
                              running(false), hashCount(0), totalHashCount(0),
                              acceptedShares(0), rejectedShares(0), currentNonce(0),
                              currentJob(nullptr) {
        startTime = std::chrono::steady_clock::now();
    }

    ~MiningThreadData();

    // Thread control
    void start();
    void stop();
    void mine();

    // VM management
    bool initializeVM();
    bool needsVMReinit(const std::string& newSeedHash) const;

    // Job management
    void updateJob(const Job& job);
    bool hasJob() const { return currentJob != nullptr; }
    Job* getCurrentJob() const { return currentJob; }
    uint64_t getNonce() const { return currentNonce; }
    void setNonce(uint64_t n) { currentNonce = n; }

    // Hash calculation
    bool calculateHash(const std::vector<uint8_t>& input, uint64_t nonce);
    void submitShare(const std::vector<uint8_t>& hash);

    // Stats
    double getHashrate() const;
    int getThreadId() const { return threadId; }
    uint64_t getHashCount() const { return hashCount; }
    uint64_t getTotalHashCount() const { return totalHashCount; }
    uint64_t getAcceptedShares() const { return acceptedShares; }
    uint64_t getRejectedShares() const { return rejectedShares; }
    void incrementHashCount() { hashCount++; totalHashCount++; }

private:
    int threadId;
    randomx_vm* vm;
    bool vmInitialized;
    std::mutex vmMutex;
    std::thread thread;
    bool running;
    uint64_t hashCount;
    uint64_t totalHashCount;
    uint64_t acceptedShares;
    uint64_t rejectedShares;
    uint64_t currentNonce;
    Job* currentJob;
    std::mutex jobMutex;
    std::string currentSeedHash;
    std::chrono::steady_clock::time_point startTime;
}; 