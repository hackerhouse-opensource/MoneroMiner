#pragma once

#include "Types.h"
#include "Constants.h"
#include "HashBuffers.h"
#include "MiningThreadData.h"
#include "Job.h"
#include "Config.h"
#include <WinSock2.h>
#include <WS2tcpip.h>
#include <queue>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <fstream>
#include "picojson.h"
#include <vector>

// Global variables declarations
extern std::mutex jobMutex;
extern std::mutex jobQueueMutex;
extern std::condition_variable jobQueueCV;
extern std::queue<Job> jobQueue;
extern std::string currentBlobHex;
extern std::string currentTargetHex;
extern std::string currentJobId;
extern int jsonRpcId;
extern std::string sessionId;
extern std::atomic<bool> shouldStop;
extern std::atomic<uint64_t> totalHashes;
extern std::atomic<uint64_t> acceptedShares;
extern std::atomic<uint64_t> rejectedShares;
extern SOCKET globalSocket;
extern std::mutex consoleMutex;
extern std::mutex logfileMutex;
extern std::ofstream logFile;
extern bool debugMode;
extern std::atomic<uint32_t> debugHashCounter;
extern std::atomic<bool> newJobAvailable;
extern std::atomic<bool> showedInitMessage;
extern std::atomic<uint32_t> activeJobId;
extern std::atomic<uint32_t> notifiedJobId;
extern std::vector<MiningThreadData*> threadData;
extern GlobalStats globalStats;

// Function declarations
void signalHandler(int signum);
void miningThread(MiningThreadData* data);
void listenForNewJobs(SOCKET sock);
void processNewJob(const picojson::object& jobObj);
void handleLoginResponse(const std::string& response);
void handleShareResponse(const std::string& response, bool& accepted);
bool submitShare(const std::string& jobId, const std::string& nonce, const std::string& hash, const std::string& algo);
std::string sendAndReceive(SOCKET sock, const std::string& payload);
std::string createSubmitPayload(const std::string& sessionId, const std::string& jobId, 
                              const std::string& nonceHex, const std::string& hashHex, 
                              const std::string& algo);
void updateThreadStats(MiningThreadData* data, uint64_t hashCount, uint64_t totalHashCount,
                      int elapsedSeconds, const std::string& jobId, uint32_t currentNonce);
void globalStatsMonitor();
bool parseCommandLine(int argc, char* argv[]);
void printHelp(); 