#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include <atomic>

// Mining job structure
class Job {
public:
    // Data members
    std::string jobId;
    std::string blobHex;            // Original hex string from pool
    std::vector<uint8_t> blob;      // Binary blob data (converted from hex)
    std::string target;
    std::string seedHash;
    uint64_t height = 0;
    uint64_t difficulty = 0;

    // Constructors
    Job() = default;

    Job(const std::string& blobHex, const std::string& jobId,
        const std::string& target, uint64_t height, const std::string& seedHash);

    // Copy constructor and assignment
    Job(const Job& other);
    Job& operator=(const Job& other);

    // Methods
    bool isValid() const;
    void clear();
    const std::string& getJobId() const { return jobId; }
    const std::vector<uint8_t>& getBlobBytes() const { return blob; }
    const std::string& getBlobHex() const { return blobHex; }
};