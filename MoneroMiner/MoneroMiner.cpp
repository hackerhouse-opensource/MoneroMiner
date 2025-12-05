/**
 * MoneroMiner.cpp - Lightweight High Performance Monero (XMR) CPU Miner
 * 
 * Implementation file containing the core mining functionality.
 * 
 * Author: Hacker Fantastic (https://hacker.house)
 * License: Attribution-NonCommercial-NoDerivatives 4.0 International
 * https://creativecommons.org/licenses/by-nc-nd/4.0/
 */
 
#include "Config.h"
#include "PoolClient.h"
#include "RandomXManager.h"
#include "MiningStats.h"
#include "Utils.h"
#include "Job.h"
#include "Globals.h"
#include <iostream>
#include <thread>
#include <vector>
#include <mutex>
#include <condition_variable>
#include <csignal>
#include <chrono>
#include <iomanip>
#include <sstream>
#include <fstream>
#include <unordered_set>
#include <array> // Add for 256-bit target
#include <windows.h> // Add for headless mode
#include <intrin.h>
#include <sysinfoapi.h>
#include <powerbase.h>
#pragma comment(lib, "PowrProf.lib")

// Global variable declarations (not definitions)
extern std::atomic<uint32_t> activeJobId;
extern std::atomic<uint32_t> notifiedJobId;
extern std::atomic<bool> newJobAvailable;
extern std::atomic<uint64_t> acceptedShares;
extern std::atomic<uint64_t> rejectedShares;
extern std::atomic<uint64_t> jsonRpcId;
extern std::string sessionId;
extern std::vector<MiningThreadData*> threadData;

// Global variables
std::vector<std::thread> miningThreads;
std::thread jobListenerThread;

// Forward declarations
void printHelp();
bool validateConfig();
void signalHandler(int signum);
void printConfig();
void miningThread(MiningThreadData* data);
bool loadConfig();
bool startMining();

// Utility: Convert difficulty to 256-bit target (big-endian)
std::array<uint8_t, 32> difficultyToTarget(uint64_t difficulty) {
    std::array<uint8_t, 32> target{};
    if (difficulty == 0) {
        // Set target to max (all 0xFF)
        target.fill(0xFF);
        return target;
    }
    // Calculate: target = (2^256 - 1) / difficulty
    // We'll use manual 256-bit division
    // 2^256 - 1 = all bytes 0xFF
    // We'll use a reference implementation for this calculation
    // For Monero, pools send target as a 32-byte hex string, but we need to calculate it if only difficulty is given

    // Use reference: xmrig's implementation
    // target = (uint256_t(1) << 256) / difficulty;
    // We'll use a simple approach for now:
    // Only support up to 64-bit difficulty, so high bytes are zero
    uint64_t quotient = ~0ULL / difficulty;
    // Place quotient in the last 8 bytes (big-endian)
    for (int i = 0; i < 8; ++i) {
        target[24 + i] = (quotient >> (8 * (7 - i))) & 0xFF;
    }
    // The rest are zero
    return target;
}

// Utility: Print 256-bit value as hex
std::string print256Hex(const uint8_t* bytes) {
    std::stringstream ss;
    for (int i = 0; i < 32; ++i) {
        ss << std::hex << std::setw(2) << std::setfill('0') << (int)bytes[i];
    }
    return ss.str();
}

// Utility: Compare 256-bit big-endian values
bool meetsTarget256(const uint8_t* hash, const uint8_t* target) {
    for (int i = 0; i < 32; ++i) {
        if (hash[i] < target[i]) return true;
        if (hash[i] > target[i]) return false;
    }
    return true; // hash == target
}

// New function: Get detailed CPU information
std::string getCPUBrandString() {
    int cpuInfo[4] = {0};
    char cpuBrand[0x40] = {0};
    
    __cpuid(cpuInfo, 0x80000002);
    memcpy(cpuBrand, cpuInfo, sizeof(cpuInfo));
    __cpuid(cpuInfo, 0x80000003);
    memcpy(cpuBrand + 16, cpuInfo, sizeof(cpuInfo));
    __cpuid(cpuInfo, 0x80000004);
    memcpy(cpuBrand + 32, cpuInfo, sizeof(cpuInfo));
    
    return std::string(cpuBrand);
}

// New function: Check CPU features
std::string getCPUFeatures() {
    int cpuInfo[4] = {0};
    __cpuid(cpuInfo, 1);
    
    bool aes = (cpuInfo[2] & (1 << 25)) != 0;
    bool avx = (cpuInfo[2] & (1 << 28)) != 0;
    
    __cpuid(cpuInfo, 7);
    bool avx2 = (cpuInfo[1] & (1 << 5)) != 0;
    
    std::string features;
    if (aes) features += " AES";
    if (avx) features += " AVX";
    if (avx2) features += " AVX2";
    features += " VM"; // RandomX VM support
    
    return features;
}

