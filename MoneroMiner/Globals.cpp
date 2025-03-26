#include "Globals.h"
#include "Config.h"
#include "Job.h"
#include "MiningThreadData.h"
#include "Types.h"
#include <queue>

// Global variables definitions
bool debugMode = false;
std::atomic<bool> shouldStop(false);
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
Config config;
GlobalStats globalStats;

// RandomX globals
randomx_cache* currentCache = nullptr;
randomx_dataset* currentDataset = nullptr;
std::string currentSeedHash;
std::mutex cacheMutex;
std::mutex seedHashMutex; 