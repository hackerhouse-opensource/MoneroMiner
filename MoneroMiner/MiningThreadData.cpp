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

MiningThreadData::MiningThreadData(int threadId) 
    : threadId(threadId), vm(nullptr), hashCount(0), totalHashCount(0), 
      startTime(std::chrono::steady_clock::now()), currentJobId(""), 
      currentNonce(0), acceptedShares(0), rejectedShares(0), elapsedSeconds(0) {
    threadSafePrint("Created mining thread data for thread " + std::to_string(threadId), true);
}

MiningThreadData::~MiningThreadData() {
    if (vm) {
        RandomXManager::destroyVM(vm);
        vm = nullptr;
    }
}

bool MiningThreadData::initializeVM() {
    if (vm) {
        RandomXManager::destroyVM(vm);
        vm = nullptr;
    }

    vm = RandomXManager::createVM(threadId);
    if (!vm) {
        threadSafePrint("Failed to create VM for thread " + std::to_string(threadId), true);
        return false;
    }

    threadSafePrint("VM initialized successfully for thread " + std::to_string(threadId), true);
    return true;
}

bool MiningThreadData::calculateHash(const std::vector<uint8_t>& blob, uint64_t nonce, uint8_t* output) {
    if (!vm) {
        threadSafePrint("Cannot calculate hash: VM is null for thread " + std::to_string(threadId), true);
        return false;
    }

    // Create input buffer with nonce
    std::vector<uint8_t> input = blob;
    if (input.size() >= 39) {  // Ensure we have space for the nonce
        input[39] = (nonce >> 0) & 0xFF;
        input[40] = (nonce >> 8) & 0xFF;
        input[41] = (nonce >> 16) & 0xFF;
        input[42] = (nonce >> 24) & 0xFF;
    }

    // Calculate hash
    randomx_calculate_hash(vm, input.data(), input.size(), output);
    hashCount++;
    totalHashCount++;
    return true;
}

void MiningThreadData::updateJob(const Job& job) {
    std::lock_guard<std::mutex> lock(jobMutex);
    
    // Delete old job if it exists
    if (currentJob) {
        delete currentJob;
    }
    
    // Create new job
    currentJob = new Job(job);
    currentJobId = job.jobId;
    currentNonce = 0;
    currentSeedHash = job.seedHash;
    
    // Reset hash count for new job
    hashCount = 0;
    
    // Check if VM needs reinitialization
    if (needsVMReinit(job.seedHash)) {
        std::lock_guard<std::mutex> vmLock(vmMutex);
        if (vm) {
            RandomXManager::destroyVM(vm);
            vm = nullptr;
        }
        vmInitialized = false;
    }
}

