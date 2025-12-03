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
    std::atomic<uint64_t> totalHashes(0);
    std::atomic<uint64_t> acceptedShares{0};
    std::atomic<uint64_t> rejectedShares{0};
    std::atomic<uint64_t> foundShares(0);
    std::chrono::steady_clock::time_point startTime = std::chrono::steady_clock::now();
    double globalHashRate = 0.0;
    uint64_t lastTotalHashes = 0;
    std::chrono::steady_clock::time_point lastUpdate = std::chrono::steady_clock::now();

    std::string formatDuration(int64_t seconds) {
        int64_t hours = seconds / 3600;
        seconds %= 3600;
        int64_t minutes = seconds / 60;
        seconds %= 60;
        
        std::stringstream ss;
        ss << std::setfill('0') << std::setw(2) << hours << ":"
           << std::setfill('0') << std::setw(2) << minutes << ":"
           << std::setfill('0') << std::setw(2) << seconds;
        return ss.str();
    }

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
        auto lastUpdate = std::chrono::steady_clock::now();
        uint64_t lastHashCount = 0;
        
        while (!shouldStop) {
            std::this_thread::sleep_for(std::chrono::seconds(10));
            
            auto now = std::chrono::steady_clock::now();
            auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - lastUpdate).count();
            
            if (elapsed >= 10) {
                // Calculate total hashrate from all threads
                double totalHashrate = 0.0;
                uint64_t totalHashes = 0;
                
                // FIXED: Access the GLOBAL threadData, not local
                for (auto* data : ::threadData) {  // Use global scope
                    if (data) {
                        totalHashrate += data->getHashrate();
                        totalHashes += data->getTotalHashCount();
                    }
                }
                
                // Also calculate from hash delta
                uint64_t hashDelta = totalHashes - lastHashCount;
                double measuredHashrate = 0.0;
                if (elapsed > 0) {
                    measuredHashrate = static_cast<double>(hashDelta) / static_cast<double>(elapsed);
                }
                
                // Use measured if thread hashrates aren't updating
                if (totalHashrate < 1.0 && measuredHashrate > 0) {
                    totalHashrate = measuredHashrate;
                }
                
                double kHashrate = totalHashrate / 1000.0;
                
                uint64_t accepted = acceptedShares.load();
                uint64_t rejected = rejectedShares.load();
                uint64_t total = accepted + rejected;
                
                std::stringstream ss;
                ss << "\n=== MINING STATISTICS ===\n";
                ss << "Global Hash Rate: " << std::fixed << std::setprecision(2) << kHashrate << " kH/s\n";
                ss << "Total Hashes: " << totalHashes << "\n";
                ss << "Shares: " << accepted << " accepted / " << rejected << " rejected";
                if (total > 0) {
                    double acceptRate = (static_cast<double>(accepted) / static_cast<double>(total)) * 100.0;
                    ss << " (" << std::fixed << std::setprecision(1) << acceptRate << "% accept rate)";
                }
                ss << "\n";
                
                ss << "Thread breakdown:\n";
                for (auto* data : ::threadData) {
                    if (data) {
                        double threadKH = data->getHashrate() / 1000.0;
                        ss << "  Thread " << data->getThreadId() << ": " 
                           << std::fixed << std::setprecision(2) << threadKH << " kH/s, "
                           << data->getTotalHashCount() << " hashes\n";
                    }
                }
                
                Utils::threadSafePrint(ss.str(), false);
                
                lastUpdate = now;
                lastHashCount = totalHashes;
            }
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

    void updateStats() {
        static auto lastUpdate = std::chrono::steady_clock::now();
        static uint64_t lastHashCount = totalHashes;
        
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration<double>(now - lastUpdate).count();
        
        if (elapsed >= 1.0) {
            uint64_t currentHashes = totalHashes.load();
            uint64_t hashCount = currentHashes - lastHashCount;
            double hashRate = static_cast<double>(hashCount) / elapsed / 1000.0;
            
            std::stringstream ss;
            ss << "[" << Utils::getCurrentTimestamp() << "] "
               << "Hash Rate: " << std::fixed << std::setprecision(2) << hashRate << " kH/s | "
               << "Shares: " << acceptedShares << "/" << (acceptedShares + rejectedShares) << " | "
               << "Total Hashes: " << currentHashes;
            Utils::threadSafePrint(ss.str(), true);
            
            lastUpdate = now;
            lastHashCount = currentHashes;
            globalHashRate = hashRate;
        }
    }
}