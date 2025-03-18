#include "MiningStats.h"
#include "Config.h"
#include "MiningThreadData.h"
#include "Globals.h"
#include "Utils.h"
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
    std::vector<MiningThreadData*> threadData;

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
            std::this_thread::sleep_for(std::chrono::seconds(5));
            
            std::lock_guard<std::mutex> lock(statsMutex);
            
            // Update global stats from all threads
            uint64_t totalHashes = 0;
            uint64_t totalAcceptedShares = 0;
            uint64_t totalRejectedShares = 0;
            double totalHashrate = 0.0;
            
            for (const auto& data : threadData) {
                if (data) {
                    totalHashes += data->getTotalHashCount();
                    totalAcceptedShares += data->acceptedShares;
                    totalRejectedShares += data->rejectedShares;
                    totalHashrate += data->getHashrate();
                }
            }
            
            // Print global stats
            std::stringstream ss;
            ss << "Global Hash Rate: " << std::fixed << std::setprecision(2) 
               << (totalHashrate / 1000.0) << " kH/s | "
               << "Shares: " << totalAcceptedShares << "/" << totalRejectedShares 
               << " | Total Hashes: " << totalHashes << std::endl;
            
            // Print individual thread stats
            for (const auto& data : threadData) {
                if (data) {
                    ss << "Thread " << data->getThreadId() 
                       << " Hash Rate: " << std::fixed << std::setprecision(2) 
                       << (data->getHashrate() / 1000.0) << " kH/s | "
                       << "Hashes: " << data->getTotalHashCount() 
                       << " | Shares: " << data->acceptedShares << "/" 
                       << data->rejectedShares << std::endl;
                }
            }
            
            threadSafePrint(ss.str(), true);
        }
    }

    void stopStatsMonitor() {
        shouldStop = true;
    }
} 