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

// Global definitions
std::vector<std::unique_ptr<ThreadMiningStats>> threadStats;
GlobalMiningStats globalStats;
std::mutex MiningStats::statsMutex;

static std::mutex hashMutex;
static std::unordered_map<int, uint64_t> hashCounts;
static double globalHashRate = 0.0;

namespace MiningStatsUtil {
    // FIX: These must match the atomic type in the header
    std::atomic<uint64_t> acceptedShares{0};
    std::atomic<uint64_t> rejectedShares{0};
    
    void globalStatsMonitor() {
        while (!shouldStop) {
            std::this_thread::sleep_for(std::chrono::seconds(10));
            // Print stats here
        }
    }
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

void initializeThreadStats(int numThreads) {
    threadStats.clear();
    for (int i = 0; i < numThreads; i++) {
        threadStats.push_back(std::make_unique<ThreadMiningStats>(i));
    }
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

void recordAcceptedShare() {
    acceptedShares++;
}

void recordRejectedShare() {
    rejectedShares++;
}

void printStats() {
    // Minimal implementation - just print basic stats
    std::stringstream ss;
    ss << "Hashrate: " << std::fixed << std::setprecision(2) 
       << (getTotalHashes() / 1000.0) << " kH/s | "
       << "Shares: " << acceptedShares << "/" << rejectedShares;
    Utils::threadSafePrint(ss.str(), true);
}
void MiningStats::printCompactStats() {
    // Compact stats display
    uint64_t total = acceptedShares + rejectedShares;
    double efficiency = (total > 0) ? (static_cast<double>(acceptedShares) / total) * 100.0 : 0.0;
    
    std::stringstream ss;
    ss << std::fixed << std::setprecision(2)
       << "Hashrate: " << (getTotalHashes() / 1000.0) << " kH/s, "
       << "Accepted: " << acceptedShares << ", Rejected: " << rejectedShares << " ("
       << efficiency << "%), "
       << "Total Hashes: " << getTotalHashes();
    Utils::threadSafePrint(ss.str(), true);
}