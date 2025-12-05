#pragma once

#include <cstdint>
#include <array>
#include <string>
#include <sstream>
#include <iomanip>
#include <algorithm>

// Undefine Windows min/max macros to avoid conflicts
#ifdef max
#undef max
#endif
#ifdef min
#undef min
#endif

struct uint256_t {
    std::array<uint64_t, 4> data; // Little-endian: data[0] is LSW, data[3] is MSW

    uint256_t() : data{0, 0, 0, 0} {}
    
    uint256_t(const uint8_t* bytes) {
        for (int wordIdx = 0; wordIdx < 4; wordIdx++) {
            uint64_t word = 0;
            int baseByteIdx = wordIdx * 8;
            for (int byteInWord = 0; byteInWord < 8; byteInWord++) {
                word |= static_cast<uint64_t>(bytes[baseByteIdx + byteInWord]) << (byteInWord * 8);
            }
            data[wordIdx] = word;
        }
    }

    bool operator<(const uint256_t& other) const {
        for (int i = 3; i >= 0; i--) {
            if (data[i] < other.data[i]) return true;
            if (data[i] > other.data[i]) return false;
        }
        return false;
    }

    bool operator>(const uint256_t& other) const {
        return other < *this;
    }

    bool operator<=(const uint256_t& other) const {
        return !(*this > other);
    }

    bool operator>=(const uint256_t& other) const {
        return !(*this < other);
    }

    bool operator==(const uint256_t& other) const {
        return data[0] == other.data[0] && 
               data[1] == other.data[1] && 
               data[2] == other.data[2] && 
               data[3] == other.data[3];
    }

    bool operator!=(const uint256_t& other) const {
        return !(*this == other);
    }

    // Portable 256-bit division by 64-bit value
    uint256_t operator/(uint64_t divisor) const {
        if (divisor == 0) return uint256_t();
        if (divisor == 1) return *this;
        
        uint256_t result;
        uint64_t remainder = 0;
        
        // Process each 64-bit word from MSW to LSW
        for (int i = 3; i >= 0; i--) {
            if (remainder == 0) {
                // Simple case
                result.data[i] = data[i] / divisor;
                remainder = data[i] % divisor;
            } else {
                // Complex case: need to handle (remainder << 64) | data[i]
                // Use bit-by-bit division
                uint64_t quotient = 0;
                
                for (int bit = 63; bit >= 0; bit--) {
                    remainder <<= 1;
                    if (data[i] & (1ULL << bit)) {
                        remainder |= 1;
                    }
                    
                    if (remainder >= divisor) {
                        remainder -= divisor;
                        quotient |= (1ULL << bit);
                    }
                }
                
                result.data[i] = quotient;
            }
        }
        
        return result;
    }

    // Create max value (2^256 - 1)
    static uint256_t maximum() {
        uint256_t result;
        result.data[0] = 0xFFFFFFFFFFFFFFFFULL;
        result.data[1] = 0xFFFFFFFFFFFFFFFFULL;
        result.data[2] = 0xFFFFFFFFFFFFFFFFULL;
        result.data[3] = 0xFFFFFFFFFFFFFFFFULL;
        return result;
    }

    std::string toHex() const {
        std::stringstream ss;
        // Display in big-endian order (MSW first) for human readability
        for (int i = 3; i >= 0; i--) {
            for (int j = 7; j >= 0; j--) {
                uint8_t byte = static_cast<uint8_t>((data[i] >> (j * 8)) & 0xFF);
                ss << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(byte);
            }
        }
        return ss.str();
    }
};