// Fixed: Check huge pages support properly
std::string getHugePagesInfo() {
    HANDLE token;
    if (OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) {
        LUID luid;
        if (LookupPrivilegeValue(NULL, SE_LOCK_MEMORY_NAME, &luid)) {
            PRIVILEGE_SET ps;
            ps.PrivilegeCount = 1;
            ps.Control = PRIVILEGE_SET_ALL_NECESSARY;
            ps.Privilege[0].Luid = luid;
            ps.Privilege[0].Attributes = SE_PRIVILEGE_ENABLED;
            
            BOOL result = FALSE;
            if (PrivilegeCheck(token, &ps, &result) && result) {
                CloseHandle(token);
                return "permission granted";
            }
        }
        CloseHandle(token);
    }
    return "unavailable";
}

// New function: Get memory info including DIMM details
void printMemoryInfo() {
    MEMORYSTATUSEX memInfo;
    memInfo.dwLength = sizeof(MEMORYSTATUSEX);
    GlobalMemoryStatusEx(&memInfo);
    
    double usedGB = (memInfo.ullTotalPhys - memInfo.ullAvailPhys) / (1024.0 * 1024.0 * 1024.0);
    double totalGB = memInfo.ullTotalPhys / (1024.0 * 1024.0 * 1024.0);
    int usage = memInfo.dwMemoryLoad;
    
    std::cout << " * MEMORY       " 
              << std::fixed << std::setprecision(1) << usedGB << "/"
              << totalGB << " GB (" << usage << "%)" << std::endl;
    
    // Try to get DIMM information via WMI (basic implementation)
    // Note: Full DIMM detection requires WMI COM calls which is complex
    // For now, show simplified info
    std::cout << "                (DIMM details require WMI - see Task Manager for full info)" << std::endl;
}

// New function: Get motherboard info
void printMotherboardInfo() {
    std::cout << " * MOTHERBOARD  ";
    
    // Try to read from registry (Windows stores some info here)
    HKEY hKey;
    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, 
                      "HARDWARE\\DESCRIPTION\\System\\BIOS", 
                      0, KEY_READ, &hKey) == ERROR_SUCCESS) {
        char manufacturer[256] = {0};
        char product[256] = {0};
        DWORD size = sizeof(manufacturer);
        
        RegQueryValueExA(hKey, "SystemManufacturer", NULL, NULL, (LPBYTE)manufacturer, &size);
        size = sizeof(product);
        RegQueryValueExA(hKey, "SystemProductName", NULL, NULL, (LPBYTE)product, &size);
        
        std::cout << manufacturer << " - " << product << std::endl;
        RegCloseKey(hKey);
    } else {
        std::cout << "Unknown (registry access failed)" << std::endl;
    }
}

// Simplified: Print detailed system information (clean format with lowercase labels)
void printDetailedSystemInfo() {
    SYSTEM_INFO sysInfo;
    GetSystemInfo(&sysInfo);
    
    std::string cpuBrand = getCPUBrandString();
    std::string cpuFeatures = getCPUFeatures();
    
    // Trim CPU brand string
    size_t start = cpuBrand.find_first_not_of(" \t");
    size_t end = cpuBrand.find_last_not_of(" \t");
    if (start != std::string::npos && end != std::string::npos) {
        cpuBrand = cpuBrand.substr(start, end - start + 1);
    }
    
    bool is64bit = sizeof(void*) == 8;
    
    // CPU - dynamic detection
    std::cout << "CPU:          " << cpuBrand << " (1) " 
              << (is64bit ? "64-bit" : "32-bit") << cpuFeatures << std::endl;
    std::cout << "              " << sysInfo.dwNumberOfProcessors << " threads" << std::endl;
    
    // Memory - dynamic calculation
    MEMORYSTATUSEX memInfo;
    memInfo.dwLength = sizeof(MEMORYSTATUSEX);
    GlobalMemoryStatusEx(&memInfo);
    
    double usedGB = (memInfo.ullTotalPhys - memInfo.ullAvailPhys) / (1024.0 * 1024.0 * 1024.0);
    double totalGB = memInfo.ullTotalPhys / (1024.0 * 1024.0 * 1024.0);
    int usage = memInfo.dwMemoryLoad;
    
    std::cout << "Memory:       " 
              << std::fixed << std::setprecision(1) << usedGB << "/"
              << totalGB << " GB (" << usage << "%)" << std::endl;
    
    // Motherboard - dynamic registry read
    std::cout << "Motherboard:  ";
    HKEY hKey;
    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, 
                      "HARDWARE\\DESCRIPTION\\System\\BIOS", 
                      0, KEY_READ, &hKey) == ERROR_SUCCESS) {
        char manufacturer[256] = {0};
        char product[256] = {0};
        DWORD size = sizeof(manufacturer);
        
        RegQueryValueExA(hKey, "SystemManufacturer", NULL, NULL, (LPBYTE)manufacturer, &size);
        size = sizeof(product);
        RegQueryValueExA(hKey, "SystemProductName", NULL, NULL, (LPBYTE)product, &size);
        
        std::cout << manufacturer << " - " << product << std::endl;
        RegCloseKey(hKey);
    } else {
        std::cout << "Unknown" << std::endl;
    }
    
    // Threads - from config
    std::cout << "Threads:      " << config.numThreads << std::endl;
    
    // Algorithm
    std::cout << "Algorithm:    RandomX (rx/0)" << std::endl;
}

