#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include <iostream>

namespace HashValidation {
    // Format hash bytes to hex string
    std::string formatHash(const std::vector<uint8_t>& hash);

    // Get target difficulty from compact target
    uint64_t getTargetDifficulty(const std::string& targetHex);

    // Validate hash against target using 4-byte tail comparison
    bool validateHash(const std::string& hashHex, const std::string& targetHex);

    // Check hash against target (internal implementation)
    bool checkHash(const uint8_t* hash, const std::string& targetHex);

    // Convert hash to hex string
    std::string hashToHex(const uint8_t* hash, size_t size);

    // Convert hex string to bytes
    std::vector<uint8_t> hexToBytes(const std::string& hex);

    // Debug output functions
    void printHashValidation(const std::string& hashHex, const std::string& targetHex);
    void printTargetExpansion(const std::string& targetHex);
    void printHashComparison(const uint8_t* hash, const uint8_t* target);

    // Legacy function for backward compatibility
    bool meetsTarget(const uint8_t* hash, const std::string& target);
} 