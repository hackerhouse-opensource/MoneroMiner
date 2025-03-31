#include "HashValidation.h"
#include "Utils.h"
#include "Globals.h"
#include <algorithm>
#include <cstring>
#include <sstream>
#include <iomanip>
#include <cmath>

namespace HashValidation {

// Structure to hold 256-bit value as 4 64-bit integers
struct uint256_t {
    uint64_t words[4];  // Most significant word first (big-endian)

    uint256_t() : words{0, 0, 0, 0} {}

    // Shift left by n bits
    void shift_left(int n) {
        if (n >= 256) {
            words[0] = words[1] = words[2] = words[3] = 0;
            return;
        }

        while (n > 0) {
            int shift = std::min(n, 64);
            uint64_t carry = 0;
            
            // Shift each word left
            for (int i = 3; i >= 0; i--) {
                uint64_t next_carry = words[i] >> (64 - shift);
                words[i] = (words[i] << shift) | carry;
                carry = next_carry;
            }
            
            n -= shift;
        }
    }

    // Compare with another 256-bit value
    bool operator<(const uint256_t& other) const {
        for (int i = 0; i < 4; i++) {
            if (words[i] < other.words[i]) return true;
            if (words[i] > other.words[i]) return false;
        }
        return false;
    }

    // Convert to hex string
    std::string to_hex() const {
        std::stringstream ss;
        ss << std::hex << std::setfill('0');
        for (int i = 0; i < 4; i++) {
            ss << std::setw(16) << words[i];
        }
        return ss.str();
    }
};

static bool firstHashShown = false;
static uint64_t hashCount = 0;

bool checkHash(const uint8_t* hash, const std::string& targetHex) {
    // Remove "0x" prefix if present
    std::string target = targetHex;
    if (target.substr(0, 2) == "0x") target = target.substr(2);
    uint32_t compact = std::stoul(target, nullptr, 16);

    // Extract exponent and mantissa
    uint8_t exponent = (compact >> 24) & 0xFF;  // 0xf3
    uint32_t mantissa = compact & 0x00FFFFFF;   // 0x220000

    // Calculate the shift amount: 8 * (exponent - 3)
    int shift = 8 * (exponent - 3);  // For 0xf3: 8 * (243 - 3) = 1920 bits

    // Create 256-bit target
    uint256_t full_target;
    // For target 0xf3220000:
    // - exponent = 0xf3 (243)
    // - mantissa = 0x220000
    // - shift = 8 * (243 - 3) = 1920 bits
    // We need to place 0x220000 at position 240 (1920/8) bytes from the right
    // This means it goes in Word 0 (the most significant word)
    int bytePosition = shift / 8;  // Convert bits to bytes
    if (bytePosition < 29) {  // Ensure we don't overflow
        // For target 0xf3220000:
        // - shift = 1920 bits
        // - We need to place 0x220000 at the start of Word 0
        // - This means we need to shift left by (256 - 24) = 232 bits
        // - This will place 0x220000 at the start of Word 0
        full_target.words[0] = static_cast<uint64_t>(mantissa);
        full_target.shift_left(232);  // Use the safe shift_left method instead of direct shift
    }

    // Convert hash to 256-bit integer (big-endian)
    uint256_t hash_value;
    for (int i = 0; i < 32; i++) {
        int word_idx = i / 8;
        int byte_idx = 7 - (i % 8);
        hash_value.words[word_idx] |= (static_cast<uint64_t>(hash[i]) << (byte_idx * 8));
    }

    // Compare hash with target
    bool valid = hash_value < full_target;

    if (debugMode) {
        std::stringstream ss;
        ss << "\n=== RandomX Hash Calculation Debug ===\n\n";
        ss << "Input Data:\n";
        ss << "Blob Template (hex):\n";
        for (int i = 0; i < 32; i++) {
            ss << "  " << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(hash[i]) << " ";
            if ((i + 1) % 16 == 0) ss << "\n";
        }
        ss << "\n";

        ss << "Hash Output:\n";
        ss << "Hash (hex):\n";
        for (int i = 0; i < 32; i++) {
            ss << "  " << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(hash[i]) << " ";
            if ((i + 1) % 16 == 0) ss << "\n";
        }
        ss << "\n";

        ss << "Target Information:\n";
        ss << "Compact Target: 0x" << target << "\n";
        ss << "Target Components:\n";
        ss << "  Exponent: 0x" << (int)exponent << std::hex << std::setw(2) << std::setfill('0')
           << " (" << std::dec << (int)exponent << ")\n";
        ss << "  Mantissa: 0x" << std::hex << std::setw(6) << std::setfill('0') << mantissa 
           << " (" << std::dec << mantissa << ")\n";
        ss << "  Shift amount: " << std::dec << shift << " bits\n";
        ss << "Expanded Target (256-bit):\n";
        ss << "  Word 0: 0x" << std::hex << std::setw(16) << std::setfill('0') << full_target.words[0] << "\n";
        ss << "  Word 1: 0x" << std::hex << std::setw(16) << std::setfill('0') << full_target.words[1] << "\n";
        ss << "  Word 2: 0x" << std::hex << std::setw(16) << std::setfill('0') << full_target.words[2] << "\n";
        ss << "  Word 3: 0x" << std::hex << std::setw(16) << std::setfill('0') << full_target.words[3] << "\n";
        ss << "Share Validation:\n";
        ss << "  Hash Word 0: 0x" << std::hex << std::setw(16) << std::setfill('0') << hash_value.words[0] << "\n";
        ss << "  Target Word 0: 0x" << std::hex << std::setw(16) << std::setfill('0') << full_target.words[0] << "\n";
        ss << "Meets Target: " << (valid ? "Yes" : "No") << "\n";
        ss << "=====================================\n";
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

    // Convert hex string to uint32_t
    uint32_t compact;
    try {
        compact = std::stoul(target, nullptr, 16);
    } catch (const std::exception& e) {
        threadSafePrint("Error converting target to uint32: " + std::string(e.what()), true);
        return std::vector<uint8_t>();
    }

    // Extract exponent and mantissa
    uint8_t exponent = (compact >> 24) & 0xFF;  // 0xf3
    uint32_t mantissa = compact & 0x00FFFFFF;   // 0x220000

    // Calculate the shift amount: 8 * (exponent - 3)
    int shift = 8 * (exponent - 3);  // For 0xf3: 8 * (243 - 3) = 1920 bits

    // Create 256-bit target
    uint256_t full_target;
    // For target 0xf3220000:
    // - exponent = 0xf3 (243)
    // - mantissa = 0x220000
    // - shift = 8 * (243 - 3) = 1920 bits
    // We need to place 0x220000 at position 240 (1920/8) bytes from the right
    // This means it goes in Word 0 (the most significant word)
    int bytePosition = shift / 8;  // Convert bits to bytes
    if (bytePosition < 29) {  // Ensure we don't overflow
        // For target 0xf3220000:
        // - shift = 1920 bits
        // - We need to place 0x220000 at the start of Word 0
        // - This means we need to shift left by (256 - 24) = 232 bits
        // - This will place 0x220000 at the start of Word 0
        full_target.words[0] = static_cast<uint64_t>(mantissa);
        full_target.shift_left(232);  // Use the safe shift_left method instead of direct shift
    }

    // Convert to byte array
    std::vector<uint8_t> expandedTarget(32);
    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 8; j++) {
            expandedTarget[i * 8 + j] = (full_target.words[i] >> (56 - j * 8)) & 0xFF;
        }
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

double getTargetDifficulty(const std::string& targetHex) {
    std::string target = targetHex;
    if (target.substr(0, 2) == "0x") target = target.substr(2);
    uint32_t compact = std::stoul(target, nullptr, 16);

    // Extract exponent and mantissa
    uint8_t exponent = (compact >> 24) & 0xFF;
    uint32_t mantissa = compact & 0x00FFFFFF;

    // Calculate difficulty as 2^256 / target
    // Since we can't do exact 256-bit division, we'll use an approximation
    // based on the compact target format
    double difficulty = std::pow(2.0, 32.0) / compact;
    
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
    std::cout << "  Target difficulty: " << std::fixed << std::setprecision(2) << getTargetDifficulty(targetHex) << std::endl;
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
    std::cout << "  Pool difficulty: " << std::fixed << std::setprecision(2) << getTargetDifficulty(targetHex) << std::endl;
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

void printTargetDetails(const std::string& targetHex) {
    std::string target = targetHex;
    if (target.substr(0, 2) == "0x") target = target.substr(2);
    uint32_t compact = std::stoul(target, nullptr, 16);

    std::cout << "\nTarget details:" << std::endl;
    std::cout << "  Compact target: 0x" << target << std::endl;
    std::cout << "  Exponent: 0x" << std::hex << std::setw(2) << std::setfill('0') 
             << static_cast<int>((compact >> 24) & 0xFF) << std::endl;
    std::cout << "  Mantissa: 0x" << std::hex << std::setw(6) << std::setfill('0') 
             << (compact & 0x00FFFFFF) << std::endl;
    std::cout << "  Difficulty: " << std::fixed << std::setprecision(2) 
             << getTargetDifficulty(targetHex) << std::endl;
}

void printHashDetails(const uint8_t* hash) {
    std::cout << "\nHash details:" << std::endl;
    std::cout << "  Hash (hex): " << hashToHex(hash, 32) << std::endl;
    std::cout << "  Hash bytes: " << formatHash(std::vector<uint8_t>(hash, hash + 32)) << std::endl;
    std::cout << "  Hash Word 0: 0x" << std::hex << std::setw(16) << std::setfill('0') 
             << (static_cast<uint64_t>(hash[0]) << 56 | 
                 static_cast<uint64_t>(hash[1]) << 48 | 
                 static_cast<uint64_t>(hash[2]) << 40 | 
                 static_cast<uint64_t>(hash[3]) << 32 | 
                 static_cast<uint64_t>(hash[4]) << 24 | 
                 static_cast<uint64_t>(hash[5]) << 16 | 
                 static_cast<uint64_t>(hash[6]) << 8 | 
                 static_cast<uint64_t>(hash[7])) << std::endl;
}

} 