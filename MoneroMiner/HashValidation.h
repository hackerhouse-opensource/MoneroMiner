#pragma once

#include <string>
#include <vector>
#include <cstdint>

namespace HashValidation {
    bool checkHash(const uint8_t* hash, const std::string& targetHex);
    bool isHashValid(const std::vector<uint8_t>& hash, const std::string& targetHex);
    bool isHashValid(const std::vector<uint8_t>& hash, const std::vector<uint8_t>& target);
    std::vector<uint8_t> compactTo256BitTarget(const std::string& targetHex);
    uint64_t getTargetDifficulty(const std::string& targetHex);
    bool checkHashDifficulty(const uint8_t* hash, uint64_t difficulty);
    bool validateHash(const std::string& hashHex, const std::string& targetHex);
    std::string expandTarget(const std::string& targetHex);
    bool checkHash(const uint8_t* hash, const std::string& target);
} 