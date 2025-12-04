#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include "Difficulty.h"  // ADD THIS

// Mining job structure
class Job {
public:
    // Data members
    std::string blob;
    std::string jobId;
    std::string target;
    std::vector<uint8_t> targetBytes;  // Keep for compatibility
    uint256_t target256;  // ADD THIS - proper 256-bit target
    uint64_t height;
    std::string seedHash;
    uint64_t difficulty;
    size_t nonceOffset;  // ADD THIS LINE

    // Constructors
    Job() : height(0), difficulty(0), nonceOffset(39) {}  // MODIFY THIS LINE

    Job(const std::string& b, const std::string& j, const std::string& t,
        uint64_t h, const std::string& s);

    // Copy constructor and assignment
    Job(const Job& other);
    Job& operator=(const Job& other);

    // Methods
    bool isValid() const;
    void clear();
    const std::string& getJobId() const { return jobId; }
    std::vector<uint8_t> getBlobBytes() const;

private:
    void parseTargetBytes();
    void parseNonceOffset();  // ADD THIS LINE
};