void printHelp() {
    std::cout << "MoneroMiner - A Monero (XMR) mining program\n\n"
              << "Usage: MoneroMiner [options]\n\n"
              << "Options:\n"
              << "  --help               Show this help message\n"
              << "  --debug              Enable debug output\n"
              << "  --logfile            Enable logging to file\n"
              << "  --threads N          Number of mining threads (default: 1)\n"
              << "  --pool ADDRESS:PORT  Pool address and port (default: xmr-eu1.nanopool.org:14444)\n"
              << "  --wallet ADDRESS      Your Monero wallet address\n"
              << "  --worker NAME        Worker name (default: worker1)\n"
              << "  --password X         Pool password (default: x)\n"
              << "  --useragent AGENT    User agent string (default: MoneroMiner/1.0.0)\n\n"
              << "Example:\n"
              << "  MoneroMiner --debug --logfile --threads 4 --wallet YOUR_WALLET_ADDRESS\n"
              << std::endl;
}

bool validateConfig() {
    if (config.walletAddress.empty()) {
        Utils::threadSafePrint("Error: Wallet address is required\n", false);
        return false;
    }
    if (config.numThreads <= 0) {
        config.numThreads = std::thread::hardware_concurrency();
        if (config.numThreads == 0) {
            config.numThreads = 4;
        }
        Utils::threadSafePrint("Using " + std::to_string(config.numThreads) + " threads", false);
    }
    return true;
}

void signalHandler(int signum) {
    Utils::threadSafePrint("Received signal " + std::to_string(signum) + ", shutting down...", false);
    shouldStop = true;
}

void printConfig() {
    std::cout << "Current Configuration:" << std::endl;
    std::cout << "Pool Address: " << config.poolAddress << ":" << config.poolPort << std::endl;
    std::cout << "Wallet: " << config.walletAddress << std::endl;
    std::cout << "Worker Name: " << config.workerName << std::endl;
    std::cout << "User Agent: " << config.userAgent << std::endl;
    std::cout << "Threads: " << config.numThreads << std::endl;
    std::cout << "Debug Mode: " << (config.debugMode ? "Yes" : "No") << std::endl;
    std::cout << "Logfile: " << (config.useLogFile ? config.logFileName : "Disabled") << std::endl;
    std::cout << std::endl;
}

static std::mutex foundNonceMutex;
static std::unordered_set<uint32_t> submittedNonces;
static std::string currentMiningJobId;

