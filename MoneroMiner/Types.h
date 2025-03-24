#pragma once

#include <string>
#include <vector>
#include <atomic>
#include <chrono>
#include <mutex>

// Forward declaration of formatHashrate function
std::string formatHashrate(double hashrate);

// Global statistics structure
struct GlobalStats {
    std::atomic<uint64_t> totalHashes{0};
    std::atomic<uint64_t> acceptedShares{0};
    std::atomic<uint64_t> rejectedShares{0};
    std::atomic<uint64_t> totalShares{0};
    std::atomic<double> currentHashrate{0.0};
    std::atomic<int> elapsedSeconds{0};
    std::string currentJobId;
    std::atomic<uint32_t> currentNonce{0};
    std::chrono::steady_clock::time_point startTime;
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
        numThreads = std::thread::hardware_concurrency();
        if (numThreads == 0) {
            numThreads = 4;
        }
    }
}; 