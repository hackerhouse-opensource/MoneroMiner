#pragma once

#include <string>
#include <cstdint>
#include <vector>

class Utils {
public:
    // Hex conversion
    static std::vector<uint8_t> hexToBytes(const std::string& hex);
    static std::string bytesToHex(const std::vector<uint8_t>& bytes);
    static std::string bytesToHex(const uint8_t* data, size_t len);

    // Display-only helpers: RandomX hashes and targets are little-endian 256-bit values
    // (byte[0] = LSB), which is the byte order used for the actual PoW comparison and for
    // the hex submitted to the pool - never reverse those. These two helpers exist purely
    // to render that same value MSB-first for on-screen/log output, so a printed hash and
    // its printed target read left-to-right like ordinary big numbers and can be visually
    // compared. Do not use the output of these for anything sent to the pool or compared.
    static std::string bytesToHexReversed(const std::vector<uint8_t>& bytes);
    static std::string reverseHexByteOrder(const std::string& hex);
    static std::string formatHex(uint64_t value, int width);
    static std::string formatHex(uint32_t value, int width);
    static std::string formatHex(const uint8_t* data, size_t len);  // Add this
    
    // Nonce conversion
    static std::string nonceToHex(uint32_t nonce);
    
    // Thread-safe printing
    static void threadSafePrint(const std::string& message, bool toLog, bool addTimestamp = true);
    
    // Logging
    static void logToFile(const std::string& message);
    static void setLogFile(const std::string& filename);
    
    // Timestamp
    static std::string getCurrentTimestamp();
    static std::string getTimestamp();
    
    // Privilege and performance functions
    static bool enableLargePages();
    static bool isRunningElevated();
    static std::string getPrivilegeStatus();
};