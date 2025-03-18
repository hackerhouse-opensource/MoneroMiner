#include "MiningStats.h"
#include "Utils.h"
#include "PoolClient.h"
#include <sstream>
#include <iomanip>
#include <thread>
#include <chrono>

namespace MiningStats {

std::mutex statsMutex;
std::vector<MiningThreadData*> threadData;
GlobalStats globalStats;
MinerConfig config;
std::atomic<bool> shouldStop;
std::chrono::steady_clock::time_point lastUpdate;
double currentHashrate;

void updateThreadStats(MiningThreadData* thread) {
    std::lock_guard<std::mutex> lock(statsMutex);
    if (thread && thread->getId() < threadData.size()) {
        threadData[thread->getId()] = thread;
        thread->isRunning = true;
    }
}

void globalStatsMonitor() {
    while (!shouldStop) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
        
        std::lock_guard<std::mutex> lock(statsMutex);
        
        // Calculate total hashrate and active threads
        uint64_t totalHashes = 0;
        int activeThreads = 0;
        uint64_t totalShares = 0;
        uint64_t acceptedShares = 0;
        uint64_t rejectedShares = 0;
        double totalHashrate = 0.0;
        
        for (const auto& data : threadData) {
            if (data && data->isRunning) {
                totalHashes += data->hashes;
                activeThreads++;
                totalShares += data->shares;
                acceptedShares += data->acceptedShares;
                rejectedShares += data->rejectedShares;
                
                // Calculate thread hashrate
                auto now = std::chrono::steady_clock::now();
                auto duration = std::chrono::duration_cast<std::chrono::seconds>(now - data->lastUpdate).count();
                if (duration > 0) {
                    totalHashrate += static_cast<double>(data->hashes) / duration;
                }
            }
        }
        
        // Update global stats
        globalStats.totalHashes = totalHashes;
        globalStats.currentHashrate = totalHashrate;
        globalStats.totalShares = totalShares;
        globalStats.acceptedShares = acceptedShares;
        globalStats.rejectedShares = rejectedShares;
        
        // Print stats
        std::stringstream ss;
        ss << "\nGlobal Stats: ";
        ss << "Threads: " << activeThreads << "/" << config.numThreads << " | ";
        ss << "Hashrate: " << std::fixed << std::setprecision(2) << totalHashrate << " H/s | ";
        ss << "Shares: " << acceptedShares << "/" << totalShares << " | ";
        ss << "Rejected: " << rejectedShares << " | ";
        ss << "Total Hashes: 0x" << std::hex << totalHashes;
        threadSafePrint(ss.str(), true);
        
        lastUpdate = std::chrono::steady_clock::now();
    }
}

void initializeStats(const MinerConfig& cfg) {
    config = cfg;
    globalStats.startTime = std::chrono::steady_clock::now();
    globalStats.totalHashes = 0;
    globalStats.acceptedShares = 0;
    globalStats.rejectedShares = 0;
    globalStats.currentHashrate = 0.0;
    threadData.clear();
    threadData.resize(config.numThreads, nullptr);
}

void updateGlobalHashrate() {
    std::lock_guard<std::mutex> lock(statsMutex);
    uint64_t totalHashes = 0;
    for (const auto* thread : threadData) {
        totalHashes += thread->getHashCount();
    }
    globalStats.totalHashes = totalHashes;

    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - globalStats.startTime).count();
    if (elapsed > 0) {
        globalStats.currentHashrate = static_cast<double>(totalHashes) / elapsed;
    }

    // Print stats every update
    std::stringstream ss;
    ss << "Mining Stats:"
       << "\n  Hashrate: " << std::fixed << std::setprecision(2) << globalStats.currentHashrate << " H/s"
       << "\n  Total Hashes: " << globalStats.totalHashes
       << "\n  Accepted Shares: " << globalStats.acceptedShares
       << "\n  Rejected Shares: " << globalStats.rejectedShares;
    threadSafePrint(ss.str());
}

void incrementAcceptedShares() {
    std::lock_guard<std::mutex> lock(statsMutex);
    ++globalStats.acceptedShares;
}

void incrementRejectedShares() {
    std::lock_guard<std::mutex> lock(statsMutex);
    ++globalStats.rejectedShares;
}

void updateGlobalStats(MiningThreadData* data) {
    std::lock_guard<std::mutex> lock(statsMutex);
    
    // Update thread-specific stats
    if (data && data->getId() >= 0 && data->getId() < static_cast<int>(threadData.size())) {
        threadData[data->getId()] = data;
        data->isRunning = true;
    }
    
    // Calculate total hashrate and active threads
    uint64_t totalHashes = 0;
    int activeThreads = 0;
    uint64_t totalShares = 0;
    double totalHashrate = 0.0;
    
    for (const auto& thread : threadData) {
        if (thread && thread->isRunning) {
            totalHashes += thread->hashes;
            activeThreads++;
            totalShares += thread->shares;
            
            // Calculate thread hashrate
            auto now = std::chrono::steady_clock::now();
            auto duration = std::chrono::duration_cast<std::chrono::seconds>(now - thread->lastUpdate).count();
            if (duration > 0) {
                totalHashrate += static_cast<double>(thread->hashes) / duration;
            }
        }
    }
    
    // Update global stats
    globalStats.totalHashes = totalHashes;
    globalStats.currentHashrate = totalHashrate;
    globalStats.totalShares = totalShares;
}

} 