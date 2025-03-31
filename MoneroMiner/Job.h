#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include <sstream>
#include <iomanip>
#include <cmath>

// Mining job structure
class Job {
public:
    std::string jobId;
    std::string blob;
    std::string target;
    uint32_t height;
    std::string seedHash;
    double difficulty;
    uint32_t nonce;

    // Default constructor
    Job() : height(0), difficulty(0.0), nonce(0) {}

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
    const std::string& getBlob() const { return blob; }
    const std::string& getTarget() const { return target; }
    uint32_t getHeight() const { return height; }
    const std::string& getSeedHash() const { return seedHash; }
    double getDifficulty() const { return difficulty; }
    uint32_t getNonce() const { return nonce; }

    // Setters
    void setId(const std::string& id) { jobId = id; }
    void setBlob(const std::string& b) { blob = b; }
    void setTarget(const std::string& t) { target = t; }
    void setHeight(uint32_t h) { height = h; }
    void setSeedHash(const std::string& seed) { seedHash = seed; }
    void setDifficulty(double d) { difficulty = d; }
    void setNonce(uint32_t n) { nonce = n; }
    void incrementNonce() { nonce++; }

    // Check if job is empty
    bool empty() const {
        return jobId.empty() || blob.empty() || target.empty() || height == 0 || seedHash.empty();
    }

    Job(const std::string& id, const std::string& b, const std::string& t, uint32_t h, const std::string& sh)
        : jobId(id), blob(b), target(t), height(h), seedHash(sh), difficulty(0.0), nonce(0) {
        calculateDifficulty();
    }

    double calculateDifficulty() {
        // Convert target from compact to expanded format
        uint64_t compactTarget = std::stoull(target, nullptr, 16);
        uint32_t exponent = (compactTarget >> 24) & 0xFF;
        uint32_t mantissa = compactTarget & 0xFFFFFF;
        
        // Calculate expanded target (256-bit)
        uint64_t expandedTarget[4] = {0, 0, 0, 0};
        if (exponent <= 3) {
            expandedTarget[3] = mantissa >> (8 * (3 - exponent));
        } else {
            uint32_t shift = 8 * (exponent - 3);
            if (shift < 64) {
                expandedTarget[3] = static_cast<uint64_t>(mantissa) << shift;
            } else if (shift < 128) {
                expandedTarget[2] = static_cast<uint64_t>(mantissa) << (shift - 64);
            } else if (shift < 192) {
                expandedTarget[1] = static_cast<uint64_t>(mantissa) << (shift - 128);
            } else if (shift < 256) {
                expandedTarget[0] = static_cast<uint64_t>(mantissa) << (shift - 192);
            }
        }
        
        // Calculate difficulty using 2^256 / target
        double expandedTargetValue = 0.0;
        double scale = 1.0;
        for (int i = 3; i >= 0; i--) {
            expandedTargetValue += static_cast<double>(expandedTarget[i]) * scale;
            scale *= 18446744073709551616.0; // 2^64
        }
        
        difficulty = pow(2.0, 256) / expandedTargetValue;
        return difficulty;
    }

    // Convert hex blob to bytes and handle nonce position correctly
    std::vector<uint8_t> getBlobBytes() const {
        std::vector<uint8_t> bytes;
        for (size_t i = 0; i < blob.length(); i += 2) {
            std::string byteString = blob.substr(i, 2);
            uint8_t byte = static_cast<uint8_t>(std::stoul(byteString, nullptr, 16));
            bytes.push_back(byte);
        }
        
        // Ensure the blob is at least 43 bytes (RandomX block header size)
        if (bytes.size() < 43) {
            bytes.resize(43, 0);
        }
        
        // Insert nonce at the correct position (bytes 39-42)
        uint32_t nonceValue = nonce;
        bytes[39] = (nonceValue >> 24) & 0xFF;
        bytes[40] = (nonceValue >> 16) & 0xFF;
        bytes[41] = (nonceValue >> 8) & 0xFF;
        bytes[42] = nonceValue & 0xFF;
        
        return bytes;
    }
};

// Job-related functions
bool isHashValid(const std::vector<uint8_t>& hash, const std::string& targetHex);
std::vector<uint8_t> compactTo256BitTarget(const std::string& targetHex);
uint64_t getTargetDifficulty(const std::string& targetHex);
bool checkHashDifficulty(const std::vector<uint8_t>& hash, uint64_t difficulty);
void incrementNonce(std::vector<uint8_t>& blob, uint32_t nonce); 