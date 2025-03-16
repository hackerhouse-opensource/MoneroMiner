/**
 * MoneroMiner.cpp - Lightweight High Performance Monero (XMR) CPU Miner
 * 
 * This is a lightweight implementation focused on efficient CPU mining using the RandomX algorithm.
 * This implementation provides a robust and efficient CPU mining solution for the Monero
 * cryptocurrency using the RandomX algorithm. Key features include:
 * 
 * - Multi-threaded mining with optimized CPU utilization
 * - Pool mining support with stratum protocol
 * - Configurable via command-line options
 * - Persistent dataset caching for improved startup time
 * - Debug logging for troubleshooting
 * 
 * Author: Hacker Fantastic (https://hacker.house)
 * License: Attribution-NonCommercial-NoDerivatives 4.0 International
 * https://creativecommons.org/licenses/by-nc-nd/4.0/
 */
 
#pragma once

#include <iostream>
#include <iomanip>
#include <sstream>
#include <string>
#include <vector>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <chrono>
#include <memory>
#include <atomic>
#include <cstring>
#include <stdexcept>
#include <random>
#include <algorithm>
#include <functional>
#include <cstdint>
#include <cstdlib>
#include <ctime>
#include <fstream>
#include <csignal>
#include <limits>
#include <WinSock2.h>
#include <WS2tcpip.h>
#include "randomx.h"
#include "picojson.h"

#pragma comment(lib, "Ws2_32.lib")

// Configuration structure
struct MinerConfig {
    std::string poolAddress = "xmr-eu1.nanopool.org";
    std::string poolPort = "10300";
    std::string walletAddress = "8BghJxGWaE2Ekh8KrrEEqhGMLVnB17cCATNscfEyH8qq9uvrG3WwYPXbvqfx1HqY96ZaF3yVYtcQ2X1KUMNt2Pr29M41jHf";
    std::string workerName = "miniminer";
    std::string password = "x";
    std::string userAgent = "miniminer/1.0.0";
    std::string logFile = "MoneroMiner.log";
    bool useLogFile = false;
    int numThreads;
    bool debugMode = false;

    MinerConfig() {
        // Calculate optimal thread count based on CPU resources
        // For RandomX mining, we want to use all available CPU cores
        // as RandomX is designed to be CPU-friendly
        numThreads = std::thread::hardware_concurrency();
        if (numThreads == 0) {
            // Fallback if hardware_concurrency fails
            numThreads = 4;
        }
    }
};

// Global configuration instance
MinerConfig config;

// Function to print help message
void printHelp() {
    std::cout << "MoneroMiner v1.0.0 - Lightweight High Performance Monero CPU Miner\n\n"
              << "This is a lightweight implementation focused on efficient CPU mining using the RandomX algorithm.\n\n"
              << "Usage: MoneroMiner.exe [OPTIONS]\n\n"
              << "Options:\n"
              << "  --help               Show this help message\n"
              << "  --debug              Enable debug logging (default: " << (config.debugMode ? "true" : "false") << ")\n"
              << "  --logfile [FILE]     Enable logging to file. If no filename is provided,\n"
              << "                       defaults to '" << config.logFile << "'. All console\n"
              << "                       output will be saved to this file.\n"
              << "  --threads N          Number of mining threads (default: " << config.numThreads << " - auto-detected)\n"
              << "  --pool-address URL   Pool address (default: " << config.poolAddress << ")\n"
              << "  --pool-port PORT     Pool port (default: " << config.poolPort << ")\n"
              << "  --wallet ADDRESS     Wallet address (default: " << config.walletAddress << ")\n"
              << "  --worker-name NAME   Worker name (default: " << config.workerName << ")\n"
              << "  --password PASS      Pool password (default: " << config.password << ")\n"
              << "  --user-agent AGENT   User agent string (default: " << config.userAgent << ")\n\n"
              << "Example:\n"
              << "  MoneroMiner.exe"
              << " --threads " << config.numThreads
              << " --pool-address " << config.poolAddress
              << " --pool-port " << config.poolPort
              << " --wallet " << config.walletAddress
              << " --worker-name " << config.workerName
              << " --password " << config.password
              << " --user-agent \"" << config.userAgent << "\"\n"
              << "  MoneroMiner.exe --debug --logfile debug.log --threads 4\n"
              << std::endl;
}

// Function to parse command line arguments
bool parseCommandLine(int argc, char* argv[]) {
    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        if (arg == "--help") {
            printHelp();
            return false;
        } else if (arg == "--debug") {
            config.debugMode = true;
        } else if (arg == "--logfile") {
            config.useLogFile = true;
            if (i + 1 < argc && argv[i + 1][0] != '-') {
                config.logFile = argv[++i];
            }
        } else if (arg == "--threads" && i + 1 < argc) {
            config.numThreads = std::atoi(argv[++i]);
            if (config.numThreads <= 0) {
                config.numThreads = std::thread::hardware_concurrency();
            }
        } else if (arg == "--pool-address" && i + 1 < argc) {
            config.poolAddress = argv[++i];
        } else if (arg == "--pool-port" && i + 1 < argc) {
            config.poolPort = argv[++i];
        } else if (arg == "--wallet" && i + 1 < argc) {
            config.walletAddress = argv[++i];
        } else if (arg == "--worker-name" && i + 1 < argc) {
            config.workerName = argv[++i];
        } else if (arg == "--password" && i + 1 < argc) {
            config.password = argv[++i];
        } else if (arg == "--user-agent" && i + 1 < argc) {
            config.userAgent = argv[++i];
        }
    }
    return true;
}

// RandomX feature flags
#ifndef RANDOMX_FEATURE_JIT
#define RANDOMX_FEATURE_JIT 1
#endif

#ifndef RANDOMX_FLAG_JIT
#define RANDOMX_FLAG_JIT 1
#endif

#ifndef RANDOMX_FLAG_FULL_MEM
#define RANDOMX_FLAG_FULL_MEM 4
#endif

// RandomX configuration
#define RANDOMX_DATASET_ITEM_SIZE 64
#define RANDOMX_DATASET_BASE_SIZE 2147483648  // 2GB for Fast Mode
#define RANDOMX_DATASET_EXTRA_SIZE 33554368   // 32MB
#define RANDOMX_SCRATCHPAD_L3 2097152        // 2MB
#define RANDOMX_CACHE_BASE_SIZE 2147483648   // 2GB for Fast Mode
#define RANDOMX_CACHE_ACCESSES 4

// Mining configuration
#define NONCE_SIZE 4
#define NONCE_OFFSET 39
#define HASH_SIZE 32
#define JOB_ID_SIZE 32
#define TARGET_SIZE 4
#define SHARE_SUBMISSION_RETRIES 3
#define JOB_QUEUE_SIZE 2
#define HASHRATE_AVERAGING_WINDOW_SIZE 60  // 60 seconds
#define THREAD_PAUSE_TIME 100              // Milliseconds to pause when no jobs available

// Error handling
#define CHECK_SOCKET_ERROR(x) if(x == SOCKET_ERROR) { \
    threadSafePrint("Socket error: " + std::to_string(WSAGetLastError())); \
    return false; \
}

#define CHECK_NULL(x, msg) if(x == nullptr) { \
    threadSafePrint(msg); \
    return false; \
}

// Helper functions
template<typename Container>
std::string bytesToHex(const Container& bytes) {
    std::ostringstream oss;
    for (auto byte : bytes) {
        oss << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(byte);
    }
    return oss.str();
}

template<typename Iterator>
std::string bytesToHex(Iterator begin, Iterator end) {
    std::ostringstream oss;
    for (auto it = begin; it != end; ++it) {
        oss << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(*it);
    }
    return oss.str();
}

// Forward declarations
class AlignedMemory;
class HashBuffers;
class MiningThreadData;
struct Job;
struct MiningStats;
struct GlobalStats;

// Function declarations
void signalHandler(int signum);
std::vector<uint8_t> hexToBytes(const std::string& hex);
void incrementNonce(std::vector<uint8_t>& blob, uint32_t nonce);
std::string createSubmitPayload(const std::string& sessionId, const std::string& jobId, 
                              const std::string& nonceHex, const std::string& hashHex, 
                              const std::string& algo);
