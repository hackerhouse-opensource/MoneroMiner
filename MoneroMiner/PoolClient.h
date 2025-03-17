#pragma once

#include <WinSock2.h>
#include <WS2tcpip.h>
#include "picojson.h"
#include "Job.h"
#include <string>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <atomic>

namespace PoolClient {
    bool initialize();
    bool connect(const std::string& address, const std::string& port);
    bool login(const std::string& wallet, const std::string& password, 
              const std::string& workerName, const std::string& userAgent);
    void listenForNewJobs(SOCKET sock);
    void processNewJob(const picojson::object& jobObj);
    void handleShareResponse(const std::string& response, bool& accepted);
    bool submitShare(const std::string& jobId, const std::string& nonceHex, 
                    const std::string& hashHex, const std::string& algo);
    void cleanup();
    
    extern SOCKET poolSocket;
    extern std::atomic<bool> shouldStop;
    extern std::queue<Job> jobQueue;
    extern std::mutex jobMutex;
    extern std::condition_variable jobAvailable;
    extern std::string currentSeedHash;
    extern std::string sessionId;
    extern std::string currentTargetHex;
    
    void handleSeedHashChange(const std::string& newSeedHash);
} 