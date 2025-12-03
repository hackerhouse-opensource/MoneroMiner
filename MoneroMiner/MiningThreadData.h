#pragma once

#include <memory>
#include <mutex>
#include <atomic>
#include <chrono>
#include <vector>
#include <string>
#include <cstdint>
#include <thread>
#include <condition_variable>
#include <queue>
#include "Types.h"
#include "HashBuffers.h"
#include "Job.h"
#include "RandomXManager.h"
#include "randomx.h"

// Forward declarations
struct randomx_vm;

class MiningThreadData {
public:
    MiningThreadData(int id);
    ~MiningThreadData();

    // Thread control
    void start();
    void stop();
    void mine();

    // VM management
    bool initializeVM();
    bool needsVMReinit(const std::string& newSeedHash) const;
    bool calculateHash(const std::vector<uint8_t>& input, uint64_t nonce);
    void submitShare(const std::vector<uint8_t>& hash);

    // Job management
    void updateJob(const Job& job);
    bool hasJob() const { return currentJob != nullptr; }
    Job* getCurrentJob() const { return currentJob; }
    
    // Accessors
    int getThreadId() const { return threadId; }
    randomx_vm* getVM() const { return vm; }
    bool getIsRunning() const { return isRunning; }
    uint64_t getNonce() const { return currentNonce; }
    uint64_t getHashCount() const { return hashCount; }
    uint64_t getTotalHashCount() const { return totalHashCount; }
    uint64_t getAcceptedShares() const { return acceptedShares.load(); }
    uint64_t getRejectedShares() const { return rejectedShares.load(); }

    // Mutators
    void setVM(randomx_vm* newVM) { vm = newVM; }
    void setIsRunning(bool running) { isRunning = running; }
    void setNonce(uint64_t n) { currentNonce = n; }
    void incrementHashCount() { hashCount++; totalHashCount++; }
    void incrementAcceptedShares() { acceptedShares++; }
    void incrementRejectedShares() { rejectedShares++; }

    // Stats
    double getHashrate() const;

    // Public for access by mining thread
    std::atomic<bool> shouldStop{false};

private:
    int threadId;
    randomx_vm* vm;
    std::atomic<bool> isRunning;
    std::atomic<uint64_t> hashCount;
    uint64_t totalHashCount;
    uint64_t currentNonce;
    Job* currentJob;
    std::mutex jobMutex;
    mutable std::mutex vmMutex;
    std::condition_variable jobCondition;
    std::queue<Job> jobQueue;
    std::string currentSeedHash;
    std::chrono::steady_clock::time_point startTime;
    std::chrono::steady_clock::time_point lastUpdate;
    std::atomic<uint64_t> acceptedShares{0};
    std::atomic<uint64_t> rejectedShares{0};
    std::atomic<uint64_t> hashes{0};
    bool vmInitialized{false};
    std::thread thread;
}; 