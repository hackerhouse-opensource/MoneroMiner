#include "Job.h"
#include "Utils.h"
#include <sstream>
#include <iomanip>
#include <stdexcept>

std::vector<uint8_t> Job::getBlobBytes() const {
    try {
        if (blob.empty()) {
            return std::vector<uint8_t>();
        }
        
        // Validate blob is valid hex
        if (blob.length() % 2 != 0) {
            throw std::runtime_error("Invalid blob: odd length");
        }
        
        // Convert hex to bytes with validation
        std::vector<uint8_t> bytes = Utils::hexToBytes(blob);
        
        // Validate size is reasonable
        if (bytes.empty() || bytes.size() > 256) {
            throw std::runtime_error("Invalid blob size: " + std::to_string(bytes.size()));
        }
        
        return bytes;
    }
    catch (const std::exception& e) {
        Utils::threadSafePrint("Error converting blob to bytes: " + std::string(e.what()), true);
        return std::vector<uint8_t>();
    }
}

bool Job::isValid() const {
    return !blob.empty() && !jobId.empty() && !target.empty() && !seedHash.empty();
}

double Job::calculateDifficulty() const {
    uint32_t targetNum = std::stoul(target, nullptr, 16);
    uint32_t exponent = (targetNum >> 24) & 0xFF;
    uint32_t mantissa = targetNum & 0xFFFFFF;
    return static_cast<double>(0xFFFFFFFFULL) / 
        (static_cast<double>(mantissa) * std::pow(2.0, static_cast<double>(exponent - 24)));
}

// Standalone job-related functions
bool isHashValid(const std::vector<uint8_t>& hash, const std::string& targetHex) {
    std::vector<uint8_t> target = compactTo256BitTarget(targetHex);
    for (size_t i = 0; i < hash.size(); ++i) {
        if (hash[i] < target[i]) return true;
        if (hash[i] > target[i]) return false;
    }
    return true;
}

std::vector<uint8_t> compactTo256BitTarget(const std::string& targetHex) {
    std::vector<uint8_t> result(32, 0);
    uint32_t compact = std::stoul(targetHex, nullptr, 16);
    uint32_t mantissa = compact & 0xFFFFFF;
    uint32_t exponent = (compact >> 24) & 0xFF;
    
    // Convert compact target to 256-bit target
    uint32_t shift = 8 * (exponent - 3);
    uint64_t value = mantissa;
    value <<= shift;
    
    // Fill the result vector
    for (int i = 0; i < 8; ++i) {
        result[31 - i] = static_cast<uint8_t>(value & 0xFF);
        value >>= 8;
    }
    
    return result;
}

uint64_t getTargetDifficulty(const std::string& targetHex) {
    uint32_t compact = std::stoul(targetHex, nullptr, 16);
    uint32_t mantissa = compact & 0xFFFFFF;
    uint32_t exponent = (compact >> 24) & 0xFF;
    return static_cast<uint64_t>(0xFFFFFFFFULL) / 
        (static_cast<uint64_t>(mantissa) * (1ULL << (exponent - 24)));
}

bool checkHashDifficulty(const std::vector<uint8_t>& hash, uint64_t difficulty) {
    if (hash.size() != 32) return false;
    
    uint64_t hashValue = 0;
    for (int i = 0; i < 8; ++i) {
        hashValue = (hashValue << 8) | hash[i];
    }
    
    return hashValue < (0xFFFFFFFFFFFFFFFFULL / difficulty);
}

void incrementNonce(std::vector<uint8_t>& blob, uint32_t nonce) {
    if (blob.size() < 43) return;
    blob[39] = (nonce >> 24) & 0xFF;
    blob[40] = (nonce >> 16) & 0xFF;
    blob[41] = (nonce >> 8) & 0xFF;
    blob[42] = nonce & 0xFF;
}