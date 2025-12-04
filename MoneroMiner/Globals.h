#pragma once

#include <atomic>
#include <mutex>
#include <condition_variable>
#include <string>
#include <queue>
#include <vector>
#include <fstream>

// Forward declarations
class Config;
class MiningThreadData;
class Job;

// Proper forward declaration for RandomX types
struct randomx_cache;
struct randomx_dataset;

// Global atomic variables
extern std::atomic<bool> shouldStop;
extern std::atomic<uint32_t> activeJobId;
extern std::atomic<uint32_t> notifiedJobId;
extern std::atomic<bool> newJobAvailable;
extern std::atomic<uint64_t> jsonRpcId;

// Global variables
extern bool debugMode;
extern Config config;
extern std::ofstream logFile;
extern std::mutex consoleMutex;
extern std::mutex logfileMutex;
extern std::mutex jobMutex;
extern std::mutex jobQueueMutex;
extern std::condition_variable jobQueueCV;

// Job-related globals
extern std::queue<Job> jobQueue;
extern std::string currentBlobHex;
extern std::string currentTargetHex;
extern std::string currentJobId;
extern std::atomic<uint64_t> totalHashes;

// Global variables declarations
extern std::atomic<uint64_t> acceptedShares;
extern std::atomic<uint64_t> rejectedShares;
extern std::string sessionId;
extern std::vector<MiningThreadData*> threadData;

// RandomX globals
extern randomx_cache* currentCache;
extern randomx_dataset* currentDataset;
extern std::string currentSeedHash;
extern std::mutex cacheMutex;
extern std::mutex seedHashMutex;

// Utility functions declarations
// void threadSafePrint(const std::string& message, bool toLogFile = false);
// std::string getCurrentTimestamp();