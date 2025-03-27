#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include <sstream>
#include <iomanip>

// Mining job structure
class Job {
public:
    std::string jobId;
    std::vector<uint8_t> blob;
    std::string target;
    uint32_t height;
    std::string seedHash;
    uint64_t difficulty;
    uint64_t nonce;

    // Default constructor
    Job() : height(0), difficulty(0), nonce(0) {}

    // Copy constructor
    Job(const Job& other) = default;

    // Move constructor
    Job(Job&& other) = default;

    // Copy assignment operator
    Job& operator=(const Job& other) = default;

    // Move assignment operator
    Job& operator=(Job&& other) = default;

    // Getters
    const std::string& getJobId() const { return jobId; }
    const std::vector<uint8_t>& getBlob() const { return blob; }
    const std::string& getTarget() const { return target; }
    uint32_t getHeight() const { return height; }
    const std::string& getSeedHash() const { return seedHash; }
    uint64_t getDifficulty() const { return difficulty; }
    uint64_t getNonce() const { return nonce; }

    // Setters
    void setId(const std::string& id) { jobId = id; }
    void setBlob(const std::vector<uint8_t>& b) { blob = b; }
    void setTarget(const std::string& t) { target = t; }
    void setHeight(uint32_t h) { height = h; }
    void setSeedHash(const std::string& seed) { seedHash = seed; }
    void setDifficulty(uint64_t d) { difficulty = d; }
    void setNonce(uint64_t n) { nonce = n; }
    void incrementNonce() { nonce++; }

    // Check if job is empty
    bool empty() const {
        return blob.empty() || target.empty() || jobId.empty() || height == 0 || seedHash.empty();
    }

    Job(const std::string& id, const std::string& blobHex, const std::string& tgt, 
        uint32_t h, const std::string& seed) 
        : jobId(id), target(tgt), height(h), seedHash(seed), difficulty(0), nonce(0) {
        // Convert hex blob to bytes
        blob.resize(blobHex.length() / 2);
        for (size_t i = 0; i < blob.size(); i++) {
            blob[i] = static_cast<uint8_t>(std::stoi(blobHex.substr(i * 2, 2), nullptr, 16));
        }

        // Calculate difficulty from target
        uint64_t targetValue = std::stoull(tgt, nullptr, 16);
        uint32_t exponent = (targetValue >> 24) & 0xFF;
        uint32_t mantissa = targetValue & 0xFFFFFF;
        
        // Calculate expanded target
        uint64_t expandedTarget = 0;
        if (exponent <= 3) {
            expandedTarget = mantissa >> (8 * (3 - exponent));
        } else {
            expandedTarget = static_cast<uint64_t>(mantissa) << (8 * (exponent - 3));
        }
        
        // Calculate difficulty
        difficulty = 0xFFFFFFFFFFFFFFFFULL / expandedTarget;
    }
};

// Job-related functions
bool isHashValid(const std::vector<uint8_t>& hash, const std::string& targetHex);
std::vector<uint8_t> compactTo256BitTarget(const std::string& targetHex);
uint64_t getTargetDifficulty(const std::string& targetHex);
bool checkHashDifficulty(const std::vector<uint8_t>& hash, uint64_t difficulty);
void incrementNonce(std::vector<uint8_t>& blob, uint32_t nonce); 