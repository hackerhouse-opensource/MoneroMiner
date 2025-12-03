#include "MiningThreadData.h"
#include "Utils.h"
#include "HashBuffers.h"
#include "RandomXManager.h"
#include "PoolClient.h"
#include "Config.h"
#include "RandomXFlags.h"
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

MiningThreadData::MiningThreadData(int id) : 
    threadId(id), 
    vm(nullptr), 
    isRunning(false), 
    hashCount(0),
    totalHashCount(0),
    currentNonce(0), 
    currentJob(nullptr),
    vmInitialized(false) {
    startTime = std::chrono::steady_clock::now();
    lastUpdate = startTime;
}

MiningThreadData::~MiningThreadData() {
    stop();
    std::lock_guard<std::mutex> lock(vmMutex);
    if (vm) {
        randomx_destroy_vm(vm);
        vm = nullptr;
    }
}

bool MiningThreadData::initializeVM() {
    std::lock_guard<std::mutex> lock(vmMutex);
    if (vm) {
        randomx_destroy_vm(vm);
        vm = nullptr;
    }
    
    if (!RandomXManager::initializeVM(threadId)) {
        Utils::threadSafePrint("Failed to initialize VM for thread " + std::to_string(threadId), true);
        return false;
    }

    vm = RandomXManager::getVM(threadId);
    if (!vm) {
        Utils::threadSafePrint("Failed to get VM for thread " + std::to_string(threadId), true);
        return false;
    }

    vmInitialized = true;
    Utils::threadSafePrint("VM initialized successfully for thread " + std::to_string(threadId), true);
    return true;
}

bool MiningThreadData::calculateHash(const std::vector<uint8_t>& input, uint64_t nonce) {
    if (!vm) {
        if (debugMode) {
            Utils::threadSafePrint("VM not initialized for thread " + std::to_string(threadId), true);
        }
        return false;
    }

    // Validate input before passing to RandomX
    if (input.empty()) {
        if (debugMode) {
            Utils::threadSafePrint("Empty input in thread " + std::to_string(threadId), true);
        }
        return false;
    }

    if (input.size() > 200) {
        Utils::threadSafePrint("Input too large in thread " + std::to_string(threadId) + 
            ": " + std::to_string(input.size()), true);
        return false;
    }

    try {
        // Pass the input vector by const reference - RandomXManager will make its own copy to buffers
        return RandomXManager::calculateHash(vm, input, nonce);
    }
    catch (const std::exception& e) {
        Utils::threadSafePrint("Exception in calculateHash for thread " + std::to_string(threadId) + 
            ": " + std::string(e.what()), true);
        return false;
    }
}

void MiningThreadData::updateJob(const Job& job) {
    std::lock_guard<std::mutex> lock(jobMutex);
    if (currentJob) {
        delete currentJob;
    }
    currentJob = new Job(job);
    jobCondition.notify_one();
}

void MiningThreadData::mine() {
    while (!shouldStop) {
        try {
            // Check if we have a current job
            {
                std::lock_guard<std::mutex> lock(jobMutex);
                if (!currentJob) {
                    // Wait for a new job
                    std::unique_lock<std::mutex> poolLock(PoolClient::jobMutex);
                    PoolClient::jobQueueCondition.wait(poolLock, [&]() {
                        return !PoolClient::jobQueue.empty() || shouldStop;
                    });
                    
                    if (shouldStop) break;
                    
                    if (PoolClient::jobQueue.empty()) {
                        continue;
                    }
                    
                    // Get the next job from the queue
                    Job newJob = PoolClient::jobQueue.front();
                    PoolClient::jobQueue.pop();
                    
                    // Update our current job
                    updateJob(newJob);
                    
                    if (debugMode) {
                        threadSafePrint("Thread " + std::to_string(threadId) + 
                            " received new job: " + currentJob->getJobId(), true);
                    }
                }
            }

            // Process current job
            std::vector<uint8_t> input;
            uint64_t currentNonce;
            std::string currentJobId;
            
            // Get job data under lock
            {
                std::lock_guard<std::mutex> lock(jobMutex);
                if (!currentJob) continue;
                
                input = currentJob->getBlobBytes();
                currentNonce = this->currentNonce;
                currentJobId = currentJob->getJobId();
            }
            
            // Calculate hash
            if (calculateHash(input, currentNonce)) {
                // Check if we still have the same job before submitting
                {
                    std::lock_guard<std::mutex> lock(jobMutex);
                    if (currentJob && currentJob->getJobId() == currentJobId) {
                        submitShare(RandomXManager::getLastHash());
                    }
                }
            }
            
            // Update nonce and stats
            {
                std::lock_guard<std::mutex> lock(jobMutex);
                if (currentJob && currentJob->getJobId() == currentJobId) {
                    this->currentNonce++;
                    hashCount++;
                    totalHashCount++;
                }
            }
            
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

void MiningThreadData::submitShare(const std::vector<uint8_t>& hash) {
    if (!currentJob) return;

    std::string hashHex = Utils::bytesToHex(hash);
    std::string nonceHex = Utils::formatHex(currentNonce, 8);

    if (config.debugMode) {
        Utils::threadSafePrint("Thread " + std::to_string(threadId) + " submitting share:");
        Utils::threadSafePrint("  Job ID: " + currentJob->getJobId());
        Utils::threadSafePrint("  Nonce: " + nonceHex);
        Utils::threadSafePrint("  Hash: " + hashHex);
    }

    bool accepted = PoolClient::submitShare(currentJob->getJobId(), nonceHex, hashHex, "rx/0");
    
    if (accepted) {
        incrementAcceptedShares();
        Utils::threadSafePrint("Share accepted by pool!");
    } else {
        incrementRejectedShares();
        Utils::threadSafePrint("Share rejected by pool");
    }
}

bool MiningThreadData::needsVMReinit(const std::string& newSeedHash) const {
    std::lock_guard<std::mutex> lock(vmMutex);
    if (!vmInitialized || !vm) {
        return true;
    }
    return currentSeedHash != newSeedHash;
}

double MiningThreadData::getHashrate() const {
    auto now = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::seconds>(now - startTime).count();
    if (duration == 0) return 0.0;
    return static_cast<double>(totalHashCount) / duration;
}

void MiningThreadData::start() {
    if (!isRunning) {
        isRunning = true;
        thread = std::thread(&MiningThreadData::mine, this);
    }
}

void MiningThreadData::stop() {
    if (isRunning) {
        isRunning = false;
        jobCondition.notify_all();
        if (thread.joinable()) {
            thread.join();
        }
    }
}