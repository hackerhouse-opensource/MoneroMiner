#pragma once

#include <atomic>
#include <memory>
#include <vector>
#include <chrono>
#include <mutex>

// Forward declaration
class MiningThreadData;

struct ThreadMiningStats {
    std::atomic<uint64_t> hashCount{0};
    std::atomic<uint64_t> accepted{0};
    std::atomic<uint64_t> rejected{0};
    std::atomic<double> hashrate{0.0};
    int threadId{0};
    
    // ADD THESE MISSING FIELDS:
    std::chrono::steady_clock::time_point startTime;
    uint64_t totalHashes{0};
    uint64_t acceptedShares{0};
    uint64_t rejectedShares{0};
    double currentHashrate{0.0};
    double runtime{0.0};
    
    ThreadMiningStats() : startTime(std::chrono::steady_clock::now()) {}
    ThreadMiningStats(int id) : threadId(id), startTime(std::chrono::steady_clock::now()) {}
};

extern std::vector<std::unique_ptr<ThreadMiningStats>> threadStats;

class GlobalMiningStats {
public:
    // ADD THESE MISSING FIELDS:
    std::chrono::steady_clock::time_point startTime;
    uint64_t totalHashes{0};
    uint64_t acceptedShares{0};
    uint64_t rejectedShares{0};
    double elapsedSeconds{0.0};
    std::string currentJobId;
    uint64_t currentNonce{0};
    
    GlobalMiningStats() : startTime(std::chrono::steady_clock::now()) {}
    
    // ...existing methods...
};

extern GlobalMiningStats globalStats;

class MiningStats {
public:
    static std::mutex statsMutex;
    
    static void initializeThreadStats(int numThreads);
    static void updateGlobalStats();
    static void printStats(GlobalMiningStats& stats);
    static void printCompactStats();  // ADD THIS LINE
    
    // ...existing methods...
};

namespace MiningStatsUtil {
    extern std::atomic<uint64_t> acceptedShares;
    extern std::atomic<uint64_t> rejectedShares;
    
    void globalStatsMonitor();
}