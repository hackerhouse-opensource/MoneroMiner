#pragma once

#include "Config.h"
#include "MiningThreadData.h"
#include "Types.h"
#include <string>
#include <vector>
#include <chrono>
#include <memory>
#include <atomic>
#include <mutex>

namespace MiningStats {
    extern std::atomic<bool> shouldStop;
    extern std::vector<std::unique_ptr<ThreadMiningStats>> threadStats;
    extern GlobalStats globalStats;
    extern std::mutex statsMutex;
    extern std::vector<MiningThreadData*> threadData;

    void initializeStats(const Config& config);
    void updateThreadStats(MiningThreadData* threadData);
    void globalStatsMonitor();
    void stopStatsMonitor();
} 