std::string sendAndReceive(SOCKET sock, const std::string& payload);
void handleLoginResponse(const std::string& response);
void miningThread(MiningThreadData* data);
void updateHashrate(MiningStats& stats);
std::string formatRuntime(uint64_t seconds);
void printStats(const MiningStats& stats);
std::vector<uint8_t> compactTo256BitTarget(const std::string& targetHex);
uint64_t getTargetDifficulty(const std::string& targetHex);
void initializeThreadVM(MiningThreadData* data);
void threadSafePrint(const std::string& message, bool debugOnly = false);
void processNewJob(const picojson::object& job);
void handleSeedHashChange(const std::string& newSeedHash);
bool initializeRandomX(const std::string& seedHash);
bool isDatasetValid(const std::string& filename, const std::string& currentSeedHash);
void saveDataset(randomx_dataset* dataset, const std::string& filename, const std::string& seedHash);
void loadDataset(randomx_dataset* dataset, const std::string& filename);
void handleShareResponse(const std::string& response, bool& accepted);
bool submitShare(const std::string& jobId, const std::string& nonceHex, 
                const std::string& hashHex, const std::string& algo);

// Global statistics structure
struct GlobalStats {
    std::atomic<uint64_t> totalHashrate{0};
    std::atomic<uint64_t> totalShares{0};
    std::atomic<uint64_t> acceptedShares{0};
    std::atomic<uint64_t> rejectedShares{0};
    std::chrono::steady_clock::time_point startTime = std::chrono::steady_clock::now();
};

// Global variables
std::mutex jobMutex;
std::string currentBlobHex;
std::string currentTargetHex;
std::string currentJobId;
int jsonRpcId = 1;
std::string sessionId;
std::atomic<bool> shouldStop(false);
std::atomic<uint64_t> totalHashes(0);
std::atomic<uint64_t> acceptedShares(0);
std::atomic<uint64_t> rejectedShares(0);
std::mutex jobQueueMutex;
std::condition_variable jobQueueCV;
SOCKET globalSocket = INVALID_SOCKET;
std::mutex consoleMutex;
std::mutex logfileMutex;
std::ofstream logFile;
bool debugMode = false;
std::string currentSeedHash;
std::mutex seedHashMutex;
randomx_cache* currentCache = nullptr;
randomx_dataset* currentDataset = nullptr;
std::mutex cacheMutex;
std::vector<MiningThreadData*> threadData;
GlobalStats globalStats;
std::atomic<uint32_t> debugHashCounter(0);
std::atomic<bool> newJobAvailable(false);
std::atomic<bool> showedInitMessage(false);
std::atomic<uint32_t> activeJobId(0);

// Mining job structure
struct Job {
    std::vector<uint8_t> blob;
    std::string target;
    std::string jobId;
    uint64_t height;
    std::string seedHash;
};

// Queue for mining jobs
std::queue<Job> jobQueue;

// Mining statistics structure
struct MiningStats {
    std::chrono::steady_clock::time_point startTime;
    uint64_t totalHashes;
    uint64_t acceptedShares;
    uint64_t rejectedShares;
    uint64_t currentHashrate;
    uint64_t runtime;
    std::mutex statsMutex;
};

// Aligned memory allocation class
class AlignedMemory {
private:
    void* ptr;
    size_t size;

public:
    explicit AlignedMemory(size_t _size, size_t alignment) : ptr(nullptr), size(_size) {
        ptr = _aligned_malloc(size, alignment);
        if (!ptr) {
            throw std::runtime_error("Failed to allocate aligned memory");
        }
        std::memset(ptr, 0, size);
    }

    ~AlignedMemory() {
        if (ptr) {
            _aligned_free(ptr);
            ptr = nullptr;
        }
    }

    void* get() const { return ptr; }
    size_t getSize() const { return size; }

    AlignedMemory(const AlignedMemory&) = delete;
    AlignedMemory& operator=(const AlignedMemory&) = delete;
    AlignedMemory(AlignedMemory&&) = delete;
    AlignedMemory& operator=(AlignedMemory&&) = delete;
};

// HashBuffers class for managing aligned memory for RandomX operations
class HashBuffers {
private:
    std::unique_ptr<AlignedMemory> tempHash;
    std::unique_ptr<AlignedMemory> hash;
    std::unique_ptr<AlignedMemory> scratchpad;

public:
    HashBuffers() {
        tempHash = std::make_unique<AlignedMemory>(RANDOMX_HASH_SIZE, 64);
        hash = std::make_unique<AlignedMemory>(RANDOMX_HASH_SIZE, 64);
        scratchpad = std::make_unique<AlignedMemory>(RANDOMX_SCRATCHPAD_L3, 64);
    }

    uint8_t* getTempHash() { return static_cast<uint8_t*>(tempHash->get()); }
    uint8_t* getHash() { return static_cast<uint8_t*>(hash->get()); }
    uint8_t* getScratchpad() { return static_cast<uint8_t*>(scratchpad->get()); }
};

// Mining thread data class
class MiningThreadData {
private:
    std::unique_ptr<HashBuffers> hashBuffers;
    randomx_vm* vm;
    std::mutex vmMutex;
    bool vmInitialized;
    int threadId;  // Add thread ID

public:
    static const unsigned int BATCH_SIZE = 256;  // Define batch size as static const member
    std::atomic<bool> isRunning;
    std::chrono::steady_clock::time_point startTime;

    MiningThreadData(int id) : vm(nullptr), vmInitialized(false), isRunning(false), 
                              startTime(std::chrono::steady_clock::now()), threadId(id) {
        hashBuffers = std::make_unique<HashBuffers>();
    }

    int getThreadId() const { return threadId; }

    ~MiningThreadData() {
        cleanup();
    }

    bool initializeVM() {
        std::lock_guard<std::mutex> lock(vmMutex);
        if (vmInitialized) return true;

        try {
            if (currentCache == nullptr || currentDataset == nullptr) {
                threadSafePrint("Cache or dataset not initialized", true);
                return false;
            }

            // Define all required RandomX flags
            const randomx_flags flags = static_cast<randomx_flags>(
                RANDOMX_FLAG_FULL_MEM |    // Use full memory mode
                RANDOMX_FLAG_JIT |         // Enable JIT compilation
                RANDOMX_FLAG_HARD_AES      // Use hardware AES
            );

            if (debugMode) {
                std::stringstream ss;
                ss << "Creating RandomX VM with flags:"
                   << "\n  RANDOMX_FLAG_FULL_MEM: " << ((flags & RANDOMX_FLAG_FULL_MEM) ? "YES" : "NO")
                   << "\n  RANDOMX_FLAG_JIT: " << ((flags & RANDOMX_FLAG_JIT) ? "YES" : "NO")
                   << "\n  RANDOMX_FLAG_HARD_AES: " << ((flags & RANDOMX_FLAG_HARD_AES) ? "YES" : "NO");
                threadSafePrint(ss.str(), true);
            }

            vm = randomx_create_vm(flags, currentCache, currentDataset);
            
            if (!vm) {
                threadSafePrint("Failed to create RandomX VM - null pointer returned", true);
                return false;
            }

            vmInitialized = true;
            if (debugMode) {
                threadSafePrint("RandomX VM created successfully");
            }
            return true;
        } catch (const std::exception& e) {
            threadSafePrint("Exception in initializeVM: " + std::string(e.what()), true);
            return false;
        }
    }

    bool calculateHash(const std::vector<uint8_t>& blob, uint8_t* hash, uint32_t currentDebugCounter = 0) {
        std::lock_guard<std::mutex> lock(vmMutex);
        if (!vmInitialized || !vm) {
            threadSafePrint("VM not initialized for hash calculation", true);
            return false;
        }

        try {
            randomx_calculate_hash(vm, blob.data(), blob.size(), hash);

            // Only log first hash and every 10000th hash in debug mode to reduce log spam
            if (debugMode && (currentDebugCounter == 1 || currentDebugCounter % 10000 == 0)) {
                std::stringstream ss;
                ss << "Thread " << threadId << " | Hash #" << currentDebugCounter 
                   << " | Nonce=0x" << bytesToHex(std::vector<uint8_t>(blob.begin() + 39, blob.begin() + 43))
                   << " | Hash=0x" << bytesToHex(std::vector<uint8_t>(hash, hash + RANDOMX_HASH_SIZE));
                threadSafePrint(ss.str(), true);
            }

            return true;
        } catch (const std::exception& e) {
            threadSafePrint("Exception in calculateHash: " + std::string(e.what()), true);
            return false;
        }
    }

