#include "Utils.h"
#include "Config.h"
#include "Globals.h"  // This includes the global config variable
#include <sstream>
#include <iomanip>
#include <mutex>
#include <iostream>
#include <fstream>
#include <chrono>

std::mutex Utils::printMutex;

std::string Utils::bytesToHex(const std::vector<uint8_t>& bytes) {
    return bytesToHex(bytes.data(), bytes.size());
}

std::string Utils::bytesToHex(const uint8_t* bytes, size_t length) {
    std::stringstream ss;
    ss << std::hex << std::setfill('0');
    for (size_t i = 0; i < length; i++) {
        ss << std::setw(2) << static_cast<int>(bytes[i]);
    }
    return ss.str();
}

std::string Utils::formatHex(uint64_t value, int width) {
    std::stringstream ss;
    ss << std::hex << std::setw(width) << std::setfill('0') << value;
    return ss.str();
}

std::vector<uint8_t> Utils::hexToBytes(const std::string& hex) {
    std::vector<uint8_t> bytes;
    for (size_t i = 0; i < hex.length(); i += 2) {
        uint8_t byte = (Utils::hexCharToInt(hex[i]) << 4) | Utils::hexCharToInt(hex[i + 1]);
        bytes.push_back(byte);
    }
    return bytes;
}

std::string Utils::formatThreadId(int threadId) {
    std::stringstream ss;
    ss << "Thread-" << threadId;
    return ss.str();
}

std::string Utils::formatRuntime(uint64_t seconds) {
    uint64_t hours = seconds / 3600;
    uint64_t minutes = (seconds % 3600) / 60;
    seconds = seconds % 60;

    std::stringstream ss;
    if (hours > 0) {
        ss << hours << "h ";
    }
    if (minutes > 0 || hours > 0) {
        ss << minutes << "m ";
    }
    ss << seconds << "s";
    return ss.str();
}

std::string Utils::getCurrentTimestamp() {
    auto now = std::chrono::system_clock::now();
    auto now_c = std::chrono::system_clock::to_time_t(now);
    char buffer[26];
    ctime_s(buffer, sizeof(buffer), &now_c);
    std::string timestamp(buffer);
    timestamp = timestamp.substr(0, timestamp.length() - 1); // Remove newline
    return timestamp;
}

std::string Utils::formatHashrate(double hashrate) {
    std::stringstream ss;
    ss << std::fixed << std::setprecision(2);
    if (hashrate >= 1e9) {
        ss << hashrate / 1e9 << " GH/s";
    } else if (hashrate >= 1e6) {
        ss << hashrate / 1e6 << " MH/s";
    } else if (hashrate >= 1e3) {
        ss << hashrate / 1e3 << " KH/s";
    } else {
        ss << hashrate << " H/s";
    }
    return ss.str();
}

void Utils::initializeLogging(const std::string& filename) {
    std::lock_guard<std::mutex> lock(consoleMutex);
    if (logFile.is_open()) {
        logFile.close();
    }
    logFile.open(filename, std::ios::app);
    if (!logFile.is_open()) {
        std::cerr << "Failed to open log file: " << filename << std::endl;
    }
}

void Utils::cleanupLogging() {
    std::lock_guard<std::mutex> lock(consoleMutex);
    if (logFile.is_open()) {
        logFile.close();
    }
}

void Utils::threadSafePrint(const std::string& message, bool addNewline) {
    std::lock_guard<std::mutex> lock(printMutex);
    std::cout << message;
    if (addNewline) {
        std::cout << std::endl;
    }
    
    if (config.useLogFile) {
        std::lock_guard<std::mutex> logLock(logfileMutex);
        if (logFile.is_open()) {
            logFile << getCurrentTimestamp() << " " << message;
            if (addNewline) {
                logFile << std::endl;
            }
        }
    }
}

bool Utils::compareHashes(const uint8_t* hash1, const uint8_t* hash2, size_t length) {
    for (size_t i = 0; i < length; i++) {
        if (hash1[i] != hash2[i]) {
            return false;
        }
    }
    return true;
}

void Utils::reverseBytes(uint8_t* bytes, size_t length) {
    for (size_t i = 0; i < length / 2; i++) {
        std::swap(bytes[i], bytes[length - 1 - i]);
    }
}

uint64_t Utils::hashToUint64(const uint8_t* hash, int offset) {
    uint64_t result = 0;
    for (int i = 0; i < 8; i++) {
        result |= static_cast<uint64_t>(hash[offset + i]) << (i * 8);
    }
    return result;
}

uint8_t Utils::hexCharToInt(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return 0;
}

char Utils::intToHexChar(uint8_t value) {
    value &= 0xF;
    return value < 10 ? '0' + value : 'a' + (value - 10);
} 