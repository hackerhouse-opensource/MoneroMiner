#include "Job.h"
#include "Utils.h"
#include "Config.h"
#include "Difficulty.h"
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <atomic>

// External config declaration
extern Config config;

Job::Job(const std::string& b, const std::string& j, const std::string& t, 
         uint64_t h, const std::string& s)
    : blob(b), jobId(j), target(t), height(h), seedHash(s), difficulty(0), nonceOffset(39) {
    parseTargetBytes();
    parseNonceOffset();
}

// Make sure these are implemented (not just declared):
Job::Job(const Job& other)
    : blob(other.blob)
    , jobId(other.jobId)
    , target(other.target)
    , targetBytes(other.targetBytes)
    , target256(other.target256)  // ADD THIS
    , height(other.height)
    , seedHash(other.seedHash)
    , difficulty(other.difficulty)
    , nonceOffset(other.nonceOffset) {
}

Job& Job::operator=(const Job& other) {
    if (this != &other) {
        blob = other.blob;
        jobId = other.jobId;
        target = other.target;
        targetBytes = other.targetBytes;
        target256 = other.target256;  // ADD THIS
        height = other.height;
        seedHash = other.seedHash;
        difficulty = other.difficulty;
        nonceOffset = other.nonceOffset;
    }
    return *this;
}

void Job::parseTargetBytes() {
    targetBytes.clear();
    targetBytes.resize(32, 0x00);
    
    if (target.empty()) {
        std::fill(targetBytes.begin(), targetBytes.end(), 0xFF);
        // Set target256 to max
        for (int i = 0; i < 4; i++) target256.data[i] = 0xFFFFFFFFFFFFFFFFULL;
        return;
    }
    
    std::string hexStr = target;
    if (hexStr.length() >= 2 && (hexStr.substr(0, 2) == "0x" || hexStr.substr(0, 2) == "0X")) {
        hexStr = hexStr.substr(2);
    }
    
    // CRITICAL FIX: Parse pool difficulty and calculate proper 256-bit target
    if (hexStr.length() <= 8) {
        while (hexStr.length() < 8) {
            hexStr = "0" + hexStr;
        }
        
        uint32_t difficulty32 = static_cast<uint32_t>(std::stoul(hexStr, nullptr, 16));
        if (difficulty32 == 0) difficulty32 = 1;
        
        // CORRECT: Calculate target = (2^256 - 1) / difficulty
        target256 = uint256_t::fromDifficulty(static_cast<uint64_t>(difficulty32));
        
        // Store in targetBytes for legacy compatibility (little-endian)
        for (int i = 0; i < 4; i++) {
            uint64_t word = target256.data[i];
            for (int j = 0; j < 8; j++) {
                targetBytes[i * 8 + j] = static_cast<uint8_t>((word >> (j * 8)) & 0xFF);
            }
        }
        
        if (config.debugMode) {
            Utils::threadSafePrint("[TARGET] Difficulty: " + std::to_string(difficulty32) + 
                " → Target (BE hex): " + target256.toHex(), true);
        }
        
        return;
    }
    
    // Full 256-bit target (fallback)
    while (hexStr.length() < 64) {
        hexStr = "0" + hexStr;
    }
    
    for (size_t i = 0; i < 32 && i * 2 < hexStr.length(); i++) {
        std::string byteStr = hexStr.substr(i * 2, 2);
        targetBytes[i] = static_cast<uint8_t>(std::stoul(byteStr, nullptr, 16));
    }
    
    // Load into target256 (assuming big-endian input)
    target256.fromBigEndian(targetBytes.data());
}

void Job::parseNonceOffset() {
    // For Monero stratum protocol, the nonce is ALWAYS at offset 39
    // This is defined in the Cryptonote protocol
    nonceOffset = 39;
    
    // However, let's verify the blob is long enough
    std::vector<uint8_t> blobBytes = getBlobBytes();
    if (blobBytes.size() < 43) {
        if (config.debugMode) {
            Utils::threadSafePrint("[JOB] WARNING: Blob too short (" + 
                std::to_string(blobBytes.size()) + " bytes) for nonce at offset 39", true);
        }
    }
}

std::vector<uint8_t> Job::getBlobBytes() const {
    std::vector<uint8_t> bytes;
    if (blob.empty()) {
        return bytes;
    }
    
    std::string hexStr = blob;
    if (hexStr.length() >= 2 && (hexStr.substr(0, 2) == "0x" || hexStr.substr(0, 2) == "0X")) {
        hexStr = hexStr.substr(2);
    }
    
    if (config.debugMode) {
        static std::atomic<bool> firstBlob{true};
        if (firstBlob.exchange(false)) {
            Utils::threadSafePrint("[JOB BLOB] Hex string length: " + std::to_string(hexStr.length()) + " chars", true);
            Utils::threadSafePrint("[JOB BLOB] First 100 chars: " + hexStr.substr(0, std::min<size_t>(100, hexStr.length())), true);
        }
    }
    
    for (size_t i = 0; i < hexStr.length(); i += 2) {
        if (i + 1 < hexStr.length()) {
            std::string byteStr = hexStr.substr(i, 2);
            bytes.push_back(static_cast<uint8_t>(std::stoul(byteStr, nullptr, 16)));
        }
    }
    
    if (config.debugMode) {
        static std::atomic<bool> firstParsed{true};
        if (firstParsed.exchange(false)) {
            std::stringstream ss;
            ss << "[JOB BLOB] Parsed " << bytes.size() << " bytes. First 50:\n  ";
            for (size_t i = 0; i < std::min<size_t>(50, bytes.size()); i++) {
                ss << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(bytes[i]) << " ";
                if ((i + 1) % 20 == 0) ss << "\n  ";
            }
            Utils::threadSafePrint(ss.str(), true);
        }
    }
    
    return bytes;
}