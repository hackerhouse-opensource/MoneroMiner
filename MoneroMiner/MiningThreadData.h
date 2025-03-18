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
#include "HashBuffers.h"
#include "Job.h"
#include <thread>

// Forward declarations
struct randomx_vm;
class RandomXManager;

class MiningThreadData {
public:
    MiningThreadData(int threadId);
    ~MiningThreadData();

    bool initializeVM();
    void cleanup();
    void mine();
    void submitShare(const uint8_t* hash);
    int getThreadId() const { return threadId; }
    uint64_t getTotalHashCount() const { return totalHashCount; }
    void updateJob(const Job& newJob);
    bool needsVMReinit(const std::string& newSeedHash) const;
    bool calculateHash(const std::vector<uint8_t>& blob, uint8_t* outputHash, uint32_t currentDebugCounter = 0);

    static const unsigned int BATCH_SIZE = 256;
    std::atomic<bool> isRunning;
    std::chrono::steady_clock::time_point startTime;
    std::atomic<uint64_t> hashes;
    std::atomic<uint64_t> shares;
    std::atomic<uint64_t> acceptedShares;
    std::atomic<uint64_t> rejectedShares;
    std::chrono::steady_clock::time_point lastUpdate;

    uint64_t getHashCount() const { return hashCount; }
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

    randomx_vm* getVM() const { return vm; }

    void start() {
        thread = std::thread(&MiningThreadData::mine, this);
    }
    
    void stop() {
        shouldStop = true;
        if (thread.joinable()) {
            thread.join();
        }
    }
    
    void setJob(const Job& job) {
        std::lock_guard<std::mutex> lock(jobMutex);
        currentJob = std::make_shared<Job>(job);
    }

private:
    int threadId;
    randomx_vm* vm = nullptr;
    bool vmInitialized = false;
    std::mutex vmMutex;
    std::mutex jobMutex;
    std::unique_ptr<HashBuffers> hashBuffers;
    uint64_t hashCount = 0;
    uint64_t totalHashCount = 0;
    std::shared_ptr<Job> currentJob;
    uint32_t currentNonce = 0;
    uint32_t nonceStart = 0;
    uint32_t nonceEnd = 0;
    std::string currentJobId;
    std::atomic<bool> shouldStop;
    std::thread thread;
    int elapsedSeconds = 0;
    uint32_t currentDebugCounter = 0;
    std::string currentSeedHash;
}; 