    void cleanup() {
        std::lock_guard<std::mutex> lock(vmMutex);
        if (vm) {
            randomx_destroy_vm(vm);
            vm = nullptr;
        }
        vmInitialized = false;
    }
};

// Job processing function
void processNewJob(const picojson::object& jobObj) {
    // Check for required fields
    auto blobIt = jobObj.find("blob");
    auto targetIt = jobObj.find("target");
    auto jobIdIt = jobObj.find("job_id");
    auto heightIt = jobObj.find("height");
    auto seedHashIt = jobObj.find("seed_hash");
    
    if (blobIt == jobObj.end() || !blobIt->second.is<std::string>() ||
        targetIt == jobObj.end() || !targetIt->second.is<std::string>() ||
        jobIdIt == jobObj.end() || !jobIdIt->second.is<std::string>() ||
        heightIt == jobObj.end() || !heightIt->second.is<double>() ||
        seedHashIt == jobObj.end() || !seedHashIt->second.is<std::string>()) {
        threadSafePrint("Invalid job format received");
        return;
    }
    
    // Create new job
    Job newJob;
    std::vector<uint8_t> blobBytes = hexToBytes(blobIt->second.get<std::string>());
    newJob.blob.assign(blobBytes.begin(), blobBytes.end());
    newJob.target = targetIt->second.get<std::string>();
    newJob.jobId = jobIdIt->second.get<std::string>();
    newJob.height = static_cast<uint64_t>(heightIt->second.get<double>());
    newJob.seedHash = seedHashIt->second.get<std::string>();
    
    // Update current seed hash if changed
    if (newJob.seedHash != currentSeedHash) {
        handleSeedHashChange(newJob.seedHash);
    }
    
    // Signal threads to stop current work
    newJobAvailable = true;
    activeJobId++;  // Increment job counter to invalidate old jobs
    
    // Clear job queue and add new job
    {
        std::lock_guard<std::mutex> lock(jobQueueMutex);
        while (!jobQueue.empty()) {
            jobQueue.pop();
        }
        jobQueue.push(newJob);
    }
    
    // Notify waiting threads
    jobQueueCV.notify_all();
    
    // Log job details
    std::stringstream ss;
    ss << "New job received - Height: " << newJob.height 
       << ", Job ID: " << newJob.jobId 
       << ", Target: " << newJob.target;
    threadSafePrint(ss.str());
    
    if (debugMode) {
        ss.str("");
        ss << "New job details:"
           << "\n  Height: " << newJob.height
           << "\n  Job ID: " << newJob.jobId
           << "\n  Target: " << newJob.target
           << "\n  Seed Hash: " << newJob.seedHash
           << "\n  Blob size: " << newJob.blob.size() << " bytes"
           << "\n  Blob: " << bytesToHex(newJob.blob)
           << "\n  Initial nonce position: bytes 39-42";
        threadSafePrint(ss.str(), true);
    }
}

// Job listener thread
void listenForNewJobs(SOCKET sock) {
    char buffer[4096];
    std::string accumulated;
    fd_set readSet;
    struct timeval timeout;
    auto lastKeepAlive = std::chrono::steady_clock::now();
    
    while (!shouldStop) {
        FD_ZERO(&readSet);
        FD_SET(sock, &readSet);
        
        timeout.tv_sec = 1;  // 1 second timeout
        timeout.tv_usec = 0;
        
        // Send keep-alive every 30 seconds
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - lastKeepAlive).count();
        if (elapsed >= 30) {
            picojson::object keepAliveObj;
            keepAliveObj["id"] = picojson::value(static_cast<double>(jsonRpcId++));
            keepAliveObj["jsonrpc"] = picojson::value("2.0");
            keepAliveObj["method"] = picojson::value("keepalived");
            keepAliveObj["params"] = picojson::value(picojson::object());
            
            picojson::value keepAliveValue(keepAliveObj);
            std::string keepAliveStr = keepAliveValue.serialize() + "\n";
            
            if (debugMode) {
                threadSafePrint("Pool send: " + keepAliveStr);
            }
            
            if (send(sock, keepAliveStr.c_str(), static_cast<int>(keepAliveStr.length()), 0) == SOCKET_ERROR) {
                threadSafePrint("Failed to send keep-alive");
                break;
            }
            lastKeepAlive = now;
            threadSafePrint("Sent keep-alive");
        }
        
        int result = select(0, &readSet, nullptr, nullptr, &timeout);
        if (result == SOCKET_ERROR) {
            threadSafePrint("Select failed: " + std::to_string(WSAGetLastError()));
            break;
        }
        
        if (result > 0 && FD_ISSET(sock, &readSet)) {
            int bytesReceived = recv(sock, buffer, sizeof(buffer), 0);
            if (bytesReceived == SOCKET_ERROR) {
                int error = WSAGetLastError();
                if (error != WSAEWOULDBLOCK) {
                    threadSafePrint("Socket error: " + std::to_string(error));
                    break;
                }
                continue;
            }
            
            if (bytesReceived == 0) {
                threadSafePrint("Pool connection closed");
                break;
            }
            
            accumulated.append(buffer, bytesReceived);
            
            size_t pos;
            while ((pos = accumulated.find('\n')) != std::string::npos) {
                std::string message = accumulated.substr(0, pos);
                accumulated.erase(0, pos + 1);
                
                if (debugMode) {
                    threadSafePrint("Pool recv: " + message);
                }
                
                picojson::value v;
                std::string err = picojson::parse(v, message);
                if (!err.empty()) {
                    threadSafePrint("JSON parse error: " + err);
                    continue;
                }
                
                if (!v.is<picojson::object>()) {
                    continue;
                }
                
                const picojson::object& obj = v.get<picojson::object>();
                
                // Handle different message types
                auto methodIt = obj.find("method");
                if (methodIt != obj.end() && methodIt->second.is<std::string>()) {
                    std::string method = methodIt->second.get<std::string>();
                    
                    if (method == "job") {
                        auto paramsIt = obj.find("params");
                        if (paramsIt != obj.end() && paramsIt->second.is<picojson::object>()) {
                            const picojson::object& params = paramsIt->second.get<picojson::object>();
                            
                            // For job notifications, process the params directly
                            processNewJob(params);
                            
                            // Check if this is a login response with nested job
                            auto jobIt = params.find("job");
                            if (jobIt != params.end() && jobIt->second.is<picojson::object>()) {
                                // For login response, process the nested job
                                processNewJob(jobIt->second.get<picojson::object>());
                            }
                            
                            // Check for seed hash change
                            auto seedHashIt = params.find("seed_hash");
                            if (seedHashIt != params.end() && seedHashIt->second.is<std::string>()) {
                                std::string newSeedHash = seedHashIt->second.get<std::string>();
                                if (newSeedHash != currentSeedHash) {
                                    handleSeedHashChange(newSeedHash);
                                }
                            }
                        }
                    }
                }
                
                // Handle result messages (responses)
                auto resultIt = obj.find("result");
                if (resultIt != obj.end() && resultIt->second.is<picojson::object>()) {
                    const auto& result = resultIt->second.get<picojson::object>();
                    if (result.count("job") && result.at("job").is<picojson::object>()) {
                        const auto& job = result.at("job").get<picojson::object>();
                        std::string details = "New job details:";
                        if (job.count("height")) details += " Height=" + job.at("height").to_str();
                        if (job.count("job_id")) details += " JobID=" + job.at("job_id").to_str();
                        if (job.count("target")) details += " Target=" + job.at("target").to_str();
                        threadSafePrint(details);
                    }
                }
            }
        }
    }
    
    threadSafePrint("Job listener thread stopped");
}

