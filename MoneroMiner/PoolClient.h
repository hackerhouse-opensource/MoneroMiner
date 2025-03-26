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
#include <memory>
#include "MiningThreadData.h"

namespace PoolClient {
    extern SOCKET poolSocket;
    extern std::mutex jobMutex;
    extern std::queue<Job> jobQueue;
    extern std::condition_variable jobAvailable;
    extern std::condition_variable jobQueueCondition;
    extern std::atomic<bool> shouldStop;

    bool initialize();
    bool connect(const std::string& address, const std::string& port);
    bool login(const std::string& wallet, const std::string& password, 
               const std::string& worker, const std::string& userAgent);
    void jobListener();
    bool submitShare(const std::string& jobId, const std::string& nonce, 
                    const std::string& hash, const std::string& algo);
    void cleanup();
    
    extern std::string currentSeedHash;
    extern std::string sessionId;
    extern std::string currentTargetHex;
    
    void handleSeedHashChange(const std::string& newSeedHash);
    void processNewJob(const picojson::object& jobObj);
    bool handleLoginResponse(const std::string& response);
    std::string sendAndReceive(const std::string& payload);
    std::string sendAndReceive(SOCKET sock, const std::string& payload);

    static inline SOCKET getSocket() { return poolSocket; }

    extern std::vector<std::shared_ptr<MiningThreadData>> threadData;
} 