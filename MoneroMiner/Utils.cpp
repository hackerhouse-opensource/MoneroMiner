#include "Utils.h"
#include <sstream>
#include <iomanip>
#include <mutex>
#include <iostream>
#include <fstream>
#include <chrono>

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
    return bytesToHex(bytes.begin(), bytes.end());
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

void threadSafePrint(const std::string& message, bool debugOnly) {
    if (debugOnly && !debugMode) {
        return;
    }
    std::lock_guard<std::mutex> lock(consoleMutex);
    std::cout << getCurrentTimestamp() << message << std::endl;
    if (logFile.is_open()) {
        std::lock_guard<std::mutex> logLock(logfileMutex);
        logFile << getCurrentTimestamp() << message << std::endl;
    }
} 