// Function to compare hash with target
bool isHashLessThanTarget(const std::vector<uint8_t>& hash, const std::vector<uint8_t>& target) {
    // Compare bytes from most significant to least significant (big-endian)
    for (int i = 0; i < 32; i++) {
        if (hash[i] < target[i]) return true;
        if (hash[i] > target[i]) return false;
    }
    return false;  // Equal case
}

// Function to expand compact target to full 256-bit target
std::vector<uint8_t> compactTo256BitTarget(const std::string& targetHex) {
    std::vector<uint8_t> target(32, 0);  // Initialize 256-bit target with zeros
    uint32_t compact = std::stoul(targetHex, nullptr, 16);
    
    // Extract mantissa (24 bits) and exponent (8 bits)
    uint32_t mantissa = compact & 0x00FFFFFF;  // Extract 24-bit mantissa
    uint8_t exponent = (compact >> 24) & 0xFF; // Extract 8-bit exponent
    
    // For Monero's target format:
    // The mantissa should be written at the leftmost position (most significant bytes)
    // The rest of the bytes should be zeros
    // For example, target 0xf3220000:
    // - exponent = 0xf3 (243)
    // - mantissa = 0x220000
    // Should expand to: 0x2200000000000000000000000000000000000000000000000000000000000000
    
    // Write mantissa in big-endian format at the start of the target
    target[0] = (mantissa >> 16) & 0xFF;  // Most significant byte
    target[1] = (mantissa >> 8) & 0xFF;   // Middle byte
    target[2] = mantissa & 0xFF;          // Least significant byte
    
    // Debug logging only for first target expansion and every 10000th hash
    static std::atomic<uint64_t> targetExpandCounter(0);
    uint64_t currentCount = ++targetExpandCounter;
    if (debugMode && (currentCount == 1 || currentCount % 10000 == 0)) {
        std::stringstream ss;
        ss << "Target expansion #" << currentCount << ":"
           << "\n  Input target: 0x" << targetHex
           << "\n  Exponent: " << static_cast<int>(exponent) << " (0x" << std::hex << static_cast<int>(exponent) << ")"
           << "\n  Mantissa: 0x" << std::hex << mantissa
           << "\n  Expanded target (BE): 0x";
        for (const auto& byte : target) {
            ss << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(byte);
        }
        threadSafePrint(ss.str(), true);
    }
    
    return target;
}

// Function to check if the hash meets the target difficulty
bool isHashValid(const std::vector<uint8_t>& hash, const std::string& targetHex) {
    static std::atomic<uint64_t> hashCheckCounter(0);
    uint64_t currentCount = ++hashCheckCounter;
    
    // Convert target hex to bytes
    std::vector<uint8_t> target = compactTo256BitTarget(targetHex);
    
    // Compare bytes from most significant to least significant (big-endian)
    bool isValid = false;
    for (int i = 0; i < 32; i++) {
        if (hash[i] < target[i]) {
            isValid = true;
            break;
        }
        if (hash[i] > target[i]) {
            isValid = false;
            break;
        }
    }
    
    // Debug logging only for first hash and every 10000th hash
    if (debugMode && (currentCount == 1 || currentCount % 10000 == 0)) {
        std::stringstream ss;
        ss << "Hash validation #" << currentCount << ":"
           << "\n  Hash (BE): 0x" << bytesToHex(hash)
           << "\n  Target (BE): 0x" << bytesToHex(target)
           << "\n  Result: " << (isValid ? "VALID" : "INVALID");
        threadSafePrint(ss.str(), true);
    }
    
    return isValid;
}

// Calculate actual difficulty from target (XMRig style)
uint64_t getTargetDifficulty(const std::string& targetHex) {
    uint32_t compact = std::stoul(targetHex, nullptr, 16);
    return 0xFFFFFFFFFFFFFFFFULL / ((uint64_t)(compact & 0x00FFFFFF));
}

// Helper function to check if a hash meets difficulty (XMRig style)
bool checkHashDifficulty(const std::vector<uint8_t>& hash, uint64_t difficulty) {
    if (difficulty == 0) return false;
    
    // Convert first 8 bytes of hash to uint64_t in little-endian order
    uint64_t hash64 = 0;
    for (int i = 0; i < 8; i++) {
        hash64 |= static_cast<uint64_t>(hash[i]) << (8 * i);
    }
    
    // Debug logging with counter
    static std::atomic<uint64_t> diffCheckCounter(0);
    uint64_t currentCount = ++diffCheckCounter;
    if (debugMode && (currentCount == 1 || currentCount % 10000 == 0)) {
        std::stringstream ss;
        ss << "Difficulty check #" << currentCount << ":"
           << "\n  Hash value (first 8 bytes): 0x" << std::hex << hash64
           << "\n  Required difficulty: " << std::dec << difficulty
           << "\n  Hash <= Target: " << (hash64 <= (std::numeric_limits<uint64_t>::max)() / difficulty ? "YES" : "NO");
        threadSafePrint(ss.str(), true);
    }
    
    return hash64 <= (std::numeric_limits<uint64_t>::max)() / difficulty;
}

// RandomX management functions
bool initializeRandomX(const std::string& seedHash) {
    std::lock_guard<std::mutex> lock(cacheMutex);
    
    if (currentCache != nullptr) {
        if (currentSeedHash == seedHash) {
            if (debugMode) {
                threadSafePrint("RandomX already initialized with current seed hash");
            }
            return true;
        }
        if (debugMode) {
            threadSafePrint("RandomX seed hash changed, reinitializing...");
        }
        randomx_release_cache(currentCache);
        currentCache = nullptr;
    }

    if (debugMode) {
        threadSafePrint("Initializing RandomX...");
    }
    
    currentCache = randomx_alloc_cache(static_cast<randomx_flags>(RANDOMX_FLAG_JIT | RANDOMX_FLAG_FULL_MEM));
    CHECK_NULL(currentCache, "Failed to allocate RandomX cache");

    std::vector<uint8_t> seedHashBytes = hexToBytes(seedHash);
    randomx_init_cache(currentCache, seedHashBytes.data(), seedHashBytes.size());
    
    if (currentDataset != nullptr) {
        randomx_release_dataset(currentDataset);
    }
    
    currentDataset = randomx_alloc_dataset(static_cast<randomx_flags>(RANDOMX_FLAG_JIT | RANDOMX_FLAG_FULL_MEM));
    CHECK_NULL(currentDataset, "Failed to allocate RandomX dataset");
    
    uint32_t datasetItemCount = randomx_dataset_item_count();
    if (debugMode) {
        threadSafePrint("Dataset size: " + std::to_string(datasetItemCount * RANDOMX_DATASET_ITEM_SIZE / (1024*1024)) + " MB");
    }

    // Check if we have a saved dataset with matching seed hash
    std::string datasetPath = "randomx_dataset.bin";
    if (isDatasetValid(datasetPath, seedHash)) {
        threadSafePrint("Loading saved dataset...");
        loadDataset(currentDataset, datasetPath);
        threadSafePrint("Dataset loaded successfully");
    } else {
        threadSafePrint("Initializing new dataset (this may take several minutes)...");
        
        // Determine number of threads for dataset initialization
        unsigned int numInitThreads = std::thread::hardware_concurrency();
        if (numInitThreads == 0) numInitThreads = 4;  // Fallback if hardware_concurrency fails
        
        std::vector<std::thread> initThreads;
        uint32_t itemsPerThread = datasetItemCount / numInitThreads;
        uint32_t remainder = datasetItemCount % numInitThreads;
        
        if (debugMode) {
            threadSafePrint("Using " + std::to_string(numInitThreads) + " threads for dataset initialization");
        }
        
        for (unsigned int i = 0; i < numInitThreads; ++i) {
            uint32_t startItem = i * itemsPerThread;
            uint32_t itemCount = itemsPerThread + (i == numInitThreads - 1 ? remainder : 0);
            
            initThreads.emplace_back([=]() {
                randomx_init_dataset(currentDataset, currentCache, startItem, itemCount);
            });
        }
        
        // Wait for all initialization threads to complete
        for (auto& thread : initThreads) {
            thread.join();
        }
        
        threadSafePrint("Dataset initialized successfully");
        
        // Save the dataset for future use
        saveDataset(currentDataset, datasetPath, seedHash);
        if (debugMode) {
            threadSafePrint("Dataset saved to disk");
        }
    }

    currentSeedHash = seedHash;
    return true;
}

