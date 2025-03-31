#include "Globals.h"
#include "Config.h"
#include "Job.h"
#include "MiningThreadData.h"
#include "Types.h"
#include <queue>
#include <sstream>
#include <iomanip>
#include <chrono>

// Global variables definitions
bool debugMode = false;
std::atomic<bool> shouldStop(false);
std::atomic<bool> showedInitMessage(false);
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

// Global configuration and stats
GlobalStats globalStats;

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

void threadSafePrint(const std::string& message, bool toLogFile) {
    std::lock_guard<std::mutex> consoleLock(consoleMutex);
    std::cout << message << std::endl;

    if (toLogFile && config.useLogFile) {
        std::lock_guard<std::mutex> logLock(logfileMutex);
        if (logFile.is_open()) {
            logFile << getCurrentTimestamp() << " " << message << std::endl;
        }
    }
} 