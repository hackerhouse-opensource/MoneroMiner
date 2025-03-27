#include "MiningThreadData.h"
#include "HashBuffers.h"
#include "RandomXManager.h"
#include "PoolClient.h"
#include "Utils.h"
#include "Constants.h"
#include "RandomXFlags.h"
#include "HashValidation.h"
#include "MiningStats.h"
#include "Globals.h"
#include "randomx.h"
#include <sstream>
#include <thread>
#include <chrono>
#include <cstring>
#include <iomanip>

// Global variables declared in MoneroMiner.h
extern bool debugMode;
extern std::queue<Job> jobQueue;
extern std::atomic<uint32_t> activeJobId;
extern std::atomic<bool> shouldStop;
extern Config config;

MiningThreadData::~MiningThreadData() {
    stop();
    if (vm) {
        RandomXManager::destroyVM(vm);
        vm = nullptr;
    }
}

bool MiningThreadData::initializeVM() {
    std::lock_guard<std::mutex> lock(vmMutex);
    if (vmInitialized) return true;

    vm = RandomXManager::createVM(threadId);
    if (!vm) return false;

    vmInitialized = true;
    return true;
}

bool MiningThreadData::calculateHash(const std::vector<uint8_t>& blob, uint64_t nonce, uint8_t* output) {
    if (!vmInitialized && !initializeVM()) {
        return false;
    }

    std::lock_guard<std::mutex> lock(vmMutex);
    return RandomXManager::calculateHash(vm, blob, nonce);
}

void MiningThreadData::updateJob(const Job& job) {
    std::lock_guard<std::mutex> lock(jobMutex);
    if (currentJob) {
        delete currentJob;
    }
    currentJob = new Job(job);
    currentNonce = static_cast<uint64_t>(threadId) * (0xFFFFFFFF / config.numThreads);
}

void MiningThreadData::mine() {
    while (!shouldStop) {
        try {
            if (!currentJob) {
                std::unique_lock<std::mutex> lock(PoolClient::jobMutex);
                PoolClient::jobQueueCondition.wait(lock, [&]() {
                    return !PoolClient::jobQueue.empty() || shouldStop;
                });
                
                if (shouldStop) break;
                
                if (PoolClient::jobQueue.empty()) {
                    continue;
                }
                
                updateJob(PoolClient::jobQueue.front());
                PoolClient::jobQueue.pop();
            }

            // Process current job
            std::vector<uint8_t> input = currentJob->getBlob();
            uint64_t currentNonce = currentNonce;
            
            // Calculate hash
            uint8_t output[32];
            if (calculateHash(input, currentNonce, output)) {
                submitShare(output);
            }
            
            // Update nonce
            currentNonce++;
            hashCount++;
            totalHashCount++;
            
            // Print hash rate every 1000 hashes
            if (hashCount % 1000 == 0) {
                threadSafePrint("Thread " + std::to_string(threadId) + 
                    " processed " + std::to_string(hashCount) + " hashes", true);
            }
        }
        catch (const std::exception& e) {
            threadSafePrint("Error in mining thread " + std::to_string(threadId) + 
                ": " + std::string(e.what()), true);
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
    }
}

void MiningThreadData::submitShare(const uint8_t* hash) {
    if (!currentJob) return;
    
    // Convert nonce to hex
    std::stringstream ss;
    ss << std::hex << std::setw(16) << std::setfill('0') << currentNonce;
    std::string nonceHex = ss.str();
    
    // Convert hash to hex
    std::string hashHex;
    for (int i = 0; i < 32; i++) {
        ss.str("");
        ss << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(hash[i]);
        hashHex += ss.str();
    }
    
    // Submit share
    bool accepted = PoolClient::submitShare(currentJob->getJobId(), nonceHex, hashHex, "rx/0");
    if (accepted) {
        acceptedShares++;
    } else {
        rejectedShares++;
    }
}

bool MiningThreadData::needsVMReinit(const std::string& newSeedHash) const {
    return currentSeedHash != newSeedHash;
}

double MiningThreadData::getHashrate() const {
    auto now = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::seconds>(now - startTime).count();
    if (duration == 0) return 0.0;
    return static_cast<double>(totalHashCount) / duration;
}

void MiningThreadData::start() {
    if (running) return;
    running = true;
    thread = std::thread(&MiningThreadData::mine, this);
}

void MiningThreadData::stop() {
    if (!running) return;
    shouldStop = true;
    if (thread.joinable()) {
        thread.join();
    }
    running = false;
} 