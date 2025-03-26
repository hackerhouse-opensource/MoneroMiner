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
    std::string blobHex;
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
    const std::string& getBlobHex() const { return blobHex; }
    const std::vector<uint8_t>& getBlob() const { return blob; }
    const std::string& getTarget() const { return target; }
    uint32_t getHeight() const { return height; }
    const std::string& getSeedHash() const { return seedHash; }
    uint64_t getDifficulty() const { return difficulty; }
    uint64_t getNonce() const { return nonce; }

    // Setters
    void setId(const std::string& id) { jobId = id; }
    void setBlobHex(const std::string& blobHexData) { 
        blobHex = blobHexData;
        // Convert hex to bytes
        blob.clear();
        for (size_t i = 0; i < blobHexData.length(); i += 2) {
            std::string byteString = blobHexData.substr(i, 2);
            blob.push_back(static_cast<uint8_t>(std::stoi(byteString, nullptr, 16)));
        }
    }
    void setBlob(const std::vector<uint8_t>& blobData) { 
        blob = blobData;
        // Convert bytes to hex
        std::stringstream ss;
        for (uint8_t byte : blob) {
            ss << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(byte);
        }
        blobHex = ss.str();
    }
    void setTarget(const std::string& targetData) { target = targetData; }
    void setHeight(uint32_t jobHeight) { height = jobHeight; }
    void setSeedHash(const std::string& hash) { seedHash = hash; }
    void setDifficulty(uint64_t d) { difficulty = d; }
    void setNonce(uint64_t n) { nonce = n; }
    void incrementNonce() { nonce++; }

    // Check if job is empty
    bool empty() const {
        return blobHex.empty() || target.empty() || jobId.empty() || height == 0 || seedHash.empty();
    }

    Job(const std::string& id, const std::string& blob, const std::string& tgt, uint32_t h, const std::string& seed)
        : jobId(id), blobHex(blob), target(tgt), height(h), seedHash(seed), nonce(0) {
        // Convert hex blob to bytes
        for (size_t i = 0; i < blob.length(); i += 2) {
            std::string byteString = blob.substr(i, 2);
            this->blob.push_back(static_cast<uint8_t>(std::stoi(byteString, nullptr, 16)));
        }
    }
};

// Job-related functions
bool isHashValid(const std::vector<uint8_t>& hash, const std::string& targetHex);
std::vector<uint8_t> compactTo256BitTarget(const std::string& targetHex);
uint64_t getTargetDifficulty(const std::string& targetHex);
bool checkHashDifficulty(const std::vector<uint8_t>& hash, uint64_t difficulty);
void incrementNonce(std::vector<uint8_t>& blob, uint32_t nonce); 