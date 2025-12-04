#include "Utils.h"
#include <iostream>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <chrono>

std::mutex Utils::printMutex;
std::string Utils::logFileName = "monerominer.log";

std::string Utils::bytesToHex(const std::vector<uint8_t>& bytes) {
    std::stringstream ss;
    ss << std::hex << std::setfill('0');
    
    // Output bytes EXACTLY as they are - DO NOT REVERSE
    // RandomX outputs hashes in little-endian byte order already
    for (size_t i = 0; i < bytes.size(); i++) {
        ss << std::setw(2) << static_cast<unsigned int>(bytes[i]);
    }
    
    return ss.str();
}

std::vector<uint8_t> Utils::hexToBytes(const std::string& hex) {
    std::vector<uint8_t> bytes;
    for (size_t i = 0; i < hex.length(); i += 2) {
        std::string byteString = hex.substr(i, 2);
        uint8_t byte = static_cast<uint8_t>(strtol(byteString.c_str(), nullptr, 16));
        bytes.push_back(byte);
    }
    return bytes;
}

std::string Utils::bytesToHex(const uint8_t* data, size_t len) {
    std::stringstream ss;
    ss << std::hex << std::setfill('0');
    for (size_t i = 0; i < len; ++i) ss << std::setw(2) << static_cast<int>(data[i]);
    return ss.str();
}

std::string Utils::formatHex(uint64_t value, int width) {
    std::stringstream ss;
    ss << std::hex << std::setfill('0') << std::setw(width) << value;
    return ss.str();
}

std::string Utils::nonceToHex(uint32_t nonce) {
    std::stringstream ss;
    ss << std::hex << std::setfill('0');
    ss << std::setw(2) << ((nonce >> 0) & 0xFF);
    ss << std::setw(2) << ((nonce >> 8) & 0xFF);
    ss << std::setw(2) << ((nonce >> 16) & 0xFF);
    ss << std::setw(2) << ((nonce >> 24) & 0xFF);
    return ss.str();
}

std::string Utils::getCurrentTimestamp() {
    auto now = std::chrono::system_clock::now();
    auto time = std::chrono::system_clock::to_time_t(now);
    std::tm tm_buf;
#ifdef _WIN32
    localtime_s(&tm_buf, &time);
#else
    localtime_r(&time, &tm_buf);
#endif
    std::stringstream ss;
    ss << std::put_time(&tm_buf, "%Y-%m-%d %H:%M:%S");
    return ss.str();
}

void Utils::threadSafePrint(const std::string& message, bool toLog) {
    std::lock_guard<std::mutex> lock(printMutex);
    std::cout << message << std::endl;
    if (toLog) logToFile(message);
}

void Utils::logToFile(const std::string& message) {
    if (logFileName.empty()) return;
    std::ofstream file(logFileName, std::ios::app);
    if (file.is_open()) file << getCurrentTimestamp() << " " << message << std::endl;
}

void threadSafePrint(const std::string& message, bool toLog) {
    Utils::threadSafePrint(message, toLog);
}