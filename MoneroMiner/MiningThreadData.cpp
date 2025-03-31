#include "MiningThreadData.h"
#include "HashBuffers.h"
#include "RandomXManager.h"
#include "PoolClient.h"
#include "Utils.h"
#include "Constants.h"
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

bool MiningThreadData::calculateHash(const std::vector<uint8_t>& input, uint64_t nonce) {
    if (!vm || input.empty()) {
        return false;
    }

    // Create a copy of the input data
    std::vector<uint8_t> data = input;
    
    // Ensure the input is at least 43 bytes (RandomX block header size)
    if (data.size() < 43) {
        data.resize(43, 0);
    }

    // Insert nonce at the correct position (bytes 39-42)
    data[39] = (nonce >> 24) & 0xFF;
    data[40] = (nonce >> 16) & 0xFF;
    data[41] = (nonce >> 8) & 0xFF;
    data[42] = nonce & 0xFF;

    // Calculate hash
    return RandomXManager::calculateHash(vm, data, nonce);
}

void MiningThreadData::updateJob(const Job& job) {
    std::lock_guard<std::mutex> lock(jobMutex);
    
    // Delete existing job if any
    if (currentJob) {
        delete currentJob;
        currentJob = nullptr;
    }
    
    // Create new job
    currentJob = new Job(job);
    
    // Initialize nonce based on thread ID
    currentNonce = static_cast<uint64_t>(threadId) * (0xFFFFFFFF / config.numThreads);
    
    if (debugMode) {
        threadSafePrint("Thread " + std::to_string(threadId) + 
            " initialized with job: " + currentJob->getJobId() + 
            " starting nonce: " + std::to_string(currentNonce), true);
    }
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
    std::lock_guard<std::mutex> lock(jobMutex);
    
    if (!currentJob) {
        if (debugMode) {
            threadSafePrint("Thread " + std::to_string(threadId) + 
                " attempted to submit share without current job", true);
        }
        return;
    }

    // Convert hash to hex string
    std::stringstream ss;
    for (uint8_t byte : hash) {
        ss << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(byte);
    }
    std::string hashHex = ss.str();

    // Convert height to string
    std::string heightStr = std::to_string(currentJob->getHeight());

    if (debugMode) {
        threadSafePrint("Thread " + std::to_string(threadId) + 
            " submitting share for job: " + currentJob->getJobId() + 
            " nonce: " + std::to_string(currentNonce), true);
    }

    // Submit share to pool
    bool accepted = PoolClient::submitShare(hashHex, currentJob->getJobId(), heightStr, std::to_string(currentNonce));
    
    if (accepted) {
        acceptedShares++;
        threadSafePrint("Share accepted! Hash: " + hashHex + " Nonce: " + std::to_string(currentNonce), true);
    } else {
        rejectedShares++;
        threadSafePrint("Share rejected. Hash: " + hashHex + " Nonce: " + std::to_string(currentNonce), true);
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