void handleSeedHashChange(const std::string& newSeedHash) {
    std::lock_guard<std::mutex> lock(seedHashMutex);
    if (newSeedHash != currentSeedHash) {
        threadSafePrint("Seed hash changed, reinitializing RandomX...");
        showedInitMessage = false;  // Reset the flag when seed hash changes
        
        // Stop all mining threads before reinitialization
        for (auto* data : threadData) {
            data->isRunning = false;
        }
        
        // Wait for a short period to ensure threads notice the stop signal
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        
        if (initializeRandomX(newSeedHash)) {
            threadSafePrint("RandomX reinitialization successful");
            
            // Reinitialize VMs in all mining threads
            bool allVMsInitialized = true;
            for (auto* data : threadData) {
                if (!data->initializeVM()) {
                    threadSafePrint("Failed to initialize VM for thread " + std::to_string(data->getThreadId()));
                    allVMsInitialized = false;
                    break;
                }
            }
            
            if (allVMsInitialized) {
                threadSafePrint("All mining thread VMs reinitialized successfully");
                // Resume mining threads
                for (auto* data : threadData) {
                    data->isRunning = true;
                }
            } else {
                threadSafePrint("Failed to reinitialize all VMs, mining may be impacted");
            }
        } else {
            threadSafePrint("RandomX reinitialization failed");
        }
    }
}

bool isDatasetValid(const std::string& filename, const std::string& currentSeedHash) {
    std::ifstream datasetFile(filename, std::ios::binary);
    std::ifstream seedFile(filename + ".seed", std::ios::binary);
    
    if (!datasetFile.good() || !seedFile.good()) {
        if (debugMode) {
            threadSafePrint("Dataset files not found or not accessible");
        }
        return false;
    }
    
    // Check dataset size
    datasetFile.seekg(0, std::ios::end);
    long long fileSize = datasetFile.tellg();
    datasetFile.close();
    
    size_t expectedSize = static_cast<size_t>(randomx_dataset_item_count()) * RANDOMX_DATASET_ITEM_SIZE;
    if (static_cast<size_t>(fileSize) != expectedSize) {
        if (debugMode) {
            threadSafePrint("Dataset size mismatch: expected " + std::to_string(expectedSize) + 
                          " bytes, got " + std::to_string(fileSize) + " bytes");
        }
        return false;
    }
    
    // Check seed hash
    std::string storedSeedHash;
    storedSeedHash.resize(currentSeedHash.size());
    seedFile.read(&storedSeedHash[0], currentSeedHash.size());
    seedFile.close();
    
    bool valid = storedSeedHash == currentSeedHash;
    if (debugMode) {
        std::string validationMsg = "Dataset seed hash validation: ";
        validationMsg += valid ? "valid" : "invalid";
        threadSafePrint(validationMsg);
    }
    return valid;
}

void saveDataset(randomx_dataset* dataset, const std::string& filename, const std::string& seedHash) {
    if (!dataset) {
        threadSafePrint("Invalid dataset pointer");
        return;
    }

    std::ofstream datasetFile(filename, std::ios::binary);
    if (!datasetFile.is_open()) {
        threadSafePrint("Failed to open dataset file for writing");
        return;
    }

    try {
        void* datasetMemory = randomx_get_dataset_memory(dataset);
        if (!datasetMemory) {
            threadSafePrint("Failed to get dataset memory");
            return;
        }

        size_t datasetSize = static_cast<size_t>(randomx_dataset_item_count()) * RANDOMX_DATASET_ITEM_SIZE;
        datasetFile.write(reinterpret_cast<const char*>(datasetMemory), datasetSize);
        datasetFile.close();

        std::ofstream seedFile(filename + ".seed", std::ios::binary);
        if (!seedFile.is_open()) {
            threadSafePrint("Failed to open seed file for writing");
            return;
        }

        seedFile.write(seedHash.c_str(), seedHash.size());
        seedFile.close();

        if (debugMode) {
            threadSafePrint("Dataset saved successfully: " + std::to_string(datasetSize) + " bytes");
        }
    } catch (const std::exception& e) {
        threadSafePrint("Exception while saving dataset: " + std::string(e.what()));
    }
}

void loadDataset(randomx_dataset* dataset, const std::string& filename) {
    if (!dataset) {
        threadSafePrint("Invalid dataset pointer");
        return;
    }

    std::ifstream file(filename, std::ios::binary);
    if (!file.is_open()) {
        threadSafePrint("Failed to open dataset file for reading");
        return;
    }

    try {
        void* datasetMemory = randomx_get_dataset_memory(dataset);
        if (!datasetMemory) {
            threadSafePrint("Failed to get dataset memory");
            return;
        }

        size_t datasetSize = static_cast<size_t>(randomx_dataset_item_count()) * RANDOMX_DATASET_ITEM_SIZE;
        file.read(reinterpret_cast<char*>(datasetMemory), static_cast<int>(datasetSize));
        file.close();

        if (debugMode) {
            threadSafePrint("Dataset loaded successfully: " + std::to_string(datasetSize) + " bytes");
        }
    } catch (const std::exception& e) {
        threadSafePrint("Exception while loading dataset: " + std::string(e.what()));
    }
}

// Function to handle share submission response
void handleShareResponse(const std::string& response, bool& accepted) {
    if (debugMode) {
        threadSafePrint("Share submission response: " + response);
    }

    picojson::value v;
    std::string err = picojson::parse(v, response);
    if (!err.empty()) {
        threadSafePrint("Failed to parse share response: " + err);
        return;
    }

    if (!v.is<picojson::object>()) {
        threadSafePrint("Invalid share response format");
        return;
    }

    const picojson::object& obj = v.get<picojson::object>();

    // Check if this is a job notification
    auto methodIt = obj.find("method");
    if (methodIt != obj.end() && methodIt->second.is<std::string>()) {
        std::string method = methodIt->second.get<std::string>();
        if (method == "job") {
            auto paramsIt = obj.find("params");
            if (paramsIt != obj.end() && paramsIt->second.is<picojson::object>()) {
                processNewJob(paramsIt->second.get<picojson::object>());
            }
            return;  // Don't process this as a share response
        }
    }

    // Check for error
    auto errorIt = obj.find("error");
    if (errorIt != obj.end() && !errorIt->second.is<picojson::null>()) {
        if (errorIt->second.is<picojson::object>()) {
            const picojson::object& error = errorIt->second.get<picojson::object>();
            auto messageIt = error.find("message");
            if (messageIt != error.end() && messageIt->second.is<std::string>()) {
                threadSafePrint("Share rejected: " + messageIt->second.get<std::string>());
            }
        } else {
            threadSafePrint("Share rejected with error: " + errorIt->second.serialize());
        }
        accepted = false;
        return;
    }

    // Check result
    auto resultIt = obj.find("result");
    if (resultIt != obj.end()) {
        if (resultIt->second.evaluate_as_boolean()) {
            threadSafePrint("Share accepted ✓");
            accepted = true;
        } else {
            threadSafePrint("Share rejected ✗");
            accepted = false;
        }
    }
}