void miningThread(MiningThreadData* data) {
    try {
        if (!data || !data->initializeVM()) {
            Utils::threadSafePrint("Thread " + std::to_string(data->getThreadId()) + " init failed", true);
            return;
        }

        // CRITICAL FIX: Calculate UNIQUE nonce range for this thread
        uint32_t totalThreads = static_cast<uint32_t>(config.numThreads);
        uint64_t totalNonceSpace = 0x100000000ULL;
        uint64_t nonceRangePerThread = totalNonceSpace / totalThreads;
        
        uint32_t startNonce = static_cast<uint32_t>(data->getThreadId() * nonceRangePerThread);
        uint32_t endNonce = (data->getThreadId() == static_cast<int>(totalThreads) - 1) 
            ? 0xFFFFFFFF 
            : static_cast<uint32_t>((data->getThreadId() + 1) * nonceRangePerThread - 1);
        
        uint32_t localNonce = startNonce;
        std::string lastJobId;
        auto lastHashrateUpdate = std::chrono::steady_clock::now();
        uint64_t hashesInPeriod = 0;
        uint64_t hashesTotal = 0;
        std::vector<uint8_t> workingBlob;
        workingBlob.reserve(128);
        std::vector<uint8_t> hashResult(32);
        uint64_t debugHashCounter = 0;

        if (config.debugMode) {
            std::string msg = "[T" + std::to_string(data->getThreadId()) + "] Started | Nonce range: 0x" + Utils::formatHex(localNonce, 8) + " - 0x" + Utils::formatHex(endNonce, 8) + "\n";
            Utils::threadSafePrint(msg, true);
        }

        if (config.debugMode) {
            std::stringstream ss;
            ss << "[T" << data->getThreadId() << "] Unique nonce range: 0x" 
               << std::hex << std::setw(8) << std::setfill('0') << startNonce 
               << " - 0x" << endNonce;
            Utils::threadSafePrint(ss.str(), true);
        }

        while (!shouldStop) {
            try {
                Job jobCopy;
                std::string currentJobId;
                
                {
                    std::lock_guard<std::mutex> lock(PoolClient::jobMutex);
                    if (PoolClient::jobQueue.empty()) {
                        std::this_thread::sleep_for(std::chrono::milliseconds(100));
                        continue;
                    }
                    
                    jobCopy = PoolClient::jobQueue.front();
                    currentJobId = jobCopy.getJobId();
                    
                    // Clear submitted nonces when job changes
                    if (currentMiningJobId != currentJobId) {
                        std::lock_guard<std::mutex> nonceLock(foundNonceMutex);
                        submittedNonces.clear();
                        currentMiningJobId = currentJobId;
                    }
                }
                
                if (currentJobId != lastJobId) {
                    if (config.debugMode) {
                        std::stringstream ss;
                        ss << "[T" << data->getThreadId() << "] [JOB] " << currentJobId 
                           << " | H:" << jobCopy.height << " | D:" << jobCopy.difficulty 
                           << " | Hashes:" << hashesTotal << "\n"
                           << "Target: " << jobCopy.getTarget();
                        Utils::threadSafePrint(ss.str(), true);
                    }
                    
                    lastJobId = currentJobId;
                    localNonce = startNonce;
                    hashesInPeriod = 0;
                    hashesTotal = 0;
                    debugHashCounter = 0;
                    lastHashrateUpdate = std::chrono::steady_clock::now();
                    
                    continue;
                }

                if (localNonce > endNonce) {
                    // This thread exhausted its range, wait for new job
                    std::this_thread::sleep_for(std::chrono::milliseconds(100));
                    continue;
                }

                workingBlob = jobCopy.getBlobBytes();
                if (workingBlob.empty() || workingBlob.size() < 76) {
                    if (config.debugMode) {
                        Utils::threadSafePrint("[T" + std::to_string(data->getThreadId()) + "] FATAL: Blob too short", true);
                    }
                    std::this_thread::sleep_for(std::chrono::milliseconds(100));
                    continue;
                }

                if (jobCopy.nonceOffset + 4 > workingBlob.size()) {
                    if (config.debugMode) {
                        Utils::threadSafePrint("[T" + std::to_string(data->getThreadId()) + "] FATAL: Invalid nonce offset", true);
                    }
                    std::this_thread::sleep_for(std::chrono::milliseconds(100));
                    continue;
                }

                // CRITICAL: Insert nonce as LITTLE-ENDIAN 32-bit value
                /*
                 * Nonce Insertion into Mining Blob
                 * =================================
                 * 
                 * The nonce is a 4-byte (32-bit) little-endian value at byte 39-42:
                 * 
                 * Example nonce = 0x12345678:
                 *   byte[39] = 0x78  (least significant byte)
                 *   byte[40] = 0x56
                 *   byte[41] = 0x34
                 *   byte[42] = 0x12  (most significant byte)
                 * 
                 * This allows miners to search 2^32 (4.3 billion) nonce values
                 * per job before wrapping around or getting a new job.
                 */
                uint32_t nonce32 = static_cast<uint32_t>(localNonce & 0xFFFFFFFFULL);
                size_t offset = jobCopy.nonceOffset;
                workingBlob[offset + 0] = static_cast<uint8_t>((nonce32 >>  0) & 0xFF);
                workingBlob[offset + 1] = static_cast<uint8_t>((nonce32 >>  8) & 0xFF);
                workingBlob[offset + 2] = static_cast<uint8_t>((nonce32 >> 16) & 0xFF);
                workingBlob[offset + 3] = static_cast<uint8_t>((nonce32 >> 24) & 0xFF);

                // Hash calculation
                bool hashOk = false;
                {
                    // Defensive: zero hashResult before calculation
                    std::fill(hashResult.begin(), hashResult.end(), 0);
                    
                    // Convert targetHash (4x uint64_t) to bytes (32 bytes) for calculation
                    std::vector<uint8_t> targetBytes(32, 0);
                    for (int wordIdx = 0; wordIdx < 4; wordIdx++) {
                        uint64_t word = jobCopy.targetHash[wordIdx];
                        for (int byteIdx = 0; byteIdx < 8; byteIdx++) {
                            targetBytes[wordIdx * 8 + byteIdx] = static_cast<uint8_t>((word >> (byteIdx * 8)) & 0xFF);
                        }
                    }
                    
                    hashOk = data->calculateHashAndCheckTarget(workingBlob, targetBytes, hashResult);
                }

                debugHashCounter++;
                
                // Enhanced debug output every 10,000 hashes with detailed byte comparison
                if (config.debugMode && (debugHashCounter % 10000 == 0)) {
                    std::stringstream ss;
                    ss << "[T" << data->getThreadId() << "] Hash #" << debugHashCounter 
                       << " | Nonce: 0x" << std::hex << std::setw(8) << std::setfill('0') << nonce32 << "\n";
                    
                    ss << "  Hash (LE):   ";
                    for (size_t i = 0; i < 32; i++) {
                        ss << std::hex << std::setw(2) << std::setfill('0') << (int)hashResult[i];
                        if (i == 7 || i == 15 || i == 23) ss << " ";
                    }
                    
                    ss << "\n  Target (LE): ";
                    // Convert targetHash to bytes for display
                    for (int wordIdx = 0; wordIdx < 4; wordIdx++) {
                        uint64_t word = jobCopy.targetHash[wordIdx];
                        for (int byteIdx = 0; byteIdx < 8; byteIdx++) {
                            uint8_t byte = static_cast<uint8_t>((word >> (byteIdx * 8)) & 0xFF);
                            ss << std::hex << std::setw(2) << std::setfill('0') << (int)byte;
                            if ((wordIdx * 8 + byteIdx == 7) || (wordIdx * 8 + byteIdx == 15) || (wordIdx * 8 + byteIdx == 23)) ss << " ";
                        }
                    }
                    
                    // Detailed byte-by-byte comparison (first 8 bytes)
                    ss << "\n  Byte-by-byte comparison (LE order):";
                    bool hashStillValid = true;
                    for (size_t i = 0; i < 8; i++) {
                        uint8_t targetByte = static_cast<uint8_t>((jobCopy.targetHash[0] >> (i * 8)) & 0xFF);
                        ss << "\n    Byte[" << i << "]: Hash=0x" 
                           << std::hex << std::setw(2) << std::setfill('0') << (int)hashResult[i]
                           << " vs Target=0x" 
                           << std::hex << std::setw(2) << std::setfill('0') << (int)targetByte;
                        
                        if (hashStillValid) {
                            if (hashResult[i] < targetByte) {
                                ss << " [PASS - hash byte is lower]";
                                hashStillValid = false;
                            } else if (hashResult[i] > targetByte) {
                                ss << " [FAIL - hash byte is higher]";
                                hashStillValid = false;
                            } else {
                                ss << " [EQUAL - continue to next byte]";
                            }
                        }
                    }
                    
                    ss << "\n  Result: " << (hashOk ? "VALID SHARE" : "Does not meet target");
                    ss << "\n  Expected shares so far: " << std::fixed << std::setprecision(3) 
                       << (static_cast<double>(hashesTotal) / static_cast<double>(jobCopy.difficulty));
                    
                    bool isAllZeros = std::all_of(hashResult.begin(), hashResult.end(), [](uint8_t b){ return b == 0; });
                    if (isAllZeros) {
                        ss << "\n  [WARNING: Hash is all zeros - VM calculation error!]";
                    }
                    
                    Utils::threadSafePrint(ss.str(), true);
                }

                // Check for valid share
                bool isAllZeros = std::all_of(hashResult.begin(), hashResult.end(), [](uint8_t b){ return b == 0; });
                if (hashOk && !isAllZeros) {
                    // CRITICAL: Check for duplicate submission BEFORE doing anything
                    bool shouldSubmit = false;
                    {
                        std::lock_guard<std::mutex> lock(foundNonceMutex);
                        if (submittedNonces.find(nonce32) == submittedNonces.end()) {
                            submittedNonces.insert(nonce32);
                            shouldSubmit = true;
                            
                            // Limit memory
                            if (submittedNonces.size() > 10000) {
                                submittedNonces.clear();
                                submittedNonces.insert(nonce32);
                            }
                        }
                    }
                    
                    if (!shouldSubmit) {
                        localNonce++;
                        continue; // Skip - already submitted
                    }
                    
                    // Double-check job still current
                    {
                        std::lock_guard<std::mutex> lock(PoolClient::jobMutex);
                        if (PoolClient::jobQueue.empty() || 
                            PoolClient::jobQueue.front().getJobId() != currentJobId) {
                            if (config.debugMode) {
                                Utils::threadSafePrint("[T" + std::to_string(data->getThreadId()) + 
                                                     "] Discarding stale share", true);
                            }
                            localNonce++;
                            continue;
                        }
                    }
                    
                    // CRITICAL FIX: Format nonce as 8-character hex (little-endian bytes)
                    std::stringstream nonceStream;
                    nonceStream << std::hex << std::setw(2) << std::setfill('0') << (int)workingBlob[offset + 0];
                    nonceStream << std::hex << std::setw(2) << std::setfill('0') << (int)workingBlob[offset + 1];
                    nonceStream << std::hex << std::setw(2) << std::setfill('0') << (int)workingBlob[offset + 2];
                    nonceStream << std::hex << std::setw(2) << std::setfill('0') << (int)workingBlob[offset + 3];
                    std::string nonceHex = nonceStream.str();
                    
                    // CRITICAL FIX: Send hash in LITTLE-ENDIAN byte order (as calculated by RandomX)
                    std::stringstream hashStream;
                    for (int i = 0; i < 32; i++) {
                        hashStream << std::hex << std::setw(2) << std::setfill('0') << (int)hashResult[i];
                    }
                    std::string hashHex = hashStream.str();

                    // Ultra-condensed single line format
                    Utils::threadSafePrint("J: " + currentJobId + " Nonce: " + nonceHex + " Hash: " + hashHex + " (" + std::to_string(hashesTotal) + " attempts)", true);
                    
                    if (config.debugMode) {
                        Utils::threadSafePrint("  Blob with nonce (first 50 bytes): ", true);
                        for (size_t i = 0; i < 50 && i < workingBlob.size(); i++) {
                            std::cout << std::hex << std::setw(2) << std::setfill('0') << (int)workingBlob[i];
                        }
                        std::cout << std::dec << std::endl;
                    }

                    // Submit share - PoolClient handles everything including response parsing
                    PoolClient::submitShare(currentJobId, nonceHex, hashHex, "rx/0");
                    // NOTE: Accept/reject counters are incremented by PoolClient::processShareResponse
                }

                hashesInPeriod++;
                hashesTotal++;
                localNonce++; // Move to next nonce in this thread's range
                
                auto now = std::chrono::steady_clock::now();
                auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - lastHashrateUpdate).count();
                if (elapsed >= 5 && hashesInPeriod > 0) {
                    double hashrate = static_cast<double>(hashesInPeriod) / static_cast<double>(elapsed);
                    data->setHashrate(hashrate);
                    
                    if (config.debugMode) {
                        Utils::threadSafePrint("[T" + std::to_string(data->getThreadId()) + "] Hashrate: " + 
                            std::to_string(static_cast<int>(hashrate)) + " H/s | Total: " + std::to_string(hashesTotal), true);
                    }
                    
                    lastHashrateUpdate = now;
                    hashesInPeriod = 0;
                }
                
                if ((localNonce & 0xFF) == 0) {
                    std::this_thread::yield();
                }
            }
            catch (const std::exception& e) {
                Utils::threadSafePrint("Thread error: " + std::string(e.what()), true);
                std::this_thread::sleep_for(std::chrono::seconds(1));
            }
        }
    }
    catch (const std::exception& e) {
        Utils::threadSafePrint("Fatal thread error: " + std::string(e.what()), true);
    }
}

