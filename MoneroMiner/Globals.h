#pragma once

#include <string>
#include <fstream>
#include <mutex>
#include <atomic>
#include <vector>
#include <condition_variable>
#include <queue>
#include "Config.h"
#include "Job.h"
#include "MiningThreadData.h"
#include "Types.h"
#include "randomx.h"

// Forward declaration
class MiningThreadData;

// Global variables
extern bool debugMode;
extern std::atomic<bool> shouldStop;
extern std::atomic<bool> showedInitMessage;
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
extern std::atomic<uint32_t> activeJobId;
extern std::atomic<uint32_t> notifiedJobId;
extern std::atomic<bool> newJobAvailable;
extern std::atomic<uint64_t> acceptedShares;
extern std::atomic<uint64_t> rejectedShares;
extern std::atomic<uint64_t> jsonRpcId;
extern std::string sessionId;
extern std::vector<MiningThreadData*> threadData;

// Global configuration and stats
extern GlobalStats globalStats;

// RandomX globals
extern randomx_cache* currentCache;
extern randomx_dataset* currentDataset;
extern std::string currentSeedHash;
extern std::mutex cacheMutex;
extern std::mutex seedHashMutex;

// Utility functions
std::string bytesToHex(const std::vector<uint8_t>& bytes);
std::string getCurrentTimestamp();
void threadSafePrint(const std::string& message, bool toLogFile); 