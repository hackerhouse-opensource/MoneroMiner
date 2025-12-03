#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include <atomic>
#include <sstream>
#include <iomanip>
#include <cmath>

// Mining job structure
class Job {
public:
    std::string blob;
    std::string jobId;
    std::string target;
    uint64_t height;
    std::string seedHash;
    double difficulty;

private:
    std::atomic<uint64_t> nonce;

public:
    // Default constructor
    Job() : height(0), difficulty(0.0), nonce(0) {}

    // Parameterized constructor
    Job(const std::string& blob, const std::string& jobId, const std::string& target,
        uint64_t height, const std::string& seedHash)
        : blob(blob), jobId(jobId), target(target), height(height),
          seedHash(seedHash), difficulty(0.0), nonce(0) {}

    // Copy constructor - safe for atomics
    Job(const Job& other)
        : blob(other.blob), jobId(other.jobId), target(other.target),
          height(other.height), seedHash(other.seedHash), difficulty(other.difficulty),
          nonce(other.nonce.load()) {}

    // Copy assignment - safe for atomics
    Job& operator=(const Job& other) {
        if (this != &other) {
            blob = other.blob;
            jobId = other.jobId;
            target = other.target;
            height = other.height;
            seedHash = other.seedHash;
            difficulty = other.difficulty;
            nonce.store(other.nonce.load());
        }
        return *this;
    }

    // Getters
    const std::string& getJobId() const { return jobId; }
    const std::string& getBlob() const { return blob; }
    const std::string& getTarget() const { return target; }
    uint64_t getHeight() const { return height; }
    const std::string& getSeedHash() const { return seedHash; }
    double getDifficulty() const { return difficulty; }
    uint64_t getNonce() const { return nonce.load(); }

    // Safe nonce operations
    void incrementNonce() { nonce.fetch_add(1); }
    void setNonce(uint64_t value) { nonce.store(value); }

    // Utility functions
    std::vector<uint8_t> getBlobBytes() const;
    double calculateDifficulty() const;
    bool isValid() const;
};

// Job-related functions
bool isHashValid(const std::vector<uint8_t>& hash, const std::string& targetHex);
std::vector<uint8_t> compactTo256BitTarget(const std::string& targetHex);
uint64_t getTargetDifficulty(const std::string& targetHex);
bool checkHashDifficulty(const std::vector<uint8_t>& hash, uint64_t difficulty);
void incrementNonce(std::vector<uint8_t>& blob, uint32_t nonce);