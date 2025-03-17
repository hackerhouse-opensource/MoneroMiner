#pragma once

#include <string>
#include <vector>
#include <cstdint>

// Mining job structure
struct Job {
    std::vector<uint8_t> blob;
    std::string target;
    std::string jobId;
    uint64_t height;
    std::string seedHash;

    // Default constructor
    Job() : height(0) {}

    // Copy constructor
    Job(const Job& other) = default;

    // Move constructor
    Job(Job&& other) = default;

    // Copy assignment operator
    Job& operator=(const Job& other) = default;

    // Move assignment operator
    Job& operator=(Job&& other) = default;

    // Getters
    const std::string& getId() const { return jobId; }
    const std::vector<uint8_t>& getBlob() const { return blob; }
    const std::string& getTarget() const { return target; }
    uint64_t getHeight() const { return height; }
    const std::string& getSeedHash() const { return seedHash; }

    // Setters
    void setId(const std::string& id) { jobId = id; }
    void setBlob(const std::vector<uint8_t>& blobData) { blob = blobData; }
    void setTarget(const std::string& targetData) { target = targetData; }
    void setHeight(uint64_t jobHeight) { height = jobHeight; }
    void setSeedHash(const std::string& hash) { seedHash = hash; }

    // Check if job is empty
    bool empty() const {
        return blob.empty() || target.empty() || jobId.empty() || height == 0 || seedHash.empty();
    }
};

// Job-related functions
bool isHashValid(const std::vector<uint8_t>& hash, const std::string& targetHex);
std::vector<uint8_t> compactTo256BitTarget(const std::string& targetHex);
uint64_t getTargetDifficulty(const std::string& targetHex);
bool checkHashDifficulty(const std::vector<uint8_t>& hash, uint64_t difficulty);
void incrementNonce(std::vector<uint8_t>& blob, uint32_t nonce); 