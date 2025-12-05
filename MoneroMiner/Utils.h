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
};