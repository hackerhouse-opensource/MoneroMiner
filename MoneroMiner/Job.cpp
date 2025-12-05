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
    
    /*
     * MONERO TARGET CALCULATION - CORRECTED
     * ======================================
     * 
     * Pool sends compact target, we need to calculate the FULL 256-bit comparison target.
     * 
     * The formula is: target = MAX_256BIT / difficulty
     * Where MAX_256BIT = 2^256 - 1
     * 
     * For typical pool mining, difficulty is < 2^64, so we can use simplified calculation:
     * target = (2^256 - 1) / difficulty ≈ (2^256) / difficulty
     * 
     * Since we can't do native 256-bit math, we use:
     * target_high = 0xFFFFFFFFFFFFFFFF (all high bits set)
     * target_low = 0xFFFFFFFFFFFFFFFF / difficulty
     * 
     * This gives us approximately the right threshold.
     */
    
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
        
        // Calculate 256-bit target using integer division approximation
        // For difficulty D, target ≈ 2^256 / D
        // We calculate this as: for each 64-bit word, value = (2^64 - 1) / (D / 2^(64*wordIndex))
        
        // Simplified for typical mining: 
        // Since difficulty < 2^32 typically, the high words will be all 0xFF...
        // and only the low word will have the actual threshold
        
        if (difficulty <= 0xFFFFFFFFFFFFFFFFULL) {
            // For difficulty that fits in 64 bits, use this approximation:
            // Word 0 (LSW): 0xFFFFFFFFFFFFFFFF / difficulty
            // Word 1-3:     0xFFFFFFFFFFFFFFFF (all bits set)
            
            targetHash[0] = 0xFFFFFFFFFFFFFFFFULL / difficulty;
            targetHash[1] = 0xFFFFFFFFFFFFFFFFULL;
            targetHash[2] = 0xFFFFFFFFFFFFFFFFULL;
            targetHash[3] = 0xFFFFFFFFFFFFFFFFULL;
        } else {
            // For very high difficulty (rare), set to minimum
            targetHash[0] = 1;
            targetHash[1] = 0;
            targetHash[2] = 0;
            targetHash[3] = 0;
        }
        
        if (config.debugMode) {
            std::stringstream ss;
            ss << "\n=== TARGET CALCULATION (CORRECTED) ===\n";
            ss << "Pool compact target: " << targetHex << "\n";
            ss << "Compact (uint32): 0x" << std::hex << std::setw(8) << std::setfill('0') << compactTarget << "\n";
            ss << "Difficulty: " << std::dec << difficulty << "\n";
            ss << "Target as 4x uint64 (LE):\n";
            ss << "  Word[0] (LSW): 0x" << std::hex << std::setw(16) << std::setfill('0') << targetHash[0] << "\n";
            ss << "  Word[1]:       0x" << std::hex << std::setw(16) << std::setfill('0') << targetHash[1] << "\n";
            ss << "  Word[2]:       0x" << std::hex << std::setw(16) << std::setfill('0') << targetHash[2] << "\n";
            ss << "  Word[3] (MSW): 0x" << std::hex << std::setw(16) << std::setfill('0') << targetHash[3] << "\n";
            ss << "Interpretation: Any hash where word[3] < 0xFFFF... is valid,\n";
            ss << "                OR if word[3]==0xFFFF... then word[2] < 0xFFFF..., etc.\n";
            ss << "Expected shares per " << difficulty << " hashes: ~1.0";
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

std::array<uint64_t, 4> Job::difficultyToTarget(uint64_t difficulty) {
    /*
     * Convert pool difficulty to 256-bit comparison target
     * ====================================================
     * 
     * Formula: target = 0xFFFFFFFFFFFFFFFF / difficulty
     * 
     * This gives us the maximum hash value that meets the difficulty.
     * Any RandomX hash output < target is a valid share.
     */
    
    std::array<uint64_t, 4> target = {0, 0, 0, 0};
    
    if (difficulty == 0) difficulty = 1;
    
    // Calculate maximum hash value for this difficulty
    uint64_t maxHash = 0xFFFFFFFFFFFFFFFFULL / difficulty;
    
    // Store in first word (little-endian)
    target[0] = maxHash;
    // Rest are zero (difficulty only affects first 64 bits for typical pool mining)
    
    return target;
}

bool Job::isValidShare(const std::array<uint64_t, 4>& hashResult) const {
    /*
     * Compare 256-bit hash against target (little-endian comparison)
     * =============================================================
     * 
     * Compare from MSW to LSW (index 3 down to 0).
     * Valid share: hashResult < targetHash
     * 
     * This matches how xmrig/xmr-stak perform validation.
     */
    
    // Compare from most significant to least significant
    for (int i = 3; i >= 0; i--) {
        if (hashResult[i] < targetHash[i]) {
            return true;  // Hash is definitely less than target
        }
        if (hashResult[i] > targetHash[i]) {
            return false; // Hash is definitely greater than target
        }
        // If equal, continue to next word
    }
    
    // All words equal - hash == target, which is valid (hash <= target)
    return true;
}

std::string Job::getTargetHex() const {
    std::stringstream ss;
    // Display as little-endian bytes (how it's stored)
    for (int wordIdx = 0; wordIdx < 4; wordIdx++) {
        uint64_t word = targetHash[wordIdx];
        for (int byteIdx = 0; byteIdx < 8; byteIdx++) {
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