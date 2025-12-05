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
    memset(targetBytes, 0x00, 32); // Initialize to ZERO (most restrictive)
    
    if (targetHex.length() == 8) {
        // XMRIG CORRECT IMPLEMENTATION (from src/base/net/stratum/Job.cpp):
        // The hex string "f3220000" represents 4 bytes: [f3, 22, 00, 00]
        // These are stored and interpreted as LITTLE-ENDIAN uint32
        
        std::vector<uint8_t> targetVec = Utils::hexToBytes(targetHex);
        
        if (targetVec.size() == 4) {
            // Parse as LITTLE-ENDIAN uint32
            uint32_t target32_le = (static_cast<uint32_t>(targetVec[0]) << 0) |
                                   (static_cast<uint32_t>(targetVec[1]) << 8) |
                                   (static_cast<uint32_t>(targetVec[2]) << 16) |
                                   (static_cast<uint32_t>(targetVec[3]) << 24);
            // target32_le = 0x000022f3 = 8,947
            
            // Calculate difficulty: diff = 0xFFFFFFFF / target32
            if (target32_le > 0) {
                difficulty = static_cast<uint64_t>(0xFFFFFFFFULL / target32_le);
            } else {
                difficulty = 0xFFFFFFFFULL;
            }
            // difficulty = 4294967295 / 8947 = 480,045 ✓
            
            // Calculate full 64-bit target for hash comparison
            // target64 = 0xFFFFFFFFFFFFFFFF / difficulty
            uint64_t target64 = 0xFFFFFFFFFFFFFFFFULL / difficulty;
            // target64 = 18446744073709551615 / 480045 = 0x000022F5EA96B666
            
            // Store as little-endian in bytes 0-7
            for (int i = 0; i < 8; i++) {
                targetBytes[i] = (target64 >> (i * 8)) & 0xFF;
            }
            // targetBytes[0-7] = [66, b6, 96, ea, f5, 22, 00, 00]
            // Bytes 8-31 stay as 0x00
        }
        
        if (config.debugMode) {
            std::stringstream ss;
            uint64_t target64 = 0;
            for (int i = 0; i < 8; i++) {
                target64 |= (static_cast<uint64_t>(targetBytes[i]) << (i * 8));
            }
            ss << "[TARGET] Raw hex: " << targetHex
               << " => Difficulty: " << std::dec << difficulty
               << " => Target64: 0x" << std::hex << std::setw(16) << std::setfill('0') << target64 << "\n";
            ss << "  Target bytes (LE, first 8): ";
            for (int i = 0; i < 8; i++) {
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