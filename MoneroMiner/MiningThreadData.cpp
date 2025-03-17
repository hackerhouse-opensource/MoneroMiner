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

// Global variables declared in MoneroMiner.h
extern bool debugMode;

MiningThreadData::MiningThreadData(int id) 
    : threadId(id), 
      isRunning(false),
      hashCount(0),
      totalHashCount(0),
      elapsedSeconds(0),
      currentJobId(""),
      currentNonce(0),
      vm(nullptr),
      vmInitialized(false),
      hashBuffers(std::make_unique<HashBuffers>()) {
}

MiningThreadData::~MiningThreadData() {
    cleanup();
}

bool MiningThreadData::initializeVM() {
    std::lock_guard<std::mutex> lock(vmMutex);
    if (!vmInitialized) {
        threadSafePrint("Initializing VM for thread " + std::to_string(threadId));
        vm = RandomXManager::createVM();
        vmInitialized = (vm != nullptr);
        if (vmInitialized) {
            threadSafePrint("VM initialized successfully for thread " + std::to_string(threadId));
        }
    }
    return vmInitialized;
}

bool MiningThreadData::calculateHash(const std::vector<uint8_t>& blob, uint8_t* outputHash, uint32_t currentDebugCounter) {
    if (!vmInitialized && !initializeVM()) {
        return false;
    }

    std::lock_guard<std::mutex> lock(vmMutex);
    RandomXManager::calculateHash(vm, blob.data(), blob.size(), outputHash);
    return true;
}

void MiningThreadData::cleanup() {
    std::lock_guard<std::mutex> lock(vmMutex);
    if (vmInitialized && vm) {
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

        // Expand target for comparison
        std::string expandedTarget = HashValidation::expandTarget(job.getTarget());
        
        // Mining loop
        uint32_t nonce = 0;
        uint64_t hashes = 0;
        auto lastHashTime = std::chrono::steady_clock::now();
        std::vector<uint8_t> mutableBlob = job.getBlob();

        while (!PoolClient::shouldStop) {
            // Update nonce in blob
            mutableBlob[39] = (nonce >> 0) & 0xFF;
            mutableBlob[40] = (nonce >> 8) & 0xFF;
            mutableBlob[41] = (nonce >> 16) & 0xFF;
            mutableBlob[42] = (nonce >> 24) & 0xFF;

            // Calculate hash
            uint8_t hash[32];
            RandomXManager::calculateHash(data->getVM(), mutableBlob.data(), mutableBlob.size(), hash);
            std::string hashHex = bytesToHex(std::vector<uint8_t>(hash, hash + 32));

            if (debugMode && hashes % 1000 == 0) {
                threadSafePrint("Hash calculation:");
                threadSafePrint("  Nonce: 0x" + bytesToHex(std::vector<uint8_t>(mutableBlob.begin() + 39, mutableBlob.begin() + 43)));
                threadSafePrint("  Hash: " + hashHex);
                threadSafePrint("  Target: " + expandedTarget);
            }

            // Check if hash meets target
            if (HashValidation::validateHash(hashHex, expandedTarget)) {
                threadSafePrint("Share found!");
                threadSafePrint("  Job ID: " + job.getId());
                threadSafePrint("  Nonce: 0x" + bytesToHex(std::vector<uint8_t>(mutableBlob.begin() + 39, mutableBlob.begin() + 43)));
                threadSafePrint("  Hash: " + hashHex);
                threadSafePrint("  Target: " + expandedTarget);

                // Submit share
                PoolClient::submitShare(job.getId(), 
                    bytesToHex(std::vector<uint8_t>(mutableBlob.begin() + 39, mutableBlob.begin() + 43)),
                    hashHex, "rx/0");
            }

            // Update statistics
            hashes++;
            if (hashes % 1000 == 0) {
                auto now = std::chrono::steady_clock::now();
                auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastHashTime).count();
                double hashrate = (1000.0 * 1000.0) / duration; // H/s
                threadSafePrint("Thread " + std::to_string(data->getThreadId()) + 
                              " - Hashes: " + std::to_string(hashes) + 
                              ", Nonce: 0x" + bytesToHex(std::vector<uint8_t>(mutableBlob.begin() + 39, mutableBlob.begin() + 43)) +
                              ", Hashrate: " + std::to_string(hashrate) + " H/s");
                lastHashTime = now;
            }

            nonce++;
        }
    }
} 