#include "Job.h"
#include "Utils.h"
#include "Config.h"
#include "Types.h"
#include <sstream>
#include <iomanip>
#include <cstring>
#include <stdexcept>

extern Config config;

// Default constructor
Job::Job() : jobId(""), height(0), seedHash(""), difficulty(0), nonceOffset(39), blob() {
    targetHash = {0, 0, 0, 0};
}

Job::Job(const Job& other) 
    : jobId(other.jobId)
    , height(other.height)
    , seedHash(other.seedHash)
    , difficulty(other.difficulty)
    , nonceOffset(other.nonceOffset)
    , targetHash(other.targetHash)
    , blob(other.blob)
{
}

// Copy assignment
Job& Job::operator=(const Job& other) {
    if (this != &other) {
        jobId = other.jobId;
        height = other.height;
        seedHash = other.seedHash;
        difficulty = other.difficulty;
        nonceOffset = other.nonceOffset;
        targetHash = other.targetHash;
        blob = other.blob;
        
        // Remove debug spam - assignment operator is called frequently
    }
    return *this;
}

Job::Job(const std::string& blobHex, const std::string& id, const std::string& targetHex,
         uint64_t h, const std::string& seed)
    : jobId(id), height(h), seedHash(seed), difficulty(0), nonceOffset(0)
{
    blob = Utils::hexToBytes(blobHex);
    nonceOffset = findNonceOffset();
    
    std::vector<uint8_t> targetData = Utils::hexToBytes(targetHex);
    
    if (targetData.size() == 4) {
        // Parse 4-byte compact target as little-endian uint32
        uint32_t compactTarget = 0;
        for (size_t i = 0; i < 4; i++) {
            compactTarget |= static_cast<uint32_t>(targetData[i]) << (i * 8);
        }
        
        if (compactTarget == 0) compactTarget = 1;
        
        // Calculate pool difficulty
        difficulty = static_cast<uint64_t>(0xFFFFFFFFULL) / static_cast<uint64_t>(compactTarget);
        
        // Calculate 256-bit target using TRUE division: (2^256 - 1) / difficulty
        uint256_t maxValue = uint256_t::maximum();
        uint256_t target256 = maxValue / difficulty;
        
        // Store result
        targetHash[0] = target256.data[0];
        targetHash[1] = target256.data[1];
        targetHash[2] = target256.data[2];
        targetHash[3] = target256.data[3];
        
        if (config.debugMode) {
            std::stringstream ss;
            ss << "\n=== TARGET CALCULATION ===\n";
            ss << "Compact: 0x" << std::hex << std::setw(8) << std::setfill('0') << compactTarget << "\n";
            ss << "Difficulty: " << std::dec << difficulty << "\n";
            ss << "Target (256-bit): " << target256.toHex();
            Utils::threadSafePrint(ss.str(), true);
        }
        
    } else if (targetData.size() == 32) {
        // Pool sent full 256-bit target (rare, but handle it)
        // Parse as little-endian 256-bit value
        for (int i = 0; i < 4; i++) {
            uint64_t word = 0;
            for (int j = 0; j < 8; j++) {
                word |= static_cast<uint64_t>(targetData[i * 8 + j]) << (j * 8);
            }
            targetHash[i] = word;
        }
        
        // Calculate difficulty from target
        if (targetHash[0] > 0) {
            difficulty = 0xFFFFFFFFFFFFFFFFULL / targetHash[0];
        } else {
            difficulty = 1;
        }
        
    } else {
        // Invalid target size - set to maximum difficulty (hardest target)
        difficulty = 1;
        targetHash = {0xFFFFFFFFFFFFFFFFULL, 0, 0, 0};
    }
}

std::string Job::getTargetHex() const {
    std::stringstream ss;
    // Display only: targetHash is little-endian (word[0]/byte[0] = LSB), the order actually
    // used for the PoW comparison. This prints it MSB-first (word[3] down to word[0], each
    // word high-byte-first) purely for human-readable logging - callers must build target
    // bytes from targetHash directly (see the little-endian conversions in picominer.cpp)
    // for anything that needs the real byte order, never parse this string back.
    for (int wordIdx = 3; wordIdx >= 0; wordIdx--) {
        uint64_t word = targetHash[wordIdx];
        // Display each word's bytes in big-endian order
        for (int byteIdx = 7; byteIdx >= 0; byteIdx--) {
            uint8_t byte = (word >> (byteIdx * 8)) & 0xFF;
            ss << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(byte);
        }
    }
    return ss.str();
}

size_t Job::findNonceOffset() const {
    // Monero pool mining: nonce is ALWAYS at byte 39 (0-indexed)
    return 39;
}

std::vector<uint8_t> Job::getBlobBytes() const {
    return blob;
}

std::string Job::getJobId() const {
    return jobId;
}

std::string Job::getTarget() const {
    return getTargetHex();
}