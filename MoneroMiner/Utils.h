#pragma once

#include <string>
#include <vector>
#include <cstdint>

// Logging functions
void initializeLogging(const std::string& filename);
void cleanupLogging();
void threadSafePrint(const std::string& message, bool addNewline = true);

// Utility functions
std::string getCurrentTimestamp();
std::string formatHashrate(double hashrate);
std::string bytesToHex(const std::vector<uint8_t>& bytes);
template<typename Iterator>
std::string bytesToHex(Iterator begin, Iterator end);
std::vector<uint8_t> hexToBytes(const std::string& hex);

// Utility functions for string formatting and printing
std::string formatThreadId(int threadId);
std::string formatRuntime(uint64_t seconds); 