void processNewJob(const picojson::object& jobObj) {
    try {
        // Extract job details
        std::string jobId = jobObj.at("job_id").get<std::string>();
        std::string blob = jobObj.at("blob").get<std::string>();
        std::string target = jobObj.at("target").get<std::string>();
        uint64_t height = static_cast<uint64_t>(jobObj.at("height").get<double>());
        std::string seedHash = jobObj.at("seed_hash").get<std::string>();

        Job job(blob, jobId, target, height, seedHash);

        // Set target in RandomXManager
        if (!RandomXManager::setTargetAndDifficulty(target)) {
            Utils::threadSafePrint("Failed to set target", true);
            return;
        }

        // FIX C4244 WARNING - Explicit intermediate variable
        uint32_t jobIdNum = 0;
        try {
            size_t pos = 0;
            uint32_t temp = static_cast<uint32_t>(std::stoul(jobId, &pos, 10));
            jobIdNum = temp;
        } catch (const std::exception&) {
            jobIdNum = static_cast<uint32_t>(std::hash<std::string>{}(jobId));
        }
        
        if (jobIdNum != activeJobId.load()) {
            activeJobId.store(jobIdNum);
            notifiedJobId.store(jobIdNum);

            if (!RandomXManager::initialize(seedHash)) {
                Utils::threadSafePrint("Failed to initialize RandomX with seed hash: " + seedHash, true);
                return;
            }

            {
                std::lock_guard<std::mutex> lock(PoolClient::jobMutex);
                while (!PoolClient::jobQueue.empty()) {
                    PoolClient::jobQueue.pop();
                }
                PoolClient::jobQueue.push(job);
            }

            if (!config.debugMode) {
                Utils::threadSafePrint("New job: " + jobId + " | Height: " + std::to_string(height), false);
            } else {
                Utils::threadSafePrint("New job details:", true);
                Utils::threadSafePrint("  Height: " + std::to_string(height), true);
                Utils::threadSafePrint("  Job ID: " + jobId, true);
                Utils::threadSafePrint("  Target: 0x" + target, true);
                Utils::threadSafePrint("  Difficulty: " + std::to_string(job.difficulty), true);
            }

            PoolClient::jobAvailable.notify_all();
            PoolClient::jobQueueCondition.notify_all();
        }
    }
    catch (const std::exception& e) {
        Utils::threadSafePrint("Error processing job: " + std::string(e.what()), true);
    }
    catch (...) {
        Utils::threadSafePrint("Unknown error processing job", true);
    }
}

