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
#include <unordered_map>

namespace MiningStats {
    std::atomic<bool> shouldStop(false);
    std::vector<std::unique_ptr<ThreadMiningStats>> threadStats;
    GlobalStats globalStats;
    std::mutex statsMutex;
    std::vector<MiningThreadData*> threadData;
    std::mutex hashMutex;
    std::unordered_map<int, uint64_t> hashCounts;
    uint64_t totalHashes = 0;

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

    void updateThreadStats(MiningThreadData* data, uint64_t hashCount, uint64_t totalHashCount,
                          int elapsedSeconds, const std::string& jobId, uint32_t currentNonce) {
        if (!data) return;

        data->incrementHashCount();
        globalStats.totalHashes = totalHashCount;
        globalStats.acceptedShares = data->getAcceptedShares();
        globalStats.rejectedShares = data->getRejectedShares();
        globalStats.elapsedSeconds = elapsedSeconds;
        globalStats.currentJobId = jobId;
        globalStats.currentNonce = currentNonce;
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
                    totalAcceptedShares += data->getAcceptedShares();
                    totalRejectedShares += data->getRejectedShares();
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
                       << " | Shares: " << data->getAcceptedShares() << "/" 
                       << data->getRejectedShares() << std::endl;
                }
            }
            
            threadSafePrint(ss.str(), true);
        }
    }

    void stopStatsMonitor() {
        shouldStop = true;
    }

    void updateHashCount(int threadId, uint64_t count) {
        std::lock_guard<std::mutex> lock(hashMutex);
        hashCounts[threadId] += count;
        totalHashes += count;
    }

    uint64_t getHashCount(int threadId) {
        std::lock_guard<std::mutex> lock(hashMutex);
        return hashCounts[threadId];
    }

    uint64_t getTotalHashes() {
        std::lock_guard<std::mutex> lock(hashMutex);
        return totalHashes;
    }
} 