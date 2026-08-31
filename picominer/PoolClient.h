#pragma once

#include "Platform.h"  // Use Platform.h instead of WinSock headers
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
    // Use cross-platform socket type
    extern socket_t poolSocket;
    extern std::mutex jobMutex;
    extern std::mutex socketMutex;
    extern std::mutex submitMutex;
    extern std::queue<Job> jobQueue;
    extern std::condition_variable jobAvailable;
    extern std::condition_variable jobQueueCondition;
    extern std::atomic<bool> shouldStop;
    extern std::string currentSeedHash;
    extern std::string sessionId;
    extern std::string currentTargetHex;
    extern std::string poolId;
    extern std::vector<std::shared_ptr<MiningThreadData>> threadData;

    // Core networking functions
    bool initialize();
    bool connect();
    bool login(const std::string& wallet, const std::string& password, 
               const std::string& worker, const std::string& userAgent);
    void cleanup();

    // Job handling
    void jobListener();
    
    // Share submission
    bool submitShare(const std::string& jobId, const std::string& nonceHex,
                    const std::string& hashHex, const std::string& algo);
    
    // Helper functions
    void handleSeedHashChange(const std::string& newSeedHash);
    void processNewJob(const picojson::object& jobObj);
    std::string sendAndReceive(const std::string& payload);
    std::string receiveData(socket_t sock);
    std::string sendData(const std::string& data);
    void distributeJob(const Job& job);
    
    bool reconnect();
    void sendKeepalive();
}