bool loadConfig() {
    try {
        std::ifstream file("config.json");
        if (file.is_open()) {
            picojson::value v;
            std::string err = picojson::parse(v, file);
            if (err.empty() && v.is<picojson::object>()) {
                const picojson::object& obj = v.get<picojson::object>();
                
                if (obj.find("poolAddress") != obj.end()) {
                    config.poolAddress = obj.at("poolAddress").get<std::string>();
                }
                if (obj.find("poolPort") != obj.end()) {
                    config.poolPort = static_cast<int>(obj.at("poolPort").get<double>());
                }
                if (obj.find("walletAddress") != obj.end()) {
                    config.walletAddress = obj.at("walletAddress").get<std::string>();
                }
                if (obj.find("workerName") != obj.end()) {
                    config.workerName = obj.at("workerName").get<std::string>();
                }
                if (obj.find("password") != obj.end()) {
                    config.password = obj.at("password").get<std::string>();
                }
                if (obj.find("userAgent") != obj.end()) {
                    config.userAgent = obj.at("userAgent").get<std::string>();
                }
                if (obj.find("numThreads") != obj.end()) {
                    config.numThreads = static_cast<int>(obj.at("numThreads").get<double>());
                }
                if (obj.find("debugMode") != obj.end()) {
                    config.debugMode = obj.at("debugMode").get<bool>();
                }
                if (obj.find("useLogFile") != obj.end()) {
                    config.useLogFile = obj.at("useLogFile").get<bool>();
                }
                if (obj.find("logFileName") != obj.end()) {
                    config.logFileName = obj.at("logFileName").get<std::string>();
                }
                
                file.close();
                return true;
            }
            file.close();
        }
        return true;
    }
    catch (const std::exception& e) {
        Utils::threadSafePrint("Error loading config: " + std::string(e.what()), true);
        return false;
    }
}

