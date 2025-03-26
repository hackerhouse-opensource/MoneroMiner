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
#include <unordered_map>

namespace MiningStats {
    extern std::atomic<bool> shouldStop;
    extern std::vector<std::unique_ptr<ThreadMiningStats>> threadStats;
    extern GlobalStats globalStats;
    extern std::mutex statsMutex;
    extern std::vector<MiningThreadData*> threadData;

    void initializeStats(const Config& config);
    void updateThreadStats(MiningThreadData* data, uint64_t hashCount, uint64_t totalHashCount,
                          int elapsedSeconds, const std::string& jobId, uint32_t currentNonce);
    void globalStatsMonitor();
    void stopStatsMonitor();
    void updateHashCount(int threadId, uint64_t count);
    uint64_t getHashCount(int threadId);
    uint64_t getTotalHashes();

    class MiningStats {
    public:
        static void updateHashCount(int threadId, uint64_t count) {
            std::lock_guard<std::mutex> lock(mutex);
            hashCounts[threadId] += count;
            totalHashes += count;
        }

        static uint64_t getHashCount(int threadId) {
            std::lock_guard<std::mutex> lock(mutex);
            return hashCounts[threadId];
        }

        static uint64_t getTotalHashes() {
            std::lock_guard<std::mutex> lock(mutex);
            return totalHashes;
        }

    private:
        static std::mutex mutex;
        static std::unordered_map<int, uint64_t> hashCounts;
        static uint64_t totalHashes;
    };
} 