#include "Utils.h"
#include "Config.h"
#include "Platform.h"   // use platform abstraction instead of windows.h
#include <iostream>
#include <fstream>
#include <mutex>
#include <ctime>
#include <chrono>
#include <sstream>
#include <iomanip>
#include <cstring>

#ifdef PLATFORM_WINDOWS
    #include <windows.h>
#endif

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
    using namespace std::chrono;
    auto now = system_clock::now();
    auto ms_part = duration_cast<milliseconds>(now.time_since_epoch()) % 1000;
    time_t now_time = system_clock::to_time_t(now);

    struct tm tm;
#ifdef PLATFORM_WINDOWS
    localtime_s(&tm, &now_time);
#else
    localtime_r(&now_time, &tm);
#endif

    long long epoch_secs = duration_cast<seconds>(now.time_since_epoch()).count();
    char buf[128];
    // Format: MM/DD/YYYY (HH:MM:SS.mmm) <epoch>:
    snprintf(buf, sizeof(buf), "%02d/%02d/%04d (%02d:%02d:%02d.%03lld) %lld: ",
             tm.tm_mon + 1,
             tm.tm_mday,
             tm.tm_year + 1900,
             tm.tm_hour,
             tm.tm_min,
             tm.tm_sec,
             static_cast<long long>(ms_part.count()),
             epoch_secs);
    return std::string(buf);
}

std::string Utils::getTimestamp() {
    return getCurrentTimestamp();
}

void Utils::threadSafePrint(const std::string& message, bool toLog, bool addTimestamp) {
	std::lock_guard<std::mutex> lock(printMutex);

	std::string output = addTimestamp ? (getTimestamp() + message) : message;

	// Print to console unless in headless mode
	if (!config.headlessMode) {
#ifdef PLATFORM_WINDOWS
		// Always print to the console for visibility
		std::cout << output;
		if (!output.empty() && output.back() != '\n') std::cout << std::endl;

		// Also send debug output to the debugger when requested:
		// - caller passed true in `toLog` (many callsites used that for debug)
		// - or global debug mode is enabled
		if (toLog || config.debugMode) {
			// OutputDebugStringA expects a null-terminated C string
			OutputDebugStringA((output + "\n").c_str());
		}
#else
		// POSIX / Linux fallback: write to stdout as before
		std::cout << output;
		if (!output.empty() && output.back() != '\n') {
			std::cout << std::endl;
		}
#endif
	}

	// Log to file if enabled
	if (toLog && config.useLogFile) {
		std::ofstream logFile(config.logFileName, std::ios::app);
		if (logFile.is_open()) {
			logFile << output;
			if (!output.empty() && output.back() != '\n') {
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

bool Utils::enableLargePages() {
#ifdef PLATFORM_WINDOWS
    HANDLE hToken;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &hToken)) {
        return false;
    }

    TOKEN_PRIVILEGES tp;
    tp.PrivilegeCount = 1;
    tp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;

    if (!LookupPrivilegeValue(NULL, SE_LOCK_MEMORY_NAME, &tp.Privileges[0].Luid)) {
        CloseHandle(hToken);
        return false;
    }

    BOOL result = AdjustTokenPrivileges(hToken, FALSE, &tp, 0, NULL, NULL);
    DWORD error = GetLastError();
    CloseHandle(hToken);

    return (result != FALSE) && (error == ERROR_SUCCESS);
#else
    // Large pages handling is platform-specific; on Linux the user can configure hugepages via sysctl/mount.
    // Return false here to indicate we did not enable Windows-style large pages.
    (void)0;
    return false;
#endif
}

bool Utils::isRunningElevated() {
    // Delegate to Platform implementation which is cross-platform
    return Platform::isRunningElevated();
}

std::string Utils::getPrivilegeStatus() {
    std::stringstream ss;
    
    bool elevated = isRunningElevated();
    bool largePages = false;
    
    if (elevated) {
        largePages = enableLargePages();
    }
    
    ss << "Privileges: ";
    
    if (elevated && largePages) {
        ss << "Administrator (Large Pages ENABLED)";
    } else if (elevated && !largePages) {
        ss << "Administrator (Large Pages FAILED - check policy)";
    } else {
        ss << "Standard User (Large Pages DISABLED)\n";
        ss << "             Run as administrator for +10-30% performance boost";
    }
    
    // Add huge pages status with consistent spacing
    ss << "\nHuge pages: " << (Platform::hasHugePagesSupport() ? "enabled" : "unavailable");
    
    // Only show 1GB pages info on non-Windows platforms
#ifndef PLATFORM_WINDOWS
    if (Platform::has1GBPagesSupport()) {
        ss << "\n1GB pages: available";
    } else {
        // Check if CPU supports it
        std::ifstream cpuinfo("/proc/cpuinfo");
        std::string line;
        bool cpuSupports = false;
        while (std::getline(cpuinfo, line)) {
            if (line.find("pdpe1gb") != std::string::npos) {
                cpuSupports = true;
                break;
            }
        }
        if (cpuSupports) {
            ss << "\n1GB pages: supported but not configured";
        }
    }
#endif
    
    return ss.str();
}