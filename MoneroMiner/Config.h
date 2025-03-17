#pragma once

#include <string>
#include <thread>
#include <atomic>
#include <chrono>
#include <mutex>

// Configuration structure
struct MinerConfig {
    std::string poolAddress = "xmr-eu1.nanopool.org";
    std::string poolPort = "10300";
    std::string walletAddress = "8BghJxGWaE2Ekh8KrrEEqhGMLVnB17cCATNscfEyH8qq9uvrG3WwYPXbvqfx1HqY96ZaF3yVYtcQ2X1KUMNt2Pr29M41jHf";
    std::string workerName = "miniminer";
    std::string password = "x";
    std::string userAgent = "miniminer/1.0.0";
    std::string logFile = "MoneroMiner.log";
    bool useLogFile = false;
    int numThreads;
    bool debugMode = false;

    MinerConfig() {
        // Calculate optimal thread count based on CPU resources
        numThreads = std::thread::hardware_concurrency();
        if (numThreads == 0) {
            numThreads = 4;  // Fallback if hardware_concurrency fails
        }
    }
};

// Global statistics structure
struct GlobalStats {
    std::atomic<uint64_t> totalHashrate{0};
    std::atomic<uint64_t> totalShares{0};
    std::atomic<uint64_t> acceptedShares{0};
    std::atomic<uint64_t> rejectedShares{0};
    std::chrono::steady_clock::time_point startTime = std::chrono::steady_clock::now();
};

// Mining statistics structure for individual threads
struct ThreadMiningStats {
    std::chrono::steady_clock::time_point startTime;
    uint64_t totalHashes;
    uint64_t acceptedShares;
    uint64_t rejectedShares;
    uint64_t currentHashrate;
    uint64_t runtime;
    std::mutex statsMutex;
}; 