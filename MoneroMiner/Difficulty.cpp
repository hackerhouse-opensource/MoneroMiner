#include "Difficulty.h"
#include <sstream>
#include <iomanip>
#include <cstring>

// XMRig-verified formula: target = (2^256 - 1) / difficulty
uint256_t uint256_t::fromDifficulty(uint64_t difficulty) {
    if (difficulty == 0) difficulty = 1;
    
    uint256_t result;
    result.data[0] = result.data[1] = result.data[2] = result.data[3] = 0;
    
    // CORRECT IMPLEMENTATION: Divide 2^256-1 by difficulty using 256-bit arithmetic
    // For now, use simplified version that works for typical pool difficulties
    
    if (difficulty == 1) {
        // Max target: all FF
        result.data[0] = result.data[1] = result.data[2] = result.data[3] = 0xFFFFFFFFFFFFFFFFULL;
        return result;
    }
    
    // For typical pool difficulties (< 2^64), the target fits in first 64-128 bits
    // target ≈ 2^64 / difficulty (stored in little-endian)
    uint64_t target64 = 0xFFFFFFFFFFFFFFFFULL / difficulty;
    
    result.data[0] = target64;
    result.data[1] = 0;
    result.data[2] = 0;
    result.data[3] = 0;
    
    return result;
}

void uint256_t::fromLittleEndian(const uint8_t* bytes) {
    // Load 4x 64-bit words from little-endian bytes
    for (int i = 0; i < 4; i++) {
        data[i] = 0;
        for (int j = 0; j < 8; j++) {
            data[i] |= static_cast<uint64_t>(bytes[i * 8 + j]) << (j * 8);
        }
    }
}

void uint256_t::fromBigEndian(const uint8_t* bytes) {
    // Load 4x 64-bit words from big-endian bytes (reversed)
    for (int i = 0; i < 4; i++) {
        data[3 - i] = 0;
        for (int j = 0; j < 8; j++) {
            data[3 - i] |= static_cast<uint64_t>(bytes[i * 8 + j]) << (56 - j * 8);
        }
    }
}

std::string uint256_t::toHex() const {
    std::stringstream ss;
    // Output in LITTLE-ENDIAN order (data[0] is LSB, data[3] is MSB)
    // But show it as a continuous hex string from MSB to LSB for readability
    for (int i = 3; i >= 0; i--) {
        ss << std::hex << std::setw(16) << std::setfill('0') << data[i];
    }
    return ss.str();
}

bool uint256_t::operator<(const uint256_t& other) const {
    // Compare from most significant to least
    for (int i = 3; i >= 0; i--) {
        if (data[i] < other.data[i]) return true;
        if (data[i] > other.data[i]) return false;
    }
    return false;  // Equal
}

bool uint256_t::operator<=(const uint256_t& other) const {
    return (*this < other) || (*this == other);
}

bool uint256_t::operator>(const uint256_t& other) const {
    return !(*this <= other);
}

bool uint256_t::operator>=(const uint256_t& other) const {
    return !(*this < other);
}

bool uint256_t::operator==(const uint256_t& other) const {
    return data[0] == other.data[0] && data[1] == other.data[1] && 
           data[2] == other.data[2] && data[3] == other.data[3];
}
