#include "Job.h"
#include "Utils.h"
#include "Config.h"
#include "Types.h"
#include <sstream>
#include <iomanip>
#include <cstring>
#include <stdexcept>

// External config declaration
extern Config config;

// Default constructor
Job::Job() : jobId(""), height(0), seedHash(""), difficulty(0), nonceOffset(39), blob() {
    memset(targetBytes, 0, 32);
}

Job::Job(const Job& other) 
    : jobId(other.jobId)
    , height(other.height)
    , seedHash(other.seedHash)
    , difficulty(other.difficulty)
    , nonceOffset(other.nonceOffset)
    , blob(other.blob)
{
    memcpy(targetBytes, other.targetBytes, 32);
}

// Copy assignment
Job& Job::operator=(const Job& other) {
    if (this != &other) {
        jobId = other.jobId;
        height = other.height;
        seedHash = other.seedHash;
        difficulty = other.difficulty;
        nonceOffset = other.nonceOffset;
        blob = other.blob;
        memcpy(targetBytes, other.targetBytes, 32);
        
        // Remove debug spam - assignment operator is called frequently
    }
    return *this;
}

Job::Job(const std::string& blobHex, const std::string& id, const std::string& targetHex, 
         uint64_t h, const std::string& seed) 
    : jobId(id), height(h), seedHash(seed), difficulty(0), nonceOffset(0) {
    
    // Parse blob
    blob = Utils::hexToBytes(blobHex);
    if (blob.size() < 43) {
        Utils::threadSafePrint("ERROR: Blob too small", true);
        return;
    }
    
    nonceOffset = 39;
    memset(targetBytes, 0x00, 32); // CRITICAL FIX: Initialize to ZERO, not 0xFF!
    
    if (targetHex.length() == 8) {
        // Parse compact target from pool (e.g., "f3220000")
        uint32_t compact = static_cast<uint32_t>(std::stoul(targetHex, nullptr, 16));
        
        // MONERO POOL FORMULA: difficulty ≈ compact / 8500
        // This is the empirical formula all Monero pools use
        // For compact = 0xf3220000 (4,079,190,016):
        //   difficulty = 4,079,190,016 / 8500 ≈ 479,904
        difficulty = static_cast<uint64_t>(static_cast<double>(compact) / 8500.0);
        
        if (difficulty == 0) {
            difficulty = 1;
        }
        
        // Calculate 256-bit target from difficulty
        // target = 0xFFFFFFFFFFFFFFFF / difficulty
        uint64_t target64 = 0xFFFFFFFFFFFFFFFFULL / difficulty;
        
        // Store target in little-endian (bytes 0-7)
        for (int i = 0; i < 8; i++) {
            targetBytes[i] = (target64 >> (i * 8)) & 0xFF;
        }
        // Bytes 8-31 remain 0x00 (from memset) - NOT 0xFF!
        
        if (config.debugMode) {
            std::stringstream ss;
            ss << "[TARGET] Compact: 0x" << std::hex << std::setw(8) << std::setfill('0') << compact
               << " => Difficulty: " << std::dec << difficulty
               << " => Target64: 0x" << std::hex << std::setw(16) << std::setfill('0') << target64 << "\n";
            ss << "  Target bytes (LE, first 16): ";
            for (int i = 0; i < 16; i++) {
                ss << std::hex << std::setw(2) << std::setfill('0') << (int)targetBytes[i] << " ";
            }
            Utils::threadSafePrint(ss.str(), true);
        }
    }
    
    if (config.debugMode) {
        std::stringstream ss;
        ss << "[JOB] Parsed job\n";
        ss << "  Job ID: " << jobId << "\n";
        ss << "  Height: " << height << "\n";
        ss << "  Seed hash: " << seedHash << "\n";
        ss << "  Blob size: " << blob.size() << " bytes\n";
        ss << "  Nonce offset: " << nonceOffset << "\n";
        ss << "  Difficulty: " << difficulty << "\n";
        ss << "  Target (LE hex): ";
        for (int i = 0; i < 32; i++) {
            ss << std::hex << std::setw(2) << std::setfill('0') << (int)targetBytes[i];
        }
        Utils::threadSafePrint(ss.str(), true);
    }
}

size_t Job::findNonceOffset() const {
    // Monero block structure for v2 (major version >= 2):
    // Standard Monero blocks (version 0x10): Nonce is at byte 39
    
    if (blob.size() < 43) {
        return 39;  // Default fallback
    }
    
    uint8_t majorVersion = blob[0];
    
    // Parse the timestamp varint to find its length
    size_t offset = 1;  // After major version
    
    // Skip minor version if present
    if (majorVersion >= 2 && majorVersion < 16) {
        offset++;
    }
    
    // Parse varint (timestamp)
    size_t varintBytes = 0;
    while (offset + varintBytes < blob.size() && varintBytes < 10) {
        if ((blob[offset + varintBytes] & 0x80) == 0) {
            varintBytes++;
            break;
        }
        varintBytes++;
    }
    
    // After varint comes prev_hash (32 bytes), then nonce (4 bytes)
    size_t calculatedOffset = offset + varintBytes + 32;
    
    // Sanity check: offset should be between 35 and 45
    if (calculatedOffset < 35 || calculatedOffset > 45) {
        if (config.debugMode) {
            std::stringstream ss;
            ss << "[JOB] Calculated nonce offset " << calculatedOffset 
               << " seems wrong, using default 39";
            Utils::threadSafePrint(ss.str(), true);
        }
        return 39;
    }
    
    return calculatedOffset;
}

std::vector<uint8_t> Job::getBlobBytes() const {
    return blob;
}

std::string Job::getJobId() const {
    return jobId;
}