void MiningThreadData::mine() {
    running = true;
    startTime = std::chrono::steady_clock::now();
    lastUpdate = startTime;

    threadSafePrint("Mining thread " + std::to_string(threadId) + " starting...", true);

    while (!shouldStop) {
        // Wait for a job
        threadSafePrint("Thread " + std::to_string(threadId) + " waiting for job...", true);
        std::unique_lock<std::mutex> jobLock(PoolClient::jobMutex);
        
        // Debug job queue state
        threadSafePrint("Thread " + std::to_string(threadId) + 
                       " job queue size: " + std::to_string(PoolClient::jobQueue.size()), true);
        
        PoolClient::jobQueueCondition.wait(jobLock, [this]() { 
            bool hasJob = !PoolClient::jobQueue.empty();
            if (hasJob) {
                threadSafePrint("Thread " + std::to_string(threadId) + 
                              " job queue is not empty, waking up", true);
            }
            return hasJob || shouldStop; 
        });

        if (shouldStop) {
            threadSafePrint("Thread " + std::to_string(threadId) + " stopping...", true);
            break;
        }

        // Get current job
        Job currentJob = PoolClient::jobQueue.front();
        jobLock.unlock();

        threadSafePrint("Thread " + std::to_string(threadId) + 
                       " processing job ID: " + currentJob.getJobId() +
                       "\n  Height: " + std::to_string(currentJob.getHeight()) +
                       "\n  Target: " + currentJob.getTarget() +
                       "\n  Seed Hash: " + currentJob.getSeedHash(), true);

        // Initialize VM with the job's seed hash
        threadSafePrint("Thread " + std::to_string(threadId) + " initializing VM...", true);
        if (!initializeVM()) {
            threadSafePrint("Failed to initialize VM for thread " + std::to_string(threadId), true);
            continue;
        }
        threadSafePrint("Thread " + std::to_string(threadId) + " VM initialized successfully", true);

        // Process the job
        uint64_t hashCount = 0;
        auto startTime = std::chrono::steady_clock::now();

        while (!shouldStop && currentJob.getJobId() == currentJobId) {
            try {
                // Calculate hash for current nonce
                uint8_t hash[32];
                if (!calculateHash(currentJob.getBlob(), currentNonce, hash)) {
                    threadSafePrint("Hash calculation failed for thread " + std::to_string(threadId), true);
                    break;
                }

                hashCount++;
                if (hashCount % 1000 == 0) {
                    auto now = std::chrono::steady_clock::now();
                    auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - startTime).count();
                    if (elapsed > 0) {
                        double hashrate = static_cast<double>(hashCount) / elapsed;
                        threadSafePrint("Thread " + std::to_string(threadId) + 
                                      " hashrate: " + std::to_string(hashrate) + " H/s" +
                                      "\n  Nonce: 0x" + std::to_string(currentNonce), true);
                    }
                }

                // Convert hash to hex string
                std::string hashHex = HashValidation::hashToHex(hash, 32);

                // Check if hash meets target
                if (HashValidation::validateHash(hashHex, currentJob.getTarget())) {
                    threadSafePrint("Thread " + std::to_string(threadId) + " found valid share!" +
                                  "\n  Job ID: " + currentJob.getJobId() +
                                  "\n  Nonce: 0x" + std::to_string(currentNonce) +
                                  "\n  Hash: " + hashHex, true);

                    submitShare(hash);
                }

                // Update nonce and hash count
                currentNonce++;
                totalHashCount++;

                // Update stats every second
                auto now = std::chrono::steady_clock::now();
                auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - startTime).count();
                if (elapsed >= 1) {
                    MiningStats::updateThreadStats(this, hashCount, this->hashCount, 
                                                 static_cast<int>(elapsed), currentJob.getJobId(), 
                                                 static_cast<uint32_t>(currentNonce));
                    hashCount = 0;
                    startTime = now;
                }
            } catch (const std::exception& e) {
                threadSafePrint("Exception in mining loop: " + std::string(e.what()), true);
                break;
            } catch (...) {
                threadSafePrint("Unknown exception in mining loop", true);
                break;
            }
        }
    }

    threadSafePrint("Mining thread " + std::to_string(threadId) + " stopping...", true);
    running = false;
}

void MiningThreadData::submitShare(const uint8_t* hash) {
    std::string hashHex = HashValidation::hashToHex(hash, 32);
    std::string nonceHex = std::to_string(currentNonce);
    
    if (PoolClient::submitShare(currentJobId, nonceHex, hashHex, "rx/0")) {
        acceptedShares++;
    } else {
        rejectedShares++;
    }
    
    shares++;
}

bool MiningThreadData::needsVMReinit(const std::string& newSeedHash) const {
    return currentSeedHash != newSeedHash;
}

double MiningThreadData::getHashrate() const {
    if (elapsedSeconds > 0) {
        return static_cast<double>(hashCount) / elapsedSeconds;
    }
    return 0.0;
}

void MiningThreadData::start() {
    threadSafePrint("Starting mining thread " + std::to_string(threadId) + "...", true);
    try {
        thread = std::thread(&MiningThreadData::mine, this);
        threadSafePrint("Successfully created thread " + std::to_string(threadId), true);
    } catch (const std::exception& e) {
        threadSafePrint("Failed to start thread " + std::to_string(threadId) + ": " + e.what(), true);
    }
}

void MiningThreadData::stop() {
    shouldStop = true;
    if (thread.joinable()) {
        thread.join();
    }
} 