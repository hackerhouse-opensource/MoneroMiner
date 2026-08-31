#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include <array>

/*
 * Monero Mining Target Conversion
 * ================================
 * This describes what the Job(blobHex, id, targetHex, h, seed) constructor
 * below actually computes, and what MiningThreadData::calculateHashAndCheckTarget
 * actually compares against when it validates a share.
 *
 * Pool sends: 4-byte compact target, little-endian (e.g. bytes f3 22 00 00)
 *
 * Step 1: Read the 4 bytes as a little-endian uint32
 *   compactTarget = 0x000022f3
 *
 * Step 2: Calculate difficulty from the compact target
 *   difficulty = 0xFFFFFFFF / compactTarget
 *
 * Step 3: Convert difficulty to the real 256-bit comparison target
 *   target = (2^256 - 1) / difficulty   (true 256-bit division, via uint256_t)
 *
 * Step 4: Hash comparison (done per-hash on the mining threads, not here)
 *   Valid share: RandomX hash < target, compared as 256-bit little-endian
 *   integers. Only the RandomX hash itself is sent back to the pool as the
 *   share's "result" - the target never leaves this process.
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

    // Get target as hex string for display
    std::string getTargetHex() const;

private:
    std::vector<uint8_t> blob;
};