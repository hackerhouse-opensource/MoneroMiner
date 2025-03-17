#pragma once

#include <atomic>
#include <chrono>
#include <mutex>
#include <vector>
#include "Types.h"
#include "MiningThreadData.h"

namespace MiningStats {
    extern std::mutex statsMutex;
    extern std::vector<MiningThreadData*> threadData;
    extern GlobalStats globalStats;
    extern MinerConfig config;
    extern std::atomic<bool> shouldStop;

    // Function declarations
    void updateThreadStats(MiningThreadData* thread);
    void globalStatsMonitor();
    void initializeStats(const MinerConfig& cfg);
    void updateGlobalHashrate();
    void incrementAcceptedShares();
    void incrementRejectedShares();
} 