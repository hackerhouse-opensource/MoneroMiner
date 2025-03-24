#include "MoneroMiner.h"
#include "Config.h"
#include "randomx.h"
#include "MiningThreadData.h"
#include <mutex>
#include <condition_variable>
#include <queue>
#include <string>
#include <atomic>
#include <fstream>
#include <vector>
#include <chrono>
#include <iomanip>
#include <sstream>
#include <iostream>
#include <ctime>

// Global variables definitions
std::mutex jobMutex;
std::mutex jobQueueMutex;
std::condition_variable jobQueueCV;
std::queue<Job> jobQueue;
std::string currentBlobHex;
std::string currentTargetHex;
std::string currentJobId;
int jsonRpcId = 1;
std::string sessionId;
std::atomic<bool> shouldStop(false);
std::atomic<uint64_t> totalHashes(0);
std::atomic<uint64_t> acceptedShares(0);
std::atomic<uint64_t> rejectedShares(0);
SOCKET globalSocket = INVALID_SOCKET;
std::mutex consoleMutex;
std::mutex logfileMutex;
std::ofstream logFile;
bool debugMode = false;
std::atomic<uint32_t> debugHashCounter(0);
std::atomic<bool> newJobAvailable(false);
std::atomic<bool> showedInitMessage(false);
std::atomic<uint32_t> activeJobId(0);
std::atomic<uint32_t> notifiedJobId(0);
std::vector<MiningThreadData*> threadData;

// Global configuration and stats
Config config;
GlobalStats globalStats;

// RandomX globals
randomx_cache* currentCache = nullptr;
randomx_dataset* currentDataset = nullptr;
std::string currentSeedHash;
std::mutex cacheMutex;
std::mutex seedHashMutex; 