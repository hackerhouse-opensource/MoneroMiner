#pragma once

#include <atomic>
#include <cstdint>
#include <vector>

class MiningThreadData {
public:
    MiningThreadData(int id);
    ~MiningThreadData();

    int getThreadId() const { return threadId; }
    bool initializeVM();
    bool calculateHash(const std::vector<uint8_t>& input, uint64_t nonce);
    
    uint64_t getTotalHashCount() const { return totalHashes.load(); }
    uint64_t getAcceptedShares() const { return acceptedShares.load(); }
    uint64_t getRejectedShares() const { return rejectedShares.load(); }
    double getHashrate() const { return hashrate.load(); }
    
    void incrementHashCount() { totalHashes++; }
    void incrementAccepted() { acceptedShares++; }
    void incrementRejected() { rejectedShares++; }
    void setHashrate(double hr) { hashrate = hr; }

private:
    int threadId;
    std::atomic<uint64_t> totalHashes{0};
    std::atomic<uint64_t> acceptedShares{0};
    std::atomic<uint64_t> rejectedShares{0};
    std::atomic<double> hashrate{0.0};
};