bool startMining() {
    // Initialize network first
    if (!PoolClient::initialize()) {
        return false;
    }
    
    if (!PoolClient::connect()) {
        return false;
    }
    
    if (!PoolClient::login(config.walletAddress, config.password, 
                          config.workerName, config.userAgent)) {
        return false;
    }
    
    // Start job listener thread IMMEDIATELY after login (uses same socket)
    jobListenerThread = std::thread(PoolClient::jobListener);
    
    // Wait for first job
    {
        std::unique_lock<std::mutex> lock(PoolClient::jobMutex);
        PoolClient::jobAvailable.wait_for(lock, std::chrono::seconds(10), 
            [] { return !PoolClient::jobQueue.empty() || shouldStop; });
    }

    if (shouldStop) {
        return false;
    }
    
    // Initialize RandomX before creating threads
    Job* currentJob = nullptr;
    {
        std::lock_guard<std::mutex> lock(PoolClient::jobMutex);
        if (!PoolClient::jobQueue.empty()) {
            currentJob = &PoolClient::jobQueue.front();
        }
    }

    if (!currentJob) {
        Utils::threadSafePrint("No job available for RandomX initialization", true);
        return false;
    }

    if (!RandomXManager::initialize(currentJob->seedHash)) {
        Utils::threadSafePrint("Failed to initialize RandomX", true);
        return false;
    }

    // Initialize thread data
    threadData.resize(static_cast<size_t>(config.numThreads));
    for (size_t i = 0; i < static_cast<size_t>(config.numThreads); i++) {  // Fix: use size_t
        threadData[i] = new MiningThreadData(static_cast<int>(i));
        if (!threadData[i]->initializeVM()) {
            Utils::threadSafePrint("Failed to initialize VM for thread " + std::to_string(i), true);
            return false;
        }
        if (config.debugMode && i < 4) {
            Utils::threadSafePrint("VM ready for thread " + std::to_string(i), true);
        }
    }
    if (!config.debugMode) {
        Utils::threadSafePrint("Initialized " + std::to_string(config.numThreads) + " mining threads", true);
    }
    
    // Start mining threads
    for (size_t i = 0; i < static_cast<size_t>(config.numThreads); i++) {  // Fix: use size_t
        miningThreads.emplace_back(miningThread, threadData[i]);
        if (config.debugMode) {
            Utils::threadSafePrint("Started mining thread " + std::to_string(i), true);
        }
    }
    
    if (!config.debugMode) {
        Utils::threadSafePrint("Mining started - Press Ctrl+C to stop", true);
    } else {
        Utils::threadSafePrint("=== MINING STARTED WITH " + std::to_string(config.numThreads) + " THREADS ===", true);
        Utils::threadSafePrint("Press Ctrl+C to stop mining", true);
    }
    
    return true;
}

