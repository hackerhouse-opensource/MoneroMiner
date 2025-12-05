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
    void startJobListener();
    void stopJobListener();
    Job getCurrentJob();
    
    // Share submission
    bool submitShare(const std::string& jobId, const std::string& nonceHex,
                    const std::string& hashHex, const std::string& algo);
    
    // Status getters
    bool isConnected();
    std::string getCurrentTarget();
    uint64_t getCurrentDifficulty();

    void jobListener();
    bool submitShare(const std::string& jobId, const std::string& nonce, 
                    const std::string& hash, const std::string& algo);
    
    void handleSeedHashChange(const std::string& newSeedHash);
    void processNewJob(const picojson::object& jobObj);
    bool handleLoginResponse(const std::string& response);
    std::string sendAndReceive(const std::string& payload);
    std::string receiveData(SOCKET sock);
    std::string sendData(const std::string& data);
    void distributeJob(const Job& job);

    bool submitShare(const std::string& jobId, 
                    const std::string& nonce, 
                    const std::string& result, 
                    const std::string& id);
    bool submitShare(const std::string& jobId, uint64_t nonce, 
                    const std::vector<uint8_t>& hash);  // Add this overload
    bool submitShare(const std::string& jobId, const std::string& nonceHex,
                    const std::string& hashHex, const std::string& algo);
    
    std::string getPoolId();
    bool reconnect();
    void sendKeepalive();  // Add this declaration

    class PoolClient {
    public:
        // ...existing code...
        
        // REMOVE THIS LINE (around line 80):
        // void handleShareResponse(const json& response);
        
        // ...existing code...
    };
}