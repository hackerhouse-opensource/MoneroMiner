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
static uint64_t hashCount = 0;

bool checkHash(const uint8_t* hash, const std::string& targetHex) {
    std::string target = targetHex;
    if (target.substr(0, 2) == "0x") target = target.substr(2);
    uint32_t compact = std::stoul(target, nullptr, 16); // 0xf3220000

    // Last 4 bytes of hash (little-endian)
    uint32_t hashTail = (hash[31] << 24) | (hash[30] << 16) | (hash[29] << 8) | hash[28];
    bool valid = hashTail <= compact;

    if (debugMode) {
        std::stringstream ss;
        ss << "Hash tail: 0x" << std::hex << std::setw(8) << std::setfill('0') << hashTail
           << " vs Target: 0x" << std::hex << std::setw(8) << std::setfill('0') << compact
           << " -> " << (valid ? "Valid" : "Invalid") << std::endl;
        threadSafePrint(ss.str(), true);
    }
    return valid;
}

bool meetsTarget(const std::vector<uint8_t>& hash, const std::vector<uint8_t>& target) {
    hashCount++;
    bool shouldShowDebug = !firstHashShown || hashCount == 10000;

    if (debugMode && shouldShowDebug) {
        std::stringstream ss;
        ss << "[" << getCurrentTimestamp() << "] randomx  hash comparison:" << std::endl;
        ss << "  Hash: " << bytesToHex(hash) << std::endl;
        ss << "  Target: " << bytesToHex(target) << std::endl;
        threadSafePrint(ss.str(), true);
    }

    // Compare hash with target in big-endian order
    for (size_t i = 0; i < hash.size(); i++) {
        if (hash[i] < target[i]) {
            if (debugMode && shouldShowDebug) {
                std::stringstream ss;
                ss << "[" << getCurrentTimestamp() << "] randomx  share found:" << std::endl;
                ss << "  Hash byte " << i << ": 0x" << std::hex << (int)hash[i] << std::endl;
                ss << "  Target byte " << i << ": 0x" << std::hex << (int)target[i] << std::endl;
                threadSafePrint(ss.str(), true);
            }
            return true;
        }
        if (hash[i] > target[i]) {
            if (debugMode && shouldShowDebug) {
                std::stringstream ss;
                ss << "[" << getCurrentTimestamp() << "] randomx  hash rejected:" << std::endl;
                ss << "  Hash byte " << i << ": 0x" << std::hex << (int)hash[i] << std::endl;
                ss << "  Target byte " << i << ": 0x" << std::hex << (int)target[i] << std::endl;
                threadSafePrint(ss.str(), true);
            }
            return false;
        }
    }
    
    if (debugMode && shouldShowDebug) {
        std::stringstream ss;
        ss << "[" << getCurrentTimestamp() << "] randomx  hash equals target" << std::endl;
        threadSafePrint(ss.str(), true);
    }
    return true;
}

