#include "Utils.h"
#include "Globals.h"
#include <sstream>
#include <iomanip>
#include <mutex>
#include <iostream>
#include <fstream>
#include <chrono>
#include "Types.h"

extern std::mutex consoleMutex;
extern std::mutex logfileMutex;
extern std::ofstream logFile;
extern bool debugMode;

// Static mutex for thread-safe printing
static std::mutex printMutex;

// Explicit template instantiations for bytesToHex
template std::string bytesToHex<std::vector<uint8_t>::iterator>(
    std::vector<uint8_t>::iterator begin,
    std::vector<uint8_t>::iterator end
);

template std::string bytesToHex<const uint8_t*>(
    const uint8_t* begin,
    const uint8_t* end
);

std::string bytesToHex(const std::vector<uint8_t>& bytes) {
    std::stringstream ss;
    ss << std::hex << std::setfill('0');
    for (const auto& byte : bytes) {
        ss << std::setw(2) << static_cast<int>(byte);
    }
    return ss.str();
}

template<typename Iterator>
std::string bytesToHex(Iterator begin, Iterator end) {
    std::stringstream ss;
    ss << std::hex << std::setfill('0');
    for (auto it = begin; it != end; ++it) {
        ss << std::setw(2) << static_cast<int>(*it);
    }
    return ss.str();
}

// Explicit template instantiations
template std::string bytesToHex<uint8_t*>(uint8_t*, uint8_t*);
template std::string bytesToHex<std::vector<uint8_t>::const_iterator>(std::vector<uint8_t>::const_iterator, std::vector<uint8_t>::const_iterator);

std::vector<uint8_t> hexToBytes(const std::string& hex) {
    std::vector<uint8_t> bytes;
    for (size_t i = 0; i < hex.length(); i += 2) {
        std::string byteString = hex.substr(i, 2);
        uint8_t byte = static_cast<uint8_t>(std::stoi(byteString, nullptr, 16));
        bytes.push_back(byte);
    }
    return bytes;
}

std::string formatThreadId(int threadId) {
    std::stringstream ss;
    ss << "Thread-" << threadId;
    return ss.str();
}

std::string formatRuntime(uint64_t seconds) {
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

std::string getCurrentTimestamp() {
    auto now = std::chrono::system_clock::now();
    auto now_c = std::chrono::system_clock::to_time_t(now);
    std::stringstream ss;
    struct tm timeinfo;
    localtime_s(&timeinfo, &now_c);
    ss << std::put_time(&timeinfo, "[%Y-%m-%d %H:%M:%S] ");
    return ss.str();
}

void threadSafePrint(const std::string& message, bool addNewline) {
    std::lock_guard<std::mutex> lock(consoleMutex);
    std::cout << message;
    if (addNewline) std::cout << std::endl;
    if (logFile.is_open()) {
        logFile << message;
        if (addNewline) logFile << std::endl;
    }
}

std::string formatHashrate(double hashrate) {
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

void initializeLogging(const std::string& filename) {
    std::lock_guard<std::mutex> lock(consoleMutex);
    if (logFile.is_open()) {
        logFile.close();
    }
    logFile.open(filename, std::ios::app);
    if (!logFile.is_open()) {
        std::cerr << "Failed to open log file: " << filename << std::endl;
    }
}

void cleanupLogging() {
    std::lock_guard<std::mutex> lock(consoleMutex);
    if (logFile.is_open()) {
        logFile.close();
    }
} 