// Implementation of submitShare function
bool submitShare(const std::string& jobId, const std::string& nonceHex, 
                const std::string& hashHex, const std::string& algo) 
{
    // Validate share before submission
    std::vector<uint8_t> hash = hexToBytes(hashHex);
    
    // Get current job for validation
    std::unique_lock<std::mutex> lock(jobQueueMutex);
    if (jobQueue.empty()) {
        threadSafePrint("No active job for share validation", true);
        return false;
    }
    
    Job currentJob = jobQueue.front();
    lock.unlock();
    
    // Verify job ID matches
    if (jobId != currentJob.jobId) {
        if (debugMode) {
            threadSafePrint("Share validation failed: job ID mismatch", true);
        }
        return false;
    }
    
    // Verify hash meets target
    if (!isHashValid(hash, currentJob.target)) {
        if (debugMode) {
            threadSafePrint("Share validation failed: hash does not meet target", true);
        }
        return false;
    }
    
    // Log share details in debug mode
    if (debugMode) {
        std::stringstream ss;
        ss << "Submitting validated share:"
           << "\n  Job ID: " << jobId
           << "\n  Nonce: 0x" << nonceHex
           << "\n  Hash: " << hashHex
           << "\n  Target: " << currentJob.target
           << "\n  Algorithm: " << algo;
        threadSafePrint(ss.str(), true);
    }
    
    // Create submission payload
    std::string payload = createSubmitPayload(sessionId, jobId, nonceHex, hashHex, algo);
    
    // Attempt submission with retries
    const int MAX_RETRIES = SHARE_SUBMISSION_RETRIES;
    const int RETRY_DELAY_MS = 1000;  // 1 second between retries
    
    for (int attempt = 1; attempt <= MAX_RETRIES; attempt++) {
        try {
            std::string response = sendAndReceive(globalSocket, payload);
            bool accepted = false;
            handleShareResponse(response, accepted);
            
            if (accepted) {
                if (attempt > 1) {
                    threadSafePrint("Share accepted on retry #" + std::to_string(attempt - 1));
                }
                return true;
            }
            
            // If share was rejected due to job not found or stale, don't retry
            if (response.find("\"error\"") != std::string::npos &&
                (response.find("job not found") != std::string::npos ||
                 response.find("stale share") != std::string::npos)) {
                if (debugMode) {
                    threadSafePrint("Share rejected permanently: " + response, true);
                }
                return false;
            }
            
            // For other rejections, retry if attempts remain
            if (attempt < MAX_RETRIES) {
                threadSafePrint("Share submission failed, retrying in 1 second... (Attempt " + 
                              std::to_string(attempt) + "/" + std::to_string(MAX_RETRIES) + ")");
                std::this_thread::sleep_for(std::chrono::milliseconds(RETRY_DELAY_MS));
                continue;
            }
        }
        catch (const std::exception& e) {
            if (attempt < MAX_RETRIES) {
                threadSafePrint("Share submission error: " + std::string(e.what()) + 
                              ", retrying in 1 second... (Attempt " + std::to_string(attempt) + 
                              "/" + std::to_string(MAX_RETRIES) + ")");
                std::this_thread::sleep_for(std::chrono::milliseconds(RETRY_DELAY_MS));
                continue;
            }
            threadSafePrint("Share submission failed after " + std::to_string(MAX_RETRIES) + 
                          " attempts: " + std::string(e.what()));
            return false;
        }
    }
    
    return false;
}

// Helper function to format thread ID consistently
std::string formatThreadId(int threadId) {
    std::stringstream ss;
    ss << std::setw(2) << std::setfill('0') << threadId;
    return ss.str();
}

// Update thread stats output formatting
void updateThreadStats(MiningThreadData* data, uint64_t hashCount, uint64_t totalHashCount,
                      int elapsedSeconds, const std::string& jobId, uint32_t currentNonce) {
    static std::vector<double> threadHashrates(32, 0.0);  // Support up to 32 threads
    
    // Calculate current thread hashrate
    double hashrate = elapsedSeconds > 0 ? static_cast<double>(hashCount) / elapsedSeconds : 0;
    threadHashrates[data->getThreadId()] = hashrate;
    
    // Calculate total hashrate across all threads
    uint64_t totalHashrate = 0;
    for (const auto& rate : threadHashrates) {
        totalHashrate += static_cast<uint64_t>(std::round(rate));
    }
    globalStats.totalHashrate.store(totalHashrate);
    
    // Only show per-thread stats in debug mode
    if (debugMode) {
        std::stringstream ss;
        ss << "Thread " << formatThreadId(data->getThreadId())
           << " | HR: " << std::fixed << std::setprecision(2) << std::setw(8) << hashrate << " H/s"
           << " | Job: " << jobId
           << " | Nonce: 0x" << std::hex << std::setw(8) << std::setfill('0') << currentNonce;
        threadSafePrint(ss.str(), true);
    }
}

// Update global stats monitor thread formatting
void globalStatsMonitor() {
    std::vector<double> threadHashrates(threadData.size(), 0.0);
    
    while (!shouldStop) {
        std::this_thread::sleep_for(std::chrono::seconds(10));
        
        // Calculate total hashrate and collect thread stats
        uint64_t totalHashrate = 0;
        size_t activeThreads = 0;
        
        for (size_t i = 0; i < threadData.size(); i++) {
            if (threadData[i]->isRunning) {
                threadHashrates[i] = static_cast<double>(globalStats.totalHashrate);
                totalHashrate += static_cast<uint64_t>(std::round(threadHashrates[i]));
                activeThreads++;
            }
        }
        
        // Format the global stats output
        std::stringstream ss;
        ss << "Global Stats: " << activeThreads << "/" << threadData.size() << " threads active | "
           << "HR: " << totalHashrate << " H/s | "
           << "Shares: " << acceptedShares << "/" << (acceptedShares + rejectedShares) 
           << " | Hashes: 0x" << std::hex << totalHashes;
        
        // Add thread summary with consistent formatting
        ss << "\nThread Summary:";
        for (size_t i = 0; i < threadHashrates.size(); i++) {
            if (i % 4 == 0) ss << "\n";
            ss << "T" << formatThreadId(i) << ": "
               << std::fixed << std::setprecision(1) << std::setw(8) << threadHashrates[i] << " H/s  ";
        }
        ss << "\n";
        
        threadSafePrint(ss.str(), false);
    }
}

// Main function implementation
int main(int argc, char* argv[]) {
    signal(SIGINT, signalHandler);
    signal(SIGTERM, signalHandler);
    
    // Parse command line arguments
    if (!parseCommandLine(argc, argv)) {
        return 0;  // Help was displayed
    }
    
    // Initialize log file if enabled
    if (config.useLogFile) {
        logFile.open(config.logFile, std::ios::out | std::ios::app);
        if (!logFile.is_open()) {
            std::cerr << "Failed to open log file: " << config.logFile << std::endl;
            return 1;
        }
    }

    debugMode = config.debugMode;
    threadSafePrint("Initializing with " + std::to_string(config.numThreads) + " mining threads...");
    
    // Initialize mining threads
    std::vector<std::thread> minerThreads;
    for (int i = 0; i < config.numThreads; i++) {
        void* ptr = _aligned_malloc(sizeof(MiningThreadData), 64);
        if (!ptr) {
            threadSafePrint("Failed to allocate aligned memory for mining thread " + std::to_string(i));
            continue;
        }
        
        MiningThreadData* miningData = new (ptr) MiningThreadData(i);
        threadData.push_back(miningData);
        minerThreads.emplace_back(miningThread, miningData);
    }
    
    // Initialize Winsock
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        threadSafePrint("Failed to initialize Winsock");
        return 1;
    }

    // Connect to pool
    threadSafePrint("Resolving " + config.poolAddress + ":" + config.poolPort + "...");
    
    struct addrinfo hints = {0}, *result = NULL;
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;

    if (getaddrinfo(config.poolAddress.c_str(), config.poolPort.c_str(), &hints, &result) != 0) {
        threadSafePrint("Failed to resolve pool address");
        WSACleanup();
        return 1;
    }

    SOCKET sock = socket(result->ai_family, result->ai_socktype, result->ai_protocol);
    if (sock == INVALID_SOCKET) {
        threadSafePrint("Failed to create socket");
        freeaddrinfo(result);
        WSACleanup();
        return 1;
    }

    if (connect(sock, result->ai_addr, static_cast<int>(result->ai_addrlen)) == SOCKET_ERROR) {
        threadSafePrint("Failed to connect to pool");
        closesocket(sock);
        freeaddrinfo(result);
        WSACleanup();
        return 1;
    }

    freeaddrinfo(result);
    threadSafePrint("Connected to pool.");

    // Set socket to non-blocking mode
    u_long mode = 1;
    if (ioctlsocket(sock, FIONBIO, &mode) != 0) {
        threadSafePrint("Failed to set socket to non-blocking mode");
        closesocket(sock);
        WSACleanup();
        return 1;
    }

    globalSocket = sock;
    
    // Send login request
    picojson::object loginParams;
    loginParams["login"] = picojson::value(config.walletAddress);
    loginParams["pass"] = picojson::value(config.password);
    loginParams["agent"] = picojson::value(config.userAgent);
    loginParams["worker"] = picojson::value(config.workerName);

    picojson::object loginRequest;
    loginRequest["method"] = picojson::value("login");
    loginRequest["params"] = picojson::value(loginParams);
    loginRequest["id"] = picojson::value(static_cast<double>(jsonRpcId++));
    loginRequest["jsonrpc"] = picojson::value("2.0");

    picojson::value loginValue(loginRequest);
    std::string loginPayload = loginValue.serialize() + "\n";
    threadSafePrint("Sending login payload: " + loginPayload);

    std::string response = sendAndReceive(sock, loginPayload);
    threadSafePrint("Received response: " + response);

    handleLoginResponse(response);

    // Start job listener thread
    threadSafePrint("Starting job listener thread...");
    std::thread listenerThread(listenForNewJobs, sock);
    
    // Start global stats monitor thread
    std::thread statsThread(globalStatsMonitor);
    
    // Start mining threads
    threadSafePrint("Starting mining threads...");
    for (auto& thread : minerThreads) {
        thread.join();
    }
    statsThread.join();
    listenerThread.join();
    
    // Cleanup
    for (auto* data : threadData) {
        data->~MiningThreadData();
        _aligned_free(data);
    }
    threadData.clear();
    
    closesocket(sock);
    WSACleanup();
    
    // Close log file before exit
    if (config.useLogFile && logFile.is_open()) {
        logFile.close();
    }
    
    return 0;
}

