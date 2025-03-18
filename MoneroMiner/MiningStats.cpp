#include "MiningStats.h"
#include "Config.h"
#include "MiningThreadData.h"
#include <iostream>
#include <iomanip>
#include <sstream>
#include <chrono>
#include <thread>
#include <mutex>
#include <atomic>

namespace MiningStats {
    std::atomic<bool> shouldStop(false);
    std::vector<std::unique_ptr<ThreadMiningStats>> threadStats;
    GlobalStats globalStats;
    std::mutex statsMutex;

    void initializeStats(const Config& config) {
        threadStats.clear();
        threadStats.resize(config.numThreads);
        for (size_t i = 0; i < config.numThreads; ++i) {
            threadStats[i] = std::make_unique<ThreadMiningStats>();
            threadStats[i]->startTime = std::chrono::steady_clock::now();
            threadStats[i]->totalHashes = 0;
            threadStats[i]->acceptedShares = 0;
            threadStats[i]->rejectedShares = 0;
            threadStats[i]->currentHashrate = 0;
            threadStats[i]->runtime = 0;
        }
        globalStats.startTime = std::chrono::steady_clock::now();
    }

    void updateThreadStats(MiningThreadData* threadData) {
        if (!threadData) return;

        std::lock_guard<std::mutex> lock(statsMutex);
        if (threadData->getThreadId() >= threadStats.size()) return;

        auto& stats = threadStats[threadData->getThreadId()];
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - stats->startTime).count();
        
        stats->totalHashes = threadData->getTotalHashCount();
        stats->acceptedShares = threadData->acceptedShares;
        stats->rejectedShares = threadData->rejectedShares;
        stats->currentHashrate = (elapsed > 0) ? stats->totalHashes / elapsed : 0;
        stats->runtime = elapsed;

        // Update global stats
        globalStats.currentHashrate.store(0.0);
        globalStats.totalShares = 0;
        globalStats.acceptedShares = 0;
        globalStats.rejectedShares = 0;

        double totalHashrate = 0.0;
        for (const auto& threadStat : threadStats) {
            totalHashrate += threadStat->currentHashrate;
            globalStats.totalShares += threadStat->acceptedShares + threadStat->rejectedShares;
            globalStats.acceptedShares += threadStat->acceptedShares;
            globalStats.rejectedShares += threadStat->rejectedShares;
        }
        globalStats.currentHashrate.store(totalHashrate);
    }

    void globalStatsMonitor() {
        while (!shouldStop) {
            {
                std::lock_guard<std::mutex> lock(statsMutex);
                auto now = std::chrono::steady_clock::now();
                auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - globalStats.startTime).count();

                double currentHashrate = globalStats.currentHashrate.load();
                std::cout << "\rGlobal Hash Rate: " << std::fixed << std::setprecision(2) 
                         << (currentHashrate / 1000.0) << " kH/s | "
                         << "Shares: " << globalStats.acceptedShares << "/" 
                         << (globalStats.acceptedShares + globalStats.rejectedShares) << " | "
                         << "Total Hashes: " << currentHashrate * elapsed << std::flush;
            }
            std::this_thread::sleep_for(std::chrono::seconds(5));
        }
    }

    void stopStatsMonitor() {
        shouldStop = true;
    }
} 