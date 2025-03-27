#pragma once

#include <string>
#include <fstream>
#include <mutex>
#include <atomic>
#include <vector>
#include <condition_variable>

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

// Global variables declarations
extern std::atomic<uint32_t> activeJobId;
extern std::atomic<uint32_t> notifiedJobId;
extern std::atomic<bool> newJobAvailable;
extern std::atomic<uint64_t> acceptedShares;
extern std::atomic<uint64_t> rejectedShares;
extern std::atomic<uint64_t> jsonRpcId;
extern std::string sessionId;
extern std::vector<MiningThreadData*> threadData; 