#pragma once

#include <string>
#include <vector>
#include <cstdint>

namespace HashValidation {
    // Format hash bytes to hex string
    std::string formatHash(const std::vector<uint8_t>& hash);

    // Get target difficulty from compact target
    uint64_t getTargetDifficulty(const std::string& targetHex);

    // Validate hash against target using 4-byte tail comparison
    bool validateHash(const std::string& hashHex, const std::string& targetHex);

    // Check hash against target (internal implementation)
    bool checkHash(const uint8_t* hash, const std::string& targetHex);

    bool meetsTarget(const std::vector<uint8_t>& hash, const std::vector<uint8_t>& target);
    std::vector<uint8_t> expandTarget(const std::string& compactTarget);
    bool checkHashDifficulty(const uint8_t* hash, uint64_t difficulty);
} 