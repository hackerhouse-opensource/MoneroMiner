#pragma once

#include <string>
#include <vector>
#include <cstdint>

// String conversion functions
std::string bytesToHex(const std::vector<uint8_t>& bytes);
template<typename Iterator>
std::string bytesToHex(Iterator begin, Iterator end);
std::vector<uint8_t> hexToBytes(const std::string& hex);

// Logging functions
std::string formatThreadId(int threadId);
std::string formatRuntime(uint64_t seconds);

// Timestamp and printing utilities
std::string getCurrentTimestamp();
void threadSafePrint(const std::string& message, bool debugOnly = false); 