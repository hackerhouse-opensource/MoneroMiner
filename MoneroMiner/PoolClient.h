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
#include <thread>

namespace PoolClient {
    extern SOCKET poolSocket;
    extern std::mutex jobMutex;
    extern std::queue<Job> jobQueue;
    extern std::condition_variable jobAvailable;
    extern std::atomic<bool> shouldStop;

    bool initialize();
    bool connect(const std::string& address, const std::string& port);
    bool login(const std::string& wallet, const std::string& password, 
               const std::string& worker, const std::string& userAgent);
    void listenForNewJobs(SOCKET socket);
    bool submitShare(const std::string& jobId, const std::string& nonce, 
                    const std::string& hash, const std::string& algo);
    void cleanup();
    
    extern std::string currentSeedHash;
    extern std::string sessionId;
    extern std::string currentTargetHex;
    
    void handleSeedHashChange(const std::string& newSeedHash);
} 