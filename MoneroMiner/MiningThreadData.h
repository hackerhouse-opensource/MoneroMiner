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
#include "RandomXManager.h"
#include <thread>
#include "randomx.h"

// Forward declarations
struct randomx_vm;

class MiningThreadData {
public:
    MiningThreadData(int threadId);
    ~MiningThreadData();

    // Getters
    int getThreadId() const { return threadId; }
    uint64_t getHashCount() const { return hashCount; }
    uint64_t getTotalHashCount() const { return totalHashCount; }
    uint64_t getAcceptedShares() const { return acceptedShares; }
    uint64_t getRejectedShares() const { return rejectedShares; }
    uint64_t getShares() const { return shares; }
    std::string getCurrentJobId() const { return currentJobId; }
    uint64_t getCurrentNonce() const { return currentNonce; }
    std::chrono::steady_clock::time_point getStartTime() const { return startTime; }
    double getHashrate() const;
    int getElapsedSeconds() const { return elapsedSeconds; }

    // Setters
    void setHashCount(uint64_t count) { hashCount = count; }
    void setTotalHashCount(uint64_t count) { totalHashCount = count; }
    void incrementHashCount() { hashCount++; }
    void incrementAcceptedShares() { acceptedShares++; }
    void incrementRejectedShares() { rejectedShares++; }
    void setCurrentJobId(const std::string& jobId) { currentJobId = jobId; }
    void setCurrentNonce(uint64_t nonce) { currentNonce = nonce; }
    void setElapsedSeconds(int seconds) { elapsedSeconds = seconds; }

    // VM operations
    bool initializeVM();
    bool calculateHash(const std::vector<uint8_t>& blob, uint64_t nonce, uint8_t* output);
    void updateJob(const Job& job);
    bool needsVMReinit(const std::string& newSeedHash) const;

    // Thread control
    void start();
    void stop();
    void mine();
    void submitShare(const uint8_t* hash);

private:
    int threadId;
    randomx_vm* vm;
    bool vmInitialized;
    uint64_t hashCount;
    uint64_t totalHashCount;
    std::unique_ptr<HashBuffers> hashBuffers;
    std::mutex vmMutex;
    std::mutex jobMutex;
    std::shared_ptr<Job> currentJob;
    uint64_t currentNonce;
    std::string currentJobId;
    std::atomic<bool> shouldStop;
    std::thread thread;
    std::chrono::steady_clock::time_point startTime;
    uint32_t currentDebugCounter;
    std::string currentSeedHash;
    std::atomic<bool> running;
    std::chrono::steady_clock::time_point lastUpdate;
    std::atomic<uint64_t> hashes;
    std::atomic<uint64_t> shares;
    std::atomic<uint64_t> acceptedShares;
    std::atomic<uint64_t> rejectedShares;
    int elapsedSeconds;
}; 