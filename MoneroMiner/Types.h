#pragma once

#include <cstdint>
#include <cstring>
#include <string>
#include <sstream>
#include <iomanip>

struct uint256_t {
    uint64_t data[4];  // Little-endian: data[0] = LSW, data[3] = MSW
    
    uint256_t() {
        std::memset(data, 0, sizeof(data));
    }
    
    uint256_t(uint64_t low) {
        data[0] = low;
        data[1] = 0;
        data[2] = 0;
        data[3] = 0;
    }
    
    // Construct from 32-byte array (little-endian)
    explicit uint256_t(const uint8_t* bytes) {
        // CRITICAL FIX: Read bytes in correct little-endian order
        // bytes[0-7] = data[0] (LSW), bytes[8-15] = data[1], etc.
        for (int wordIdx = 0; wordIdx < 4; wordIdx++) {
            data[wordIdx] = 0;
            for (int byteIdx = 0; byteIdx < 8; byteIdx++) {
                data[wordIdx] |= static_cast<uint64_t>(bytes[wordIdx * 8 + byteIdx]) << (byteIdx * 8);
            }
        }
    }
    
    // Convert to 32-byte array (little-endian)
    void toBytes(uint8_t* bytes) const {
        for (int i = 0; i < 4; i++) {
            for (int j = 0; j < 8; j++) {
                bytes[i * 8 + j] = static_cast<uint8_t>((data[i] >> (j * 8)) & 0xFF);
            }
        }
    }
    
    // Comparison operators (compare from MSW to LSW)
    bool operator<(const uint256_t& other) const {
        for (int i = 3; i >= 0; i--) {
            if (data[i] < other.data[i]) return true;
            if (data[i] > other.data[i]) return false;
        }
        return false;
    }
    
    bool operator<=(const uint256_t& other) const {
        return (*this < other) || (*this == other);
    }
    
    bool operator>(const uint256_t& other) const {
        return other < *this;
    }
    
    bool operator>=(const uint256_t& other) const {
        return other <= *this;
    }
    
    bool operator==(const uint256_t& other) const {
        return data[0] == other.data[0] && data[1] == other.data[1] &&
               data[2] == other.data[2] && data[3] == other.data[3];
    }
    
    bool operator!=(const uint256_t& other) const {
        return !(*this == other);
    }
    
    // Convert to hex string (little-endian display for debugging)
    std::string toHex() const {
        std::stringstream ss;
        // Display in little-endian order (as stored)
        for (int i = 0; i < 4; i++) {
            ss << std::hex << std::setw(16) << std::setfill('0') << data[i];
        }
        return ss.str();
    }
    
    // Convert to hex string (big-endian display for human readability)
    std::string toHexBE() const {
        std::stringstream ss;
        for (int i = 3; i >= 0; i--) {
            ss << std::hex << std::setw(16) << std::setfill('0') << data[i];
        }
        return ss.str();
    }
};
