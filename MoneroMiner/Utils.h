#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include <mutex>

class Utils {
public:
    // Hex conversion utilities
    static std::string bytesToHex(const std::vector<uint8_t>& bytes);
    static std::string bytesToHex(const uint8_t* bytes, size_t length);
    static std::vector<uint8_t> hexToBytes(const std::string& hex);
    static std::string formatHex(uint64_t value, int width);
    
    // Hash utilities
    static bool compareHashes(const uint8_t* hash1, const uint8_t* hash2, size_t length);
    static void reverseBytes(uint8_t* bytes, size_t length);
    static uint64_t hashToUint64(const uint8_t* hash, int offset = 0);
    
    // Formatting utilities
    static std::string formatHashrate(double hashrate);
    static std::string formatThreadId(int threadId);
    static std::string formatRuntime(uint64_t seconds);

    // Logging utilities
    static void initializeLogging(const std::string& filename);
    static void cleanupLogging();
    static void threadSafePrint(const std::string& message, bool addNewline = true);
    static std::string getCurrentTimestamp();

private:
    static std::mutex printMutex;
    static uint8_t hexCharToInt(char c);
    static char intToHexChar(uint8_t value);
};

// Global functions
void threadSafePrint(const std::string& message, bool addNewline = true); 