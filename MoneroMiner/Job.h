#pragma once

#include <string>
#include <vector>
#include <cstdint>

// Mining job structure
class Job {
public:
    // Data members
    std::string jobId;
    uint64_t height;
    std::string seedHash;
    uint64_t difficulty;
    size_t nonceOffset;
    uint8_t targetBytes[32];

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

private:
    std::vector<uint8_t> blob;
};