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

MiningThreadData::~MiningThreadData() {
    cleanup();
}

bool MiningThreadData::initializeVM() {
    std::lock_guard<std::mutex> lock(vmMutex);
    if (!vmInitialized) {
        vm = RandomXManager::createVM();
        if (vm) {
            vmInitialized = true;
            return true;
        }
    }
    return false;
}

bool MiningThreadData::calculateHash(const std::vector<uint8_t>& blob, uint8_t* outputHash, uint32_t currentDebugCounter) {
    if (!vm || !vmInitialized) {
        return false;
    }

    std::lock_guard<std::mutex> lock(vmMutex);
    
    // Copy blob to input buffer
    std::memcpy(hashBuffers->inputBuffer.data(), blob.data(), blob.size());
    
    // Calculate hash
    RandomXManager::calculateHash(vm, hashBuffers->inputBuffer.data(), hashBuffers->inputBuffer.size(), outputHash);
    
    // Update counters
    hashCount++;
    totalHashCount++;
    
    return true;
}

void MiningThreadData::cleanup() {
    std::lock_guard<std::mutex> lock(vmMutex);
    if (vm && vmInitialized) {
        RandomXManager::destroyVM(vm);
        vm = nullptr;
        vmInitialized = false;
    }
}

void miningThread(MiningThreadData* data) {
    threadSafePrint("Mining thread " + std::to_string(data->getThreadId()) + " started");
    
    while (!PoolClient::shouldStop) {
        Job job;
        {
            std::unique_lock<std::mutex> lock(PoolClient::jobMutex);
            PoolClient::jobAvailable.wait(lock, [] { return !PoolClient::jobQueue.empty() || PoolClient::shouldStop; });
            if (PoolClient::shouldStop) break;
            job = PoolClient::jobQueue.front();
            PoolClient::jobQueue.pop();
        }

        if (debugMode) {
            threadSafePrint("Processing new job:");
            threadSafePrint("  Height: " + std::to_string(job.getHeight()));
            threadSafePrint("  Job ID: " + job.getId());
            threadSafePrint("  Target: " + job.getTarget());
            threadSafePrint("  Seed Hash: " + job.getSeedHash());
            threadSafePrint("  Blob size: " + std::to_string(job.getBlob().size()) + " bytes");
            threadSafePrint("  Blob: " + bytesToHex(job.getBlob()));
            threadSafePrint("  Initial nonce position: bytes 39-42");
        }

        // Initialize RandomX with the seed hash from the job
        if (!RandomXManager::initializeRandomX(job.getSeedHash())) {
            threadSafePrint("Failed to initialize RandomX with seed hash: " + job.getSeedHash());
            continue;
        }

        // Initialize VM if needed
        if (!data->getVM()) {
            data->initializeVM();
        }

        // Get the target from the job
        std::string target = job.getTarget();
        
        // Mining loop
        uint32_t nonce = 0;
        uint64_t hashes = 0;
        auto lastHashTime = std::chrono::steady_clock::now();
        std::vector<uint8_t> mutableBlob = job.getBlob();
        bool firstHash = true;

        while (!PoolClient::shouldStop) {
            try {
                // Set nonce in the blob
                mutableBlob[39] = (nonce >> 24) & 0xFF;
                mutableBlob[40] = (nonce >> 16) & 0xFF;
                mutableBlob[41] = (nonce >> 8) & 0xFF;
                mutableBlob[42] = nonce & 0xFF;

                // Calculate hash
                uint8_t hash[32];
                if (!data->calculateHash(mutableBlob, hash)) {
                    threadSafePrint("Failed to calculate hash");
                    continue;
                }
                std::string hashHex = HashValidation::formatHash(std::vector<uint8_t>(hash, hash + 32));

                // Force debug output for first hash
                if (firstHash) {
                    std::stringstream ss;
                    ss << "\nFirst hash attempt:" << std::endl;
                    ss << "  Job ID: " << job.getId() << std::endl;
                    ss << "  Height: " << job.getHeight() << std::endl;
                    ss << "  Target: " << target << std::endl;
                    ss << "  Nonce: 0x" << bytesToHex(std::vector<uint8_t>(mutableBlob.begin() + 39, mutableBlob.begin() + 43)) << std::endl;
                    ss << "  Blob: " << bytesToHex(mutableBlob) << std::endl;
                    ss << "  Hash: " << hashHex << std::endl;
                    ss << "  Hash bytes: ";
                    for (int i = 0; i < 32; i++) {
                        ss << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(hash[i]) << " ";
                    }
                    ss << std::endl;
                    ss << "  Target difficulty: " << std::dec << HashValidation::getTargetDifficulty(target) << std::endl;
                    threadSafePrint(ss.str(), true);
                    firstHash = false;
                }

                // Check if hash meets target
                if (HashValidation::validateHash(hashHex, target)) {
                    threadSafePrint("\nShare found!");
                    threadSafePrint("  Job ID: " + job.getId());
                    threadSafePrint("  Nonce: 0x" + bytesToHex(std::vector<uint8_t>(mutableBlob.begin() + 39, mutableBlob.begin() + 43)));
                    threadSafePrint("  Hash: " + hashHex);
                    threadSafePrint("  Target: " + target);

                    // Submit share
                    if (!PoolClient::submitShare(job.getId(), 
                        bytesToHex(std::vector<uint8_t>(mutableBlob.begin() + 39, mutableBlob.begin() + 43)),
                        hashHex, "rx/0")) {
                        threadSafePrint("Failed to submit share");
                    }
                }

                // Update nonce and hash counter
                nonce++;
                hashes++;

                // Log thread stats every second
                auto now = std::chrono::steady_clock::now();
                if (std::chrono::duration_cast<std::chrono::seconds>(now - lastHashTime).count() >= 1) {
                    // Update global stats
                    data->hashes = hashes;
                    data->lastUpdate = now;
                    MiningStats::updateThreadStats(data);
                    
                    std::stringstream ss;
                    ss << "Thread " << data->getThreadId() << " stats: Hashes: " << hashes << " | Nonce: 0x" 
                       << bytesToHex(std::vector<uint8_t>(mutableBlob.begin() + 39, mutableBlob.begin() + 43));
                    threadSafePrint(ss.str(), true);
                    hashes = 0;
                    lastHashTime = now;
                }
            } catch (const std::exception& e) {
                threadSafePrint("\nError in mining loop: " + std::string(e.what()));
                break;
            }
        }
    }
} 