std::vector<uint8_t> expandTarget(const std::string& compactTarget) {
    static bool firstTargetExpansion = true;
    std::stringstream ss;
    
    bool shouldShowDebug = !firstHashShown || hashCount == 10000;
    
    if (firstTargetExpansion && shouldShowDebug) {
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
        if (firstTargetExpansion && shouldShowDebug) {
            threadSafePrint("Error: Target must be 8 hex characters (4 bytes), got: " + target, true);
        }
        return std::vector<uint8_t>();
    }

    // Convert hex string to uint32_t
    uint32_t compact;
    try {
        compact = std::stoul(target, nullptr, 16);
    } catch (const std::exception& e) {
        if (firstTargetExpansion && shouldShowDebug) {
            threadSafePrint("Error converting target to uint32: " + std::string(e.what()), true);
        }
        return std::vector<uint8_t>();
    }

    // Create 256-bit target (32 bytes)
    std::vector<uint8_t> expandedTarget(32, 0); // Initialize with zeros

    // For RandomX pool mining, the target is the last 4 bytes of the 256-bit target
    // Assuming little-endian pool target (f3220000 = 00 00 22 f3)
    expandedTarget[28] = (compact >> 24) & 0xFF; // 0xf3
    expandedTarget[29] = (compact >> 16) & 0xFF; // 0x22
    expandedTarget[30] = (compact >> 8) & 0xFF;  // 0x00
    expandedTarget[31] = compact & 0xFF;         // 0x00

    // Only show debug output for first hash attempt or 10,000th hash
    if (firstTargetExpansion && shouldShowDebug) {
        ss.str("");
        ss << "Target expansion details:" << std::endl;
        ss << "  Original target: " << compactTarget << std::endl;
        ss << "  Cleaned target: " << target << std::endl;
        ss << "  Compact value: 0x" << std::hex << std::setw(8) << std::setfill('0') << compact << std::endl;
        ss << "  Pool difficulty: " << std::dec << (0xFFFFFFFFULL / compact) << std::endl;
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
    for (uint8_t byte : hash) {
        ss << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(byte) << " ";
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
    std::string target = targetHex;
    if (target.substr(0, 2) == "0x") target = target.substr(2);
    uint32_t compact = std::stoul(target, nullptr, 16);
    return compact ? (0xFFFFFFFFULL / compact) : 0;
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

std::string hashToHex(const uint8_t* hash, size_t size) {
    std::stringstream ss;
    for (size_t i = 0; i < size; i++) {
        ss << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(hash[i]);
    }
    return ss.str();
}

std::vector<uint8_t> hexToBytes(const std::string& hex) {
    std::vector<uint8_t> bytes;
    std::string h = hex;
    if (h.substr(0, 2) == "0x") h = h.substr(2);
    for (size_t i = 0; i < h.length(); i += 2) {
        std::string byteString = h.substr(i, 2);
        bytes.push_back(static_cast<uint8_t>(std::stoul(byteString, nullptr, 16)));
    }
    return bytes;
}

void printHashValidation(const std::string& hashHex, const std::string& targetHex) {
    std::cout << "\nValidating hash:" << std::endl;
    std::cout << "  Hash: " << hashHex << std::endl;
    std::cout << "  Target: " << targetHex << std::endl;
    std::cout << "  Target difficulty: " << getTargetDifficulty(targetHex) << std::endl;
}

void printTargetExpansion(const std::string& targetHex) {
    std::cout << "\nExpanding target:" << std::endl;
    std::cout << "  Compact target: " << targetHex << std::endl;
    std::cout << "\nTarget expansion details:" << std::endl;
    std::cout << "  Original target: " << targetHex << std::endl;
    std::string target = targetHex;
    if (target.substr(0, 2) == "0x") target = target.substr(2);
    std::cout << "  Cleaned target: " << target << std::endl;
    uint32_t compact = std::stoul(target, nullptr, 16);
    std::cout << "  Compact value: 0x" << std::hex << std::setw(8) << std::setfill('0') << compact << std::endl;
    std::cout << "  Pool difficulty: " << std::dec << getTargetDifficulty(targetHex) << std::endl;

    // Create expanded target (32 bytes)
    std::vector<uint8_t> expanded(32, 0);
    expanded[28] = (compact >> 24) & 0xFF;
    expanded[29] = (compact >> 16) & 0xFF;
    expanded[30] = (compact >> 8) & 0xFF;
    expanded[31] = compact & 0xFF;

    std::cout << "  Expanded target (hex): " << hashToHex(expanded.data(), 32) << std::endl;
    std::cout << "  Expanded target bytes: " << formatHash(expanded) << std::endl;
}

void printHashComparison(const uint8_t* hash, const uint8_t* target) {
    std::cout << "\nHash validation:" << std::endl;
    std::cout << "  Hash bytes: " << hashToHex(hash, 32) << std::endl;
    std::cout << "  Target bytes: " << hashToHex(target, 32) << std::endl;
    std::cout << "  Hash bytes (hex): " << formatHash(std::vector<uint8_t>(hash, hash + 32)) << std::endl;
    std::cout << "  Target bytes (hex): " << formatHash(std::vector<uint8_t>(target, target + 32)) << std::endl;

    // Find first byte where hash differs from target
    for (int i = 0; i < 32; i++) {
        if (hash[i] != target[i]) {
            std::cout << "\nHash is greater than target at byte " << i << ":" << std::endl;
            std::cout << "  Hash byte: 0x" << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(hash[i]) << std::endl;
            std::cout << "  Target byte: 0x" << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(target[i]) << std::endl;
            std::cout << "  Hash value: " << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(hash[i]) << std::endl;
            std::cout << "  Target value: " << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(target[i]) << std::endl;
            break;
        }
    }
}

} 