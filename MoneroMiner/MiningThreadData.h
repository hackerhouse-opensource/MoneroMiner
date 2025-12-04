#pragma once

#include <atomic>
#include <cstdint>
#include <vector>
#include "randomx.h"

class MiningThreadData {
public:
    MiningThreadData(int id);
    ~MiningThreadData();

    bool initializeVM();
    bool calculateHash(const std::vector<uint8_t>& input, uint64_t nonce);
    
    // CRITICAL: New method for proper hash calculation with target checking
    bool calculateHashAndCheckTarget(
        const std::vector<uint8_t>& blob,
        const std::vector<uint8_t>& target,
        std::vector<uint8_t>& hashOut
    );
    
    int getThreadId() const { return threadId; }
    void setHashrate(double rate) { hashrate.store(rate); }
    double getHashrate() const { return hashrate.load(); }
    void incrementHashCount() { hashCount.fetch_add(1); }
    uint64_t getTotalHashCount() const { return hashCount.load(); }
    void incrementAccepted() { acceptedShares.fetch_add(1); }
    void incrementRejected() { rejectedShares.fetch_add(1); }
    uint64_t getAcceptedShares() const { return acceptedShares.load(); }
    uint64_t getRejectedShares() const { return rejectedShares.load(); }

private:
    int threadId;
    std::atomic<double> hashrate{0.0};
    std::atomic<uint64_t> hashCount{0};
    std::atomic<uint64_t> acceptedShares{0};
    std::atomic<uint64_t> rejectedShares{0};
};