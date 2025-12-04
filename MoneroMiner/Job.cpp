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
    
    if (config.debugMode) {
        Utils::threadSafePrint("[JOB COPY] Copied difficulty: " + std::to_string(difficulty) + 
            " from source: " + std::to_string(other.difficulty), true);
    }
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
    if (blobHex.length() % 2 != 0) {
        throw std::runtime_error("Invalid blob hex string");
    }
    
    blob.resize(blobHex.length() / 2);
    for (size_t i = 0; i < blob.size(); ++i) {
        std::string byteStr = blobHex.substr(i * 2, 2);
        blob[i] = static_cast<uint8_t>(std::strtoul(byteStr.c_str(), nullptr, 16));
    }
    
    // Calculate nonce offset
    if (blob.size() >= 43) {
        size_t offset = 0;
        uint8_t varintSize = blob[0] >= 0x02 ? 2 : 1;
        offset += varintSize;
        offset += 1;
        while (offset < blob.size() && (blob[offset] & 0x80)) {
            offset++;
        }
        offset++;
        nonceOffset = offset;
        
        if (nonceOffset < 34 || nonceOffset > 50 || nonceOffset + 4 > blob.size()) {
            nonceOffset = 39;
        }
    } else {
        nonceOffset = 39;
    }
    
    // Target calculation
    memset(targetBytes, 0, 32);
    
    if (targetHex.length() == 8) {
        uint32_t compact = static_cast<uint32_t>(std::stoul(targetHex, nullptr, 16));
        difficulty = static_cast<uint64_t>(compact / 8500.0);
        
        // FORCE minimum difficulty for testing
        if (difficulty == 0) {
            difficulty = 480045;  // Fallback
        }
        
        uint64_t target64 = difficulty > 0 ? (0xFFFFFFFFFFFFFFFFULL / difficulty) : 0xFFFFFFFFFFFFFFFFULL;
        
        for (int i = 0; i < 8; i++) {
            targetBytes[i] = static_cast<uint8_t>((target64 >> (i * 8)) & 0xFF);
        }
        
        // Only print in debug mode
        if (config.debugMode) {
            Utils::threadSafePrint("[JOB CTOR] Difficulty set to: " + std::to_string(difficulty) + 
                " | Target: 0x" + Utils::formatHex(target64, 16), true);
        }
    } else {
        Utils::threadSafePrint("[JOB CTOR ERROR] Invalid target hex length: " + std::to_string(targetHex.length()), true);
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