#include "HashValidation.h"
#include "Utils.h"
#include "Globals.h"
#include <algorithm>
#include <cstring>
#include <sstream>
#include <iomanip>

namespace HashValidation {

bool checkHash(const uint8_t* hash, const std::string& targetHex) {
    if (!hash || targetHex.empty()) {
        return false;
    }

    std::vector<uint8_t> target = compactTo256BitTarget(targetHex);
    if (target.empty()) {
        return false;
    }

    // Compare hash with target (little-endian)
    for (size_t i = 0; i < 32; ++i) {
        if (hash[i] > target[i]) {
            return false;
        }
        if (hash[i] < target[i]) {
            return true;
        }
    }

    return true; // Equal is considered valid
}

std::string expandTarget(const std::string& targetHex) {
    threadSafePrint("Expanding target: " + targetHex, true);
    
    // Convert compact target to 256-bit target
    std::vector<uint8_t> target = compactTo256BitTarget(targetHex);
    if (target.empty()) {
        threadSafePrint("Failed to expand target: " + targetHex, true);
        return "";
    }
    
    std::string result = bytesToHex(target);
    threadSafePrint("Expanded target: " + result, true);
    return result;
}

bool validateHash(const std::string& hashHex, const std::string& targetHex) {
    threadSafePrint("Validating hash: " + hashHex + " against target: " + targetHex, true);
    
    // Convert hex strings to byte arrays
    std::vector<uint8_t> hashBytes = hexToBytes(hashHex);
    std::vector<uint8_t> targetBytes = hexToBytes(targetHex);
    
    if (hashBytes.empty() || targetBytes.empty()) {
        threadSafePrint("Invalid hash or target format", true);
        return false;
    }
    
    threadSafePrint("Hash bytes: " + bytesToHex(hashBytes), true);
    threadSafePrint("Target bytes: " + bytesToHex(targetBytes), true);
    
    // Compare in little-endian order
    for (size_t i = 0; i < hashBytes.size(); i++) {
        if (hashBytes[i] < targetBytes[i]) {
            threadSafePrint("Hash is less than target", true);
            return true;
        }
        if (hashBytes[i] > targetBytes[i]) {
            threadSafePrint("Hash is greater than target", true);
            return false;
        }
    }
    
    threadSafePrint("Hash equals target", true);
    return true;
}

std::vector<uint8_t> compactTo256BitTarget(const std::string& compactHex) {
    threadSafePrint("Converting compact target: " + compactHex, true);
    
    if (compactHex.length() != 8) {
        threadSafePrint("Invalid compact target length: " + std::to_string(compactHex.length()), true);
        return std::vector<uint8_t>();
    }
    
    // Parse compact target
    uint32_t compact;
    try {
        compact = std::stoul(compactHex, nullptr, 16);
    } catch (const std::exception& e) {
        threadSafePrint("Failed to parse compact target: " + std::string(e.what()), true);
        return std::vector<uint8_t>();
    }
    
    // Extract components
    uint8_t exponent = compact >> 24;
    uint32_t mantissa = compact & 0xFFFFFF;
    
    threadSafePrint("Compact target components - Exponent: " + std::to_string(exponent) + 
                   ", Mantissa: 0x" + std::to_string(mantissa), true);
    
    // Create 256-bit target in little-endian format
    std::vector<uint8_t> target(32, 0);
    if (exponent >= 3) {
        size_t offset = exponent - 3;
        if (offset < 32) {
            target[offset] = mantissa & 0xFF;
            if (offset + 1 < 32) {
                target[offset + 1] = (mantissa >> 8) & 0xFF;
            }
            if (offset + 2 < 32) {
                target[offset + 2] = (mantissa >> 16) & 0xFF;
            }
        }
    }
    
    threadSafePrint("Converted to 256-bit target: " + bytesToHex(target), true);
    return target;
}

uint64_t getTargetDifficulty(const std::string& targetHex) {
    std::vector<uint8_t> target = compactTo256BitTarget(targetHex);
    if (target.empty()) {
        return 0;
    }

    uint64_t difficulty = 0;
    for (size_t i = 0; i < 8; ++i) {
        difficulty = (difficulty << 8) | target[i];
    }

    return difficulty;
}

bool checkHashDifficulty(const uint8_t* hash, uint64_t difficulty) {
    if (!hash || difficulty == 0) {
        return false;
    }

    uint64_t hashValue = 0;
    for (size_t i = 0; i < 8; ++i) {
        hashValue = (hashValue << 8) | hash[i];
    }

    return hashValue <= difficulty;
}

} 