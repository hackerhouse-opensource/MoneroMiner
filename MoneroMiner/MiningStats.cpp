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

void updateThreadStats(MiningThreadData* thread) {
    // Update individual thread statistics
    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - globalStats.startTime).count();
    thread->setElapsedSeconds(static_cast<int>(elapsed));
}

void globalStatsMonitor() {
    while (!PoolClient::shouldStop) {
        std::this_thread::sleep_for(std::chrono::seconds(10));
        
        // Calculate total hashes and hashrate
        uint64_t totalHashes = 0;
        double totalHashrate = 0.0;
        int activeThreads = 0;
        
        std::stringstream ss;
        ss << "Global Stats: ";
        
        // Count active threads and calculate total hashrate
        for (const auto& data : threadData) {
            if (data && data->isRunning) {
                activeThreads++;
                totalHashes += data->getTotalHashCount();
                totalHashrate += data->getHashrate();
            }
        }
        
        // Format global stats
        ss << activeThreads << "/" << threadData.size() << " threads active | ";
        ss << "HR: " << std::fixed << std::setprecision(1) << totalHashrate << " H/s | ";
        ss << "Shares: " << globalStats.acceptedShares << "/" << globalStats.rejectedShares << " | ";
        ss << "Hashes: 0x" << std::hex << totalHashes;
        
        threadSafePrint(ss.str());
        
        // Print thread summary
        threadSafePrint("Thread Summary:");
        for (size_t i = 0; i < threadData.size(); i++) {
            if (i % 4 == 0) {
                ss.str("");
                ss.clear();
            }
            if (threadData[i]) {
                ss << "T" << std::setw(2) << std::setfill('0') << i << ": " 
                   << std::fixed << std::setprecision(1) << std::setw(7) 
                   << threadData[i]->getHashrate() << " H/s  ";
            }
            if (i % 4 == 3 || i == threadData.size() - 1) {
                threadSafePrint(ss.str());
            }
        }
    }
}

void initializeStats() {
    globalStats.startTime = std::chrono::steady_clock::now();
    globalStats.totalHashes = 0;
    globalStats.acceptedShares = 0;
    globalStats.rejectedShares = 0;
    globalStats.currentHashrate = 0.0;
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
    ++globalStats.acceptedShares;
}

void incrementRejectedShares() {
    ++globalStats.rejectedShares;
}

} 