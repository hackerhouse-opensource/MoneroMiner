#include "HashValidation.h"
#include "Utils.h"
#include "Globals.h"
#include <algorithm>
#include <cstring>
#include <sstream>
#include <iomanip>
#include <cmath>

namespace HashValidation {

static bool firstHashShown = false;

bool checkHash(const uint8_t* hash, const std::string& targetHex) {
    if (!hash || targetHex.empty()) {
        return false;
    }

    std::vector<uint8_t> target = expandTarget(targetHex);
    if (target.empty()) {
        return false;
    }

    // Compare hash with target (big-endian)
    for (size_t i = 0; i < 32; ++i) {
        if (hash[i] < target[i]) {
            return true;
        }
        if (hash[i] > target[i]) {
            return false;
        }
    }

    return true; // Equal is considered valid
}

bool meetsTarget(const std::vector<uint8_t>& hash, const std::vector<uint8_t>& target) {
    if (debugMode) {
        std::stringstream ss;
        ss << "[" << getCurrentTimestamp() << "] randomx  hash comparison:" << std::endl;
        ss << "  Hash: " << bytesToHex(hash) << std::endl;
        ss << "  Target: " << bytesToHex(target) << std::endl;
        threadSafePrint(ss.str(), true);
    }

    // Compare hash with target in big-endian order
    for (size_t i = 0; i < hash.size(); i++) {
        if (hash[i] < target[i]) {
            if (debugMode) {
                std::stringstream ss;
                ss << "[" << getCurrentTimestamp() << "] randomx  share found:" << std::endl;
                ss << "  Hash byte " << i << ": 0x" << std::hex << (int)hash[i] << std::endl;
                ss << "  Target byte " << i << ": 0x" << std::hex << (int)target[i] << std::endl;
                threadSafePrint(ss.str(), true);
            }
            return true;
        }
        if (hash[i] > target[i]) {
            if (debugMode) {
                std::stringstream ss;
                ss << "[" << getCurrentTimestamp() << "] randomx  hash rejected:" << std::endl;
                ss << "  Hash byte " << i << ": 0x" << std::hex << (int)hash[i] << std::endl;
                ss << "  Target byte " << i << ": 0x" << std::hex << (int)target[i] << std::endl;
                threadSafePrint(ss.str(), true);
            }
            return false;
        }
    }
    
    if (debugMode) {
        std::stringstream ss;
        ss << "[" << getCurrentTimestamp() << "] randomx  hash equals target" << std::endl;
        threadSafePrint(ss.str(), true);
    }
    return true;
}

std::vector<uint8_t> expandTarget(const std::string& compactTarget) {
    static bool firstTargetExpansion = true;
    std::stringstream ss;
    
    if (firstTargetExpansion && !firstHashShown) {
        ss << "\nExpanding target:" << std::endl;
        ss << "  Compact target: " << compactTarget << std::endl;
        threadSafePrint(ss.str(), true);
    }

    // Remove any "0x" prefix if present
    std::string target = compactTarget;
    if (target.length() >= 2 && target.substr(0, 2) == "0x") {
        target = target.substr(2);
    }

    // Ensure the target is exactly 8 hex characters (4 bytes)
    if (target.length() != 8) {
        if (firstTargetExpansion && !firstHashShown) {
            threadSafePrint("Error: Target must be 8 hex characters (4 bytes), got: " + target, true);
        }
        return std::vector<uint8_t>();
    }

    // Convert hex string to uint32_t
    uint32_t compact;
    try {
        compact = std::stoul(target, nullptr, 16);
    } catch (const std::exception& e) {
        if (firstTargetExpansion && !firstHashShown) {
            threadSafePrint("Error converting target to uint32: " + std::string(e.what()), true);
        }
        return std::vector<uint8_t>();
    }

    // Create 256-bit target (32 bytes)
    std::vector<uint8_t> expandedTarget(32, 0);

    // Extract size and mantissa from compact target
    uint8_t size = (compact >> 24) & 0xFF;
    uint32_t mantissa = compact & 0x00FFFFFF;

    // For target f3220000:
    // size = 0xf3 (243)
    // mantissa = 0x220000
    // The target is calculated as: mantissa * 2^(8 * (32 - size))

    // Calculate the number of bytes to shift mantissa
    int shift = 32 - size;
    if (shift < 0) shift = 0;
    if (shift > 31) shift = 31;

    // Place mantissa at the correct position (big-endian)
    if (shift <= 28) { // We need at least 4 bytes for mantissa
        expandedTarget[shift] = (mantissa >> 16) & 0xFF;
        expandedTarget[shift + 1] = (mantissa >> 8) & 0xFF;
        expandedTarget[shift + 2] = mantissa & 0xFF;
    }

    // Fill remaining bytes with 0xFF
    for (int i = shift + 3; i < 32; i++) {
        expandedTarget[i] = 0xFF;
    }

    // Only show debug output for first hash attempt
    if (firstTargetExpansion && !firstHashShown) {
        ss.str("");
        ss << "Target expansion details:" << std::endl;
        ss << "  Original target: " << compactTarget << std::endl;
        ss << "  Cleaned target: " << target << std::endl;
        ss << "  Compact value: 0x" << std::hex << std::setw(8) << std::setfill('0') << compact << std::endl;
        ss << "  Size: 0x" << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(size) 
           << " (" << std::dec << static_cast<int>(size) << ")" << std::endl;
        ss << "  Mantissa: 0x" << std::hex << std::setw(6) << std::setfill('0') << mantissa << std::endl;
        ss << "  Shift: " << std::dec << shift << " bytes" << std::endl;
        ss << "  Pool difficulty: " << std::dec << getTargetDifficulty(compactTarget) << std::endl;
        ss << "  Expanded target (hex): " << bytesToHex(expandedTarget) << std::endl;
        ss << "  Expanded target bytes: ";
        for (const auto& byte : expandedTarget) {
            ss << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(byte) << " ";
        }
        ss << std::endl;
        threadSafePrint(ss.str(), true);
        firstTargetExpansion = false;
    }

    return expandedTarget;
}

std::string formatHash(const std::vector<uint8_t>& hash) {
    std::stringstream ss;
    ss << std::hex << std::setfill('0');
    for (const auto& byte : hash) {
        ss << std::setw(2) << static_cast<int>(byte);
    }
    return ss.str();
}

bool validateHash(const std::string& hashHex, const std::string& targetHex) {
    std::stringstream ss;
    
    // Only show debug output for first hash attempt
    if (!firstHashShown) {
        ss << "\nValidating hash:" << std::endl;
        ss << "  Hash: " << hashHex << std::endl;
        ss << "  Target: " << targetHex << std::endl;
        ss << "  Target difficulty: " << std::dec << getTargetDifficulty(targetHex) << std::endl;
        threadSafePrint(ss.str(), true);
    }
    
    // Convert hex strings to byte arrays
    std::vector<uint8_t> hashBytes = hexToBytes(hashHex);
    std::vector<uint8_t> targetBytes = expandTarget(targetHex);
    
    if (hashBytes.empty() || targetBytes.empty()) {
        if (!firstHashShown) {
            threadSafePrint("Invalid hash or target format", true);
        }
        return false;
    }
    
    // Only show debug output for first hash attempt
    if (!firstHashShown) {
        ss.str("");
        ss << "Hash validation:" << std::endl;
        ss << "  Hash bytes: " << bytesToHex(hashBytes) << std::endl;
        ss << "  Target bytes: " << bytesToHex(targetBytes) << std::endl;
        ss << "  Hash bytes (hex): ";
        for (const auto& byte : hashBytes) {
            ss << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(byte) << " ";
        }
        ss << std::endl;
        ss << "  Target bytes (hex): ";
        for (const auto& byte : targetBytes) {
            ss << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(byte) << " ";
        }
        ss << std::endl;
        threadSafePrint(ss.str(), true);
    }
    
    // Compare in big-endian order
    for (size_t i = 0; i < hashBytes.size(); i++) {
        if (hashBytes[i] < targetBytes[i]) {
            if (!firstHashShown) {
                ss.str("");
                ss << "Hash is less than target at byte " << i << ":" << std::endl;
                ss << "  Hash byte: 0x" << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(hashBytes[i]) << std::endl;
                ss << "  Target byte: 0x" << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(targetBytes[i]) << std::endl;
                ss << "  Hash value: " << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(hashBytes[i]) << std::endl;
                ss << "  Target value: " << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(targetBytes[i]) << std::endl;
                threadSafePrint(ss.str(), true);
                firstHashShown = true;
            }
            return true;
        }
        if (hashBytes[i] > targetBytes[i]) {
            if (!firstHashShown) {
                ss.str("");
                ss << "Hash is greater than target at byte " << i << ":" << std::endl;
                ss << "  Hash byte: 0x" << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(hashBytes[i]) << std::endl;
                ss << "  Target byte: 0x" << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(targetBytes[i]) << std::endl;
                ss << "  Hash value: " << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(hashBytes[i]) << std::endl;
                ss << "  Target value: " << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(targetBytes[i]) << std::endl;
                threadSafePrint(ss.str(), true);
                firstHashShown = true;
            }
            return false;
        }
    }
    
    if (!firstHashShown) {
        threadSafePrint("Hash equals target", true);
        firstHashShown = true;
    }
    return true;
}

uint64_t getTargetDifficulty(const std::string& targetHex) {
    // Remove any "0x" prefix if present
    std::string target = targetHex;
    if (target.length() >= 2 && target.substr(0, 2) == "0x") {
        target = target.substr(2);
    }

    // Ensure the target is exactly 8 hex characters (4 bytes)
    if (target.length() != 8) {
        return 0;
    }

    // Convert hex string to uint32_t
    uint32_t compact;
    try {
        compact = std::stoul(target, nullptr, 16);
    } catch (const std::exception& e) {
        return 0;
    }

    // Extract size and mantissa
    uint8_t size = (compact >> 24) & 0xFF;
    uint32_t mantissa = compact & 0x00FFFFFF;

    if (mantissa == 0) {
        return 0;
    }

    // In Monero:
    // difficulty = 2^256 / (mantissa * 2^(8 * (32 - size)))
    
    // For f3220000:
    // size = 0xf3 (243)
    // mantissa = 0x220000
    // difficulty = 2^256 / (0x220000 * 2^(8 * (32 - 243)))
    
    // Calculate difficulty using 64-bit arithmetic
    uint64_t difficulty;
    
    // Calculate shift based on size
    int shift = 32 - size;
    if (shift < 0) {
        // Target is too large, return minimum difficulty
        return 1;
    }

    // Calculate difficulty = 2^256 / (mantissa * 2^(8 * shift))
    // = (2^256 / 2^(8 * shift)) / mantissa
    // = 2^(256 - 8 * shift) / mantissa
    int bits = 256 - (8 * shift);
    
    if (bits <= 64) {
        // We can calculate this directly
        difficulty = (1ULL << bits) / mantissa;
    } else {
        // For large values, approximate using max uint64
        difficulty = 0xFFFFFFFFFFFFFFFFULL / mantissa;
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