// Update hexToBytes to use standard vector for compatibility
std::vector<uint8_t> hexToBytes(const std::string& hex) {
    std::vector<uint8_t> bytes;
    bytes.reserve(hex.length() / 2);

    // Convert each pair of hex characters to a byte
    for (size_t i = 0; i < hex.length(); i += 2) {
        if (i + 1 >= hex.length()) {
            throw std::runtime_error("Invalid hex string length");
        }

        std::string byteString = hex.substr(i, 2);
        uint8_t byte = static_cast<uint8_t>(std::stoul(byteString, nullptr, 16));
        bytes.push_back(byte);
    }

    return bytes;
}

// Update incrementNonce to use little-endian format and reduce debug output
void incrementNonce(std::vector<uint8_t>& blob, uint32_t nonce) {
    // Write in little-endian format (Monero standard)
    for (int i = 0; i < 4; i++) {
        blob[39 + i] = static_cast<uint8_t>((nonce >> (8 * i)) & 0xFF);
    }

    static std::atomic<uint64_t> nonceCounter(0);
    uint64_t currentCount = ++nonceCounter;
    if (debugMode && (currentCount == 1 || currentCount % 10000 == 0)) {
        std::stringstream ss;
        ss << "Incremented nonce to: 0x" << std::hex << std::setw(8) << std::setfill('0') << nonce;
        threadSafePrint(ss.str(), true);
    }
}

// Create submit payload for shares
std::string createSubmitPayload(const std::string& sessionId, const std::string& jobId, 
                              const std::string& nonceHex, const std::string& hashHex, 
                              const std::string& algo) {
    std::ostringstream oss;
    oss << "{\"method\":\"submit\",\"params\":{\"id\":\"" << sessionId 
        << "\",\"job_id\":\"" << jobId 
        << "\",\"nonce\":\"" << nonceHex 
        << "\",\"result\":\"" << hashHex 
        << "\"},\"id\":" << jsonRpcId++ 
        << ",\"algo\":\"" << algo << "\"}";
    return oss.str();
}

// Send and receive data over socket with enhanced logging
std::string sendAndReceive(SOCKET sock, const std::string& payload) {
    if (sock == INVALID_SOCKET) {
        throw std::runtime_error("Invalid socket");
    }

    // Log JSON communication in debug mode
    if (debugMode) {
        threadSafePrint("Pool send: " + payload);
    }
    
    // Send the payload
    size_t totalSent = 0;
    size_t payloadLength = payload.length();
    std::string payloadWithNewline = payload + "\n";
    
    // Set send timeout
    struct timeval sendTimeout;
    sendTimeout.tv_sec = 10;  // 10 seconds timeout
    sendTimeout.tv_usec = 0;
    setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, (char*)&sendTimeout, sizeof(sendTimeout));
    
    while (totalSent < payloadLength + 1) {
        // Calculate remaining bytes and ensure we don't exceed INT_MAX
        size_t remainingBytes = payloadLength + 1 - totalSent;
        int bytesToSend = (remainingBytes > INT_MAX) ? INT_MAX : static_cast<int>(remainingBytes);
        
        int sent = send(sock, payloadWithNewline.c_str() + totalSent, bytesToSend, 0);
        if (sent == SOCKET_ERROR) {
            int error = WSAGetLastError();
            if (error == WSAEWOULDBLOCK) {
                // Socket would block, wait a bit and retry
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                continue;
            }
            throw std::runtime_error("Failed to send data: " + std::to_string(error));
        }
        totalSent += sent;
    }

    // Set receive timeout
    struct timeval recvTimeout;
    recvTimeout.tv_sec = 30;  // 30 seconds timeout
    recvTimeout.tv_usec = 0;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (char*)&recvTimeout, sizeof(recvTimeout));

    // Receive response
    std::vector<char> buffer(4096);
    std::string response;
    int bytesReceived;
    
    auto startTime = std::chrono::steady_clock::now();
    const auto timeout = std::chrono::seconds(30);  // 30 seconds total timeout
    
    do {
        bytesReceived = recv(sock, buffer.data(), static_cast<int>(buffer.size()), 0);
        if (bytesReceived == SOCKET_ERROR) {
            int error = WSAGetLastError();
            if (error == WSAEWOULDBLOCK) {
                // Check if we've exceeded the total timeout
                if (std::chrono::steady_clock::now() - startTime > timeout) {
                    throw std::runtime_error("Receive operation timed out");
                }
                // Socket would block, wait a bit and retry
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                continue;
            }
            throw std::runtime_error("Failed to receive data: " + std::to_string(error));
        }
        if (bytesReceived == 0) {
            // Connection closed by server
            if (response.empty()) {
                throw std::runtime_error("Connection closed by server before receiving data");
            }
            break;
        }
        response.append(buffer.data(), bytesReceived);
        
        // Check for timeout
        if (std::chrono::steady_clock::now() - startTime > timeout) {
            throw std::runtime_error("Receive operation timed out");
        }
    } while (response.find('\n') == std::string::npos);

    // Log pool response in debug mode
    if (debugMode) {
        threadSafePrint("Pool recv: " + response);
        
        // Parse and log relevant job details if present
        try {
            picojson::value v;
            std::string err = picojson::parse(v, response);
            if (err.empty() && v.is<picojson::object>()) {
                const auto& obj = v.get<picojson::object>();
                if (obj.count("result") && obj.at("result").is<picojson::object>()) {
                    const auto& result = obj.at("result").get<picojson::object>();
                    if (result.count("job") && result.at("job").is<picojson::object>()) {
                        const auto& job = result.at("job").get<picojson::object>();
                        std::string details = "New job details:";
                        if (job.count("height")) details += " Height=" + job.at("height").to_str();
                        if (job.count("job_id")) details += " JobID=" + job.at("job_id").to_str();
                        if (job.count("target")) details += " Target=" + job.at("target").to_str();
                        threadSafePrint(details);
                    }
                }
            }
        } catch (const std::exception&) {
            // Ignore parsing errors in debug output
        }
    }
    
    return response;
}

// Handle login response from pool
void handleLoginResponse(const std::string& response) {
    // Parse response to get session ID
    size_t pos = response.find("\"id\":\"");
    if (pos == std::string::npos) {
        throw std::runtime_error("Invalid login response: no session ID found");
    }

    pos += 6;  // Skip "\"id\":\""
    size_t endPos = response.find("\"", pos);
    if (endPos == std::string::npos) {
        throw std::runtime_error("Invalid login response: malformed session ID");
    }

    sessionId = response.substr(pos, endPos - pos);
    if (debugMode) {
        threadSafePrint("Session ID: " + sessionId);
    }
}

