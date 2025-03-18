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
    if (debugMode) {
        std::stringstream ss;
        ss << "\nExpanding target:" << std::endl;
        ss << "  Compact target: " << compactTarget << std::endl;
        ss << "  Target difficulty: " << std::dec << getTargetDifficulty(compactTarget) << std::endl;
        threadSafePrint(ss.str(), true);
    }

    // Remove any "0x" prefix if present
    std::string target = compactTarget;
    if (target.length() >= 2 && target.substr(0, 2) == "0x") {
        target = target.substr(2);
    }

    // Ensure the target is exactly 8 hex characters (4 bytes)
    if (target.length() != 8) {
        threadSafePrint("Error: Target must be 8 hex characters (4 bytes), got: " + target, true);
        return std::vector<uint8_t>();
    }

    // Convert hex string to bytes
    std::vector<uint8_t> targetBytes;
    targetBytes.reserve(4);
    
    try {
        for (size_t i = 0; i < target.length(); i += 2) {
            std::string byteString = target.substr(i, 2);
            targetBytes.push_back(static_cast<uint8_t>(std::stoi(byteString, nullptr, 16)));
        }
    } catch (const std::exception& e) {
        threadSafePrint("Error converting target to bytes: " + std::string(e.what()), true);
        return std::vector<uint8_t>();
    }

    if (targetBytes.size() != 4) {
        threadSafePrint("Error: Invalid target size (expected 4 bytes), got: " + std::to_string(targetBytes.size()), true);
        return std::vector<uint8_t>();
    }

    // Create 256-bit target (32 bytes)
    std::vector<uint8_t> expandedTarget(32, 0);

    // Place the compact target in the least significant 4 bytes (big-endian)
    expandedTarget[28] = targetBytes[0];
    expandedTarget[29] = targetBytes[1];
    expandedTarget[30] = targetBytes[2];
    expandedTarget[31] = targetBytes[3];

    if (debugMode) {
        std::stringstream ss;
        ss << "Target expansion details:" << std::endl;
        ss << "  Original target: " << compactTarget << std::endl;
        ss << "  Cleaned target: " << target << std::endl;
        ss << "  Target bytes (hex): ";
        for (const auto& byte : targetBytes) {
            ss << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(byte) << " ";
        }
        ss << std::endl;
        ss << "  Expanded target (hex): " << bytesToHex(expandedTarget) << std::endl;
        ss << "  Expanded target bytes (hex): ";
        for (const auto& byte : expandedTarget) {
            ss << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(byte) << " ";
        }
        ss << std::endl;
        ss << "  Target difficulty: " << std::dec << getTargetDifficulty(compactTarget) << std::endl;
        threadSafePrint(ss.str(), true);
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
    if (debugMode) {
        std::stringstream ss;
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
        threadSafePrint("Invalid hash or target format", true);
        return false;
    }
    
    if (debugMode) {
        std::stringstream ss;
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
            if (debugMode) {
                std::stringstream ss;
                ss << "Hash is less than target at byte " << i << ":" << std::endl;
                ss << "  Hash byte: 0x" << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(hashBytes[i]) << std::endl;
                ss << "  Target byte: 0x" << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(targetBytes[i]) << std::endl;
                ss << "  Hash value: " << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(hashBytes[i]) << std::endl;
                ss << "  Target value: " << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(targetBytes[i]) << std::endl;
                threadSafePrint(ss.str(), true);
            }
            return true;
        }
        if (hashBytes[i] > targetBytes[i]) {
            if (debugMode) {
                std::stringstream ss;
                ss << "Hash is greater than target at byte " << i << ":" << std::endl;
                ss << "  Hash byte: 0x" << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(hashBytes[i]) << std::endl;
                ss << "  Target byte: 0x" << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(targetBytes[i]) << std::endl;
                ss << "  Hash value: " << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(hashBytes[i]) << std::endl;
                ss << "  Target value: " << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(targetBytes[i]) << std::endl;
                threadSafePrint(ss.str(), true);
            }
            return false;
        }
    }
    
    if (debugMode) {
        threadSafePrint("Hash equals target", true);
    }
    return true;
}

uint64_t getTargetDifficulty(const std::string& targetHex) {
    std::vector<uint8_t> target = expandTarget(targetHex);
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