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

    // Division by 64-bit value using verified algorithm
    uint256_t operator/(uint64_t divisor) const {
        if (divisor == 0) return uint256_t();
        if (divisor == 1) return *this;
        
        // For Monero mining, we're typically dividing (2^256-1) by difficulty
        // Use Knuth's Algorithm D adapted for 256/64 division
        
        uint256_t quotient;
        uint64_t remainder = 0;
        
        // Process 64-bit words from MSW to LSW
        for (int i = 3; i >= 0; i--) {
            // We need to divide the 128-bit number (remainder:data[i]) by divisor
            
            // Special case: if remainder is 0, simple division
            if (remainder == 0) {
                quotient.data[i] = data[i] / divisor;
                remainder = data[i] % divisor;
            }
            // General case: need to handle the high part
            else {
                // Calculate quotient and remainder for this word
                // dividend = (remainder << 64) + data[i]
                // We'll use bit-by-bit long division
                
                uint64_t word_quotient = 0;
                uint64_t word_data = data[i];
                
                // Process each bit
                for (int bit = 63; bit >= 0; bit--) {
                    // Shift remainder and bring down next bit
                    remainder <<= 1;
                    if (word_data & (1ULL << bit)) {
                        remainder |= 1;
                    }
                    
                    // Check if we can subtract
                    if (remainder >= divisor) {
                        remainder -= divisor;
                        word_quotient |= (1ULL << bit);
                    }
                }
                
                quotient.data[i] = word_quotient;
            }
        }
        
        return quotient;
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
