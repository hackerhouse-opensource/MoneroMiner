#pragma once

#include <cstdint>
#include <vector>
#include <string>

// Lightweight 256-bit unsigned integer for Monero difficulty calculations
// Matches XMRig's implementation exactly
class uint256_t {
public:
    uint64_t data[4];  // Little-endian: data[0] is least significant
    
    uint256_t() {
        data[0] = data[1] = data[2] = data[3] = 0;
    }
    
    // Create from difficulty: target = (2^256 - 1) / difficulty
    static uint256_t fromDifficulty(uint64_t difficulty);
    
    // Load from little-endian 32-byte array (RandomX hash output)
    void fromLittleEndian(const uint8_t* bytes);
    
    // Load from big-endian 32-byte array
    void fromBigEndian(const uint8_t* bytes);
    
    // Convert to big-endian hex string for logging
    std::string toHex() const;
    
    // Comparison operators
    bool operator<(const uint256_t& other) const;
    bool operator<=(const uint256_t& other) const;
    bool operator>(const uint256_t& other) const;
    bool operator>=(const uint256_t& other) const;
    bool operator==(const uint256_t& other) const;
};
