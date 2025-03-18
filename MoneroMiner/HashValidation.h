#pragma once

#include <string>
#include <vector>
#include <cstdint>

namespace HashValidation {
    bool checkHash(const uint8_t* hash, const std::string& targetHex);
    bool meetsTarget(const std::vector<uint8_t>& hash, const std::vector<uint8_t>& target);
    std::vector<uint8_t> expandTarget(const std::string& compactTarget);
    std::string formatHash(const std::vector<uint8_t>& hash);
    bool validateHash(const std::string& hashHex, const std::string& targetHex);
    uint64_t getTargetDifficulty(const std::string& targetHex);
    bool checkHashDifficulty(const uint8_t* hash, uint64_t difficulty);
} 