// Mining thread function with improved share handling
void miningThread(MiningThreadData* data) {
    try {
        // Wait for dataset to be initialized
        while (!currentDataset && !shouldStop) {
            if (!showedInitMessage.exchange(true)) {
                threadSafePrint("Waiting for dataset initialization...");
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }

        if (shouldStop) return;

        // Initialize VM with proper flags
        if (!data->initializeVM()) {
            threadSafePrint("Failed to initialize VM for thread " + std::to_string(data->getThreadId()));
            return;
        }

        std::vector<uint8_t> hash(RANDOMX_HASH_SIZE);
        uint64_t hashCount = 0;
        uint64_t totalHashCount = 0;
        auto lastHashrateUpdate = std::chrono::steady_clock::now();
        uint32_t debugCounter = 1;
        
        if (debugMode) {
            threadSafePrint("Thread " + std::to_string(data->getThreadId()) + 
                          " using batch size: " + std::to_string(MiningThreadData::BATCH_SIZE));
        }

        // Calculate nonce range for this thread
        uint32_t threadCount = static_cast<uint32_t>(threadData.size());
        uint64_t totalSpace = static_cast<uint64_t>(0xFFFFFFFF);
        uint64_t spacePerThread = totalSpace / threadCount;
        uint64_t startNonce = data->getThreadId() * spacePerThread;
        uint64_t endNonce = (data->getThreadId() == threadCount - 1) ? totalSpace : (startNonce + spacePerThread - 1);

        if (debugMode) {
            std::stringstream ss;
            ss << "Thread " << data->getThreadId() << " assigned nonce range: 0x" 
               << std::hex << std::setw(8) << std::setfill('0') << startNonce 
               << " to 0x" << std::hex << std::setw(8) << std::setfill('0') << endNonce;
            threadSafePrint(ss.str(), true);
        }

        uint32_t currentJobIdSnapshot = 0;
        uint64_t currentDifficulty = 0;
        uint32_t currentNonce = static_cast<uint32_t>(startNonce);

        while (!shouldStop) {
            std::unique_lock<std::mutex> lock(jobQueueMutex);
            jobQueueCV.wait_for(lock, std::chrono::milliseconds(100), [] { 
                return !jobQueue.empty() || shouldStop; 
            });
            
            if (shouldStop) break;
            if (jobQueue.empty()) continue;

            Job job = jobQueue.front();
            currentJobIdSnapshot = activeJobId.load();
            currentDifficulty = getTargetDifficulty(job.target);
            lock.unlock();

            if (debugMode) {
                threadSafePrint("Thread " + std::to_string(data->getThreadId()) + 
                              " mining with difficulty: " + std::to_string(currentDifficulty));
            }

            bool foundShare = false;

            while (!shouldStop && currentJobIdSnapshot == activeJobId.load() && 
                   currentNonce <= static_cast<uint32_t>(endNonce)) {
                // Process hashes in batches
                for (unsigned int i = 0; i < MiningThreadData::BATCH_SIZE && 
                     currentNonce <= static_cast<uint32_t>(endNonce); i++) {
                    // Update nonce in job blob (little-endian)
                    incrementNonce(job.blob, currentNonce);
                    
                    if (!data->calculateHash(job.blob, hash.data(), debugCounter++)) {
                        threadSafePrint("Hash calculation failed for thread " + std::to_string(data->getThreadId()));
                        break;
                    }

                    totalHashes++;
                    hashCount++;

                    // Check hash against target
                    if (isHashValid(hash, job.target)) {
                        if (currentJobIdSnapshot == activeJobId.load()) {
                            std::string nonceHex = bytesToHex(std::vector<uint8_t>(job.blob.begin() + 39, job.blob.begin() + 43));
                            std::string hashHex = bytesToHex(hash);
                            
                            if (debugMode) {
                                std::stringstream ss;
                                ss << "\nValid share found by thread " << data->getThreadId() << ":"
                                   << "\n  Job ID: " << job.jobId
                                   << "\n  Height: " << job.height
                                   << "\n  Nonce: 0x" << nonceHex
                                   << "\n  Hash: " << hashHex
                                   << "\n  Target: " << job.target
                                   << "\n  Difficulty: " << currentDifficulty;
                                threadSafePrint(ss.str(), true);
                            }
                            
                            if (submitShare(job.jobId, nonceHex, hashHex, "rx/0")) {
                                acceptedShares++;
                                foundShare = true;
                            } else {
                                rejectedShares++;
                            }
                        }
                    }

                    // Increment nonce by 1
                    currentNonce++;
                }

                // Update stats every second
                auto now = std::chrono::steady_clock::now();
                auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - lastHashrateUpdate);
                if (elapsed.count() >= 1) {
                    updateThreadStats(data, hashCount, totalHashCount, static_cast<int>(elapsed.count()), 
                                   job.jobId, currentNonce);
                    hashCount = 0;
                    lastHashrateUpdate = now;
                }

                // Check for new jobs
                if (newJobAvailable.load() || foundShare) {
                    break;
                }
            }

            // If we've reached the end of our nonce range, wrap around to start
            if (currentNonce > static_cast<uint32_t>(endNonce)) {
                currentNonce = static_cast<uint32_t>(startNonce);
            }
        }
    }
    catch (const std::exception& e) {
        threadSafePrint("Exception in mining thread " + std::to_string(data->getThreadId()) + 
                       ": " + std::string(e.what()));
    }
}

// Update mining statistics
void updateHashrate(MiningStats& stats) {
    std::lock_guard<std::mutex> lock(stats.statsMutex);
    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - stats.startTime);
    stats.runtime = static_cast<uint64_t>(elapsed.count());  // Fix conversion warning
    if (stats.runtime > 0) {
        stats.currentHashrate = stats.totalHashes / stats.runtime;
    }
}

// Format runtime into human readable string
std::string formatRuntime(uint64_t seconds) {
    uint64_t hours = seconds / 3600;
    seconds %= 3600;
    uint64_t minutes = seconds / 60;
    seconds %= 60;

    std::ostringstream oss;
    oss << std::setfill('0') << std::setw(2) << hours << ":"
        << std::setfill('0') << std::setw(2) << minutes << ":"
        << std::setfill('0') << std::setw(2) << seconds;
    return oss.str();
}

// Main function signal handler
void signalHandler(int signum) {
    threadSafePrint("Received signal " + std::to_string(signum) + ", shutting down...");
    shouldStop = true;
}

// Thread-safe print function with improved formatting and file logging
void threadSafePrint(const std::string& message, bool debugOnly) {
    if (debugOnly && !debugMode) {
        return;
    }
    
    auto now = std::chrono::system_clock::now();
    auto now_time = std::chrono::system_clock::to_time_t(now);
    char timestamp[32];
    struct tm timeinfo;
    localtime_s(&timeinfo, &now_time);
    
    std::istringstream iss(message);
    std::string line;
    bool firstLine = true;

    // Format all lines first
    std::vector<std::string> formattedLines;
    while (std::getline(iss, line)) {
        std::strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", &timeinfo);
        std::string formattedLine = "[" + std::string(timestamp) + "] " + line;
        formattedLines.push_back(formattedLine);
        
        if (!firstLine) {
            now = std::chrono::system_clock::now();
            now_time = std::chrono::system_clock::to_time_t(now);
            localtime_s(&timeinfo, &now_time);
        }
        firstLine = false;
    }

    // Write to console
    {
        std::lock_guard<std::mutex> consoleLock(consoleMutex);
        for (const auto& formattedLine : formattedLines) {
            std::cout << formattedLine << std::endl;
        }
    }

    // Write to log file if enabled
    if (config.useLogFile) {
        std::lock_guard<std::mutex> logLock(logfileMutex);
        for (const auto& formattedLine : formattedLines) {
            logFile << formattedLine << std::endl;
        }
        logFile.flush();
    }
} 