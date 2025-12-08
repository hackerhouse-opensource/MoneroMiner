#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include <array>

/*
 * Monero Mining Target Conversion
 * ================================
 * 
 * Pool sends: 4-byte compact target (e.g., 0xf3220000 little-endian)
 * 
 * Step 1: Extract compact value
 *   Reverse bytes: 0x000022f3
 * 
 * Step 2: Calculate difficulty
 *   difficulty = 0xFFFFFFFFFFFFFFFF / (compact + 1)
 *   Example: 0xFFFFFFFFFFFFFFFF / 0x22f4 ≈ 480045
 * 
 * Step 3: Convert difficulty to 256-bit comparison target
 *   target = 0xFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF / difficulty
 *   This gives the actual threshold for hash comparison
 * 
 * Step 4: Hash comparison
 *   Valid share: hash_result <= target (as 256-bit little-endian integers)
 */

// Mining job structure
class Job {
public:
    // Data members
    std::string jobId;
    uint64_t height;
    std::string seedHash;
    uint64_t difficulty;
    size_t nonceOffset;
    
    // 256-bit target stored as 4x uint64_t (little-endian)
    std::array<uint64_t, 4> targetHash;

    // Default constructor (implemented in .cpp)
    Job();

    // Parameterized constructor (implemented in .cpp)
    Job(const std::string& blobHex, const std::string& id, const std::string& targetHex,
        uint64_t h, const std::string& seed);

    // Copy constructor (implemented in .cpp)
    Job(const Job& other);

    // Copy assignment operator (implemented in .cpp)
    Job& operator=(const Job& other);

    // Other methods
    size_t findNonceOffset() const;
    std::vector<uint8_t> getBlobBytes() const;
    std::string getJobId() const;
    std::string getTarget() const;
    
    // Convert difficulty to 256-bit comparison target
    static std::array<uint64_t, 4> difficultyToTarget(uint64_t difficulty);
    
    // Compare hash against target (returns true if hash <= target)
    bool isValidShare(const std::array<uint64_t, 4>& hashResult) const;
    
    // Get target as hex string for display
    std::string getTargetHex() const;

private:
    std::vector<uint8_t> blob;
};