int main(int argc, char* argv[]) {
    // Check for --help first
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            printHelp();
            return 0;
        }
    }

    // Initialize Winsock
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        std::cerr << "Failed to initialize Winsock" << std::endl;
        return 1;
    }

    // Load configuration FIRST
    if (!loadConfig()) {
        std::cerr << "Failed to load configuration" << std::endl;
        WSACleanup();
        return 1;
    }

    // THEN parse command line arguments (so they override config file)
    if (!config.parseCommandLine(argc, argv)) {
        WSACleanup();
        return 0;  // --help was shown
    }
    
    // HEADLESS MODE: Hide console window
    if (config.headlessMode) {
        FreeConsole();
        Utils::threadSafePrint("=== HEADLESS MODE ACTIVATED ===", true);
        Utils::threadSafePrint("Miner running in background. Check " + config.logFileName + " for status.", true);
    }
    
    // Show the main header with timestamp
    Utils::threadSafePrint("=== MoneroMiner v1.0.0 ===", true);
    
    // Show detailed system info (always)
    printDetailedSystemInfo();
    
    // Show configuration AFTER system info
    printConfig();
    
    // Main mining loop with auto-restart
    bool firstRun = true;
    int reconnectAttempts = 0;
    const int MAX_RECONNECT_ATTEMPTS = 5;
    std::thread statsThread;
    
    while (reconnectAttempts < MAX_RECONNECT_ATTEMPTS) {
        if (!firstRun) {
            Utils::threadSafePrint("=== RESTARTING MINER (Attempt " + 
                std::to_string(reconnectAttempts + 1) + "/" + 
                std::to_string(MAX_RECONNECT_ATTEMPTS) + ") ===", true);
            
            // Cleanup previous session
            shouldStop = true;
            
            // Wait for threads to stop
            for (auto& thread : miningThreads) {
                if (thread.joinable()) thread.join();
            }
            if (jobListenerThread.joinable()) jobListenerThread.join();
            if (statsThread.joinable()) statsThread.join();
            
            // Clean up resources
            for (auto* data : threadData) {
                delete data;
            }
            threadData.clear();
            miningThreads.clear();
            
            RandomXManager::cleanup();
            PoolClient::cleanup();
            
            // Reset stop flag
            shouldStop = false;
            
            // Wait before reconnecting
            std::this_thread::sleep_for(std::chrono::seconds(10));
        }
        
        firstRun = false;
        
        // Start mining (this is the ONLY place it's called)
        if (!startMining()) {
            Utils::threadSafePrint("Failed to start mining", true);
            reconnectAttempts++;
            continue;
        }
        
        statsThread = std::thread(MiningStatsUtil::globalStatsMonitor);
        
        Utils::threadSafePrint("=== MINER IS NOW RUNNING ===", true);
        Utils::threadSafePrint("Press Ctrl+C to stop mining", true);
        
        // Main loop - monitor for connection issues
        auto lastStatsTime = std::chrono::steady_clock::now();
        int secondsCounter = 0;
        auto lastJobTime = std::chrono::steady_clock::now();
        
        while (!shouldStop) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
            secondsCounter++;
            
            // Check if we've received any jobs recently
            {
                std::lock_guard<std::mutex> lock(PoolClient::jobMutex);
                if (!PoolClient::jobQueue.empty()) {
                    lastJobTime = std::chrono::steady_clock::now();
                }
            }
            
            auto now = std::chrono::steady_clock::now();
            auto jobTimeout = std::chrono::duration_cast<std::chrono::seconds>(now - lastJobTime).count();
            
            // If no job for 5 minutes, connection is dead
            if (jobTimeout > 300) {
                Utils::threadSafePrint("ERROR: No job received for 5 minutes - connection dead", true);
                shouldStop = true;
                reconnectAttempts++;
                break;
            }
            
            // Display stats every 10 seconds (both debug and non-debug modes)
            if (secondsCounter >= 10) {
                auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - lastStatsTime).count();
                
                if (elapsed >= 10) {
                    double totalHashrate = 0.0;
                    for (size_t i = 0; i < threadData.size(); i++) {
                        if (threadData[i] != nullptr) {
                            totalHashrate += threadData[i]->getHashrate();
                        }
                    }
                    
                    uint64_t currentDiff = 0;
                    {
                        std::lock_guard<std::mutex> lock(PoolClient::jobMutex);
                        if (!PoolClient::jobQueue.empty()) {
                            currentDiff = PoolClient::jobQueue.front().difficulty;
                        }
                    }
                    
                    std::stringstream ss;
                    ss << "Hashrate: " << std::fixed << std::setprecision(1) << totalHashrate << " H/s";
                    ss << " | Difficulty: " << currentDiff;
                    ss << " | Accepted: " << MiningStatsUtil::acceptedShares.load();
                    ss << " | Rejected: " << MiningStatsUtil::rejectedShares.load();
                    
                    Utils::threadSafePrint(ss.str(), false);
                    lastStatsTime = now;
                    secondsCounter = 0;
                }
            }
        }
        
        // If user stopped (not error), break
        if (shouldStop && reconnectAttempts == 0) {
            Utils::threadSafePrint("Miner stopped by user", true);
            break;
        }
    }
    
    if (reconnectAttempts >= MAX_RECONNECT_ATTEMPTS) {
        Utils::threadSafePrint("ERROR: Maximum reconnection attempts reached - giving up", true);
    }

    // Final cleanup
    Utils::threadSafePrint("Shutting down miner...", true);
    
    shouldStop = true;
    
    for (auto& thread : miningThreads) {
        if (thread.joinable()) thread.join();
    }
    if (jobListenerThread.joinable()) jobListenerThread.join();
    if (statsThread.joinable()) statsThread.join();
    
    // Clean up resources
    for (auto* data : threadData) {
        delete data;
    }
    threadData.clear();
    miningThreads.clear();
    
    RandomXManager::cleanup();
    PoolClient::cleanup();
    
    WSACleanup();
    
    Utils::threadSafePrint("Miner shut down successfully", true);
    
    return 0;
}