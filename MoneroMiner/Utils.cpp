#include "Utils.h"
#include "Config.h"
#include <iostream>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <mutex>
#include <ctime>
#include <chrono>

// External references to globals defined in Globals.cpp
extern Config config;
static std::mutex printMutex;

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

std::string Utils::formatHex(uint32_t value, int width) {
    std::stringstream ss;
    ss << std::hex << std::setw(width) << std::setfill('0') << value;
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
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()) % 1000;
    
    std::tm tm;
    localtime_s(&tm, &time);
    
    std::stringstream ss;
    ss << std::put_time(&tm, "%d/%m/%Y (%H:%M:%S.")
       << std::setfill('0') << std::setw(3) << ms.count() << ") "
       << static_cast<uint32_t>(time) << ": ";  // Added colon and space
    
    return ss.str();
}

std::string Utils::getTimestamp() {
    return getCurrentTimestamp();
}

void Utils::threadSafePrint(const std::string& message, bool toLog, bool addTimestamp) {
    std::lock_guard<std::mutex> lock(printMutex);
    
    std::string output = addTimestamp ? (getTimestamp() + message) : message;
    
    // Print to console unless in headless mode
    if (!config.headlessMode) {
        std::cout << output;
        if (output.back() != '\n') {
            std::cout << std::endl;
        }
    }
    
    // Log to file if enabled
    if (toLog && config.useLogFile) {
        std::ofstream logFile(config.logFileName, std::ios::app);
        if (logFile.is_open()) {
            logFile << output;
            if (output.back() != '\n') {
                logFile << std::endl;
            }
            logFile.close();
        }
    }
}

void Utils::logToFile(const std::string& message) {
    if (config.useLogFile && !config.logFileName.empty()) {
        std::ofstream logFile(config.logFileName, std::ios::app);
        if (logFile.is_open()) {
            logFile << getTimestamp() << message << std::endl;
            logFile.close();
        }
    }
}

void Utils::setLogFile(const std::string& filename) {
    // This function now just updates the config
    config.logFileName = filename;
    config.useLogFile = true;
}

std::string Utils::formatHex(uint64_t value, int width) {
    std::stringstream ss;
    ss << std::hex << std::setw(width) << std::setfill('0') << value;
    return ss.str();
}

std::string Utils::formatHex(const uint8_t* data, size_t len) {
    std::stringstream ss;
    for (size_t i = 0; i < len; i++) {
        ss << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(data[i]);
    }
    return ss.str();
}