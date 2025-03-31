#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include <iostream>

namespace HashValidation {
    // Constants for RandomX
    constexpr uint32_t TARGET_BITS = 256;
    constexpr uint32_t COMPACT_TARGET_SIZE = 4;
    constexpr uint32_t HASH_SIZE = 32;
    constexpr uint32_t MANTISSA_BITS = 24;
    constexpr uint32_t EXPONENT_BITS = 8;

    // Format hash bytes to hex string
    std::string formatHash(const std::vector<uint8_t>& hash);

    // Get target difficulty from compact target
    double getTargetDifficulty(const std::string& targetHex);

    // Convert hash to hex string
    std::string hashToHex(const uint8_t* hash, size_t size);

    // Convert hex string to bytes
    std::vector<uint8_t> hexToBytes(const std::string& hex);

    // Debug output functions
    void printHashValidation(const std::string& hashHex, const std::string& targetHex);
    void printTargetExpansion(const std::string& targetHex);
    void printHashComparison(const uint8_t* hash, const uint8_t* target);
    void printTargetDetails(const std::string& targetHex);
    void printHashDetails(const uint8_t* hash);
} 