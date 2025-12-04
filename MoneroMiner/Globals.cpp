#include "Globals.h"
#include "Config.h"
#include "MiningThreadData.h"
#include "Job.h"
#include "randomx.h"  // This defines the actual RandomX types
#include <fstream>
#include <queue>
#include <sstream>
#include <iomanip>
#include <chrono>
#include <iostream>

// Define global variables
bool debugMode = false;
std::atomic<bool> shouldStop(false);
std::atomic<bool> showedInitMessage(false);
Config config;  // Define the global config instance
std::ofstream logFile;
std::mutex consoleMutex;
std::mutex logfileMutex;
std::mutex jobMutex;
std::mutex jobQueueMutex;
std::condition_variable jobQueueCV;

// Job-related globals
std::queue<Job> jobQueue;
std::string currentBlobHex;
std::string currentTargetHex;
std::string currentJobId;
std::atomic<uint64_t> totalHashes(0);

// Mining statistics
std::atomic<uint32_t> activeJobId(0);
std::atomic<uint32_t> notifiedJobId(0);
std::atomic<bool> newJobAvailable(false);
std::atomic<uint64_t> acceptedShares(0);
std::atomic<uint64_t> rejectedShares(0);
std::atomic<uint64_t> jsonRpcId(0);
std::string sessionId;
std::vector<MiningThreadData*> threadData;

// RandomX globals
randomx_cache* currentCache = nullptr;
randomx_dataset* currentDataset = nullptr;
std::string currentSeedHash;
std::mutex cacheMutex;
std::mutex seedHashMutex;

// Utility functions
std::string bytesToHex(const std::vector<uint8_t>& bytes) {
    std::stringstream ss;
    ss << std::hex << std::setfill('0');
    for (uint8_t byte : bytes) {
        ss << std::setw(2) << static_cast<int>(byte) << " ";
    }
    return ss.str();
}

std::string getCurrentTimestamp() {
    auto now = std::chrono::system_clock::now();
    auto now_c = std::chrono::system_clock::to_time_t(now);
    char buffer[26];
    ctime_s(buffer, sizeof(buffer), &now_c);
    std::string timestamp(buffer);
    timestamp = timestamp.substr(0, timestamp.length() - 1); // Remove newline
    return timestamp;
}