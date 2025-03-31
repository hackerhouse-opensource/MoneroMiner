#include "MiningThread.h"
#include "MiningThreadData.h"
#include "RandomXManager.h"
#include "MiningStats.h"
#include "Globals.h"
#include "Utils.h"
#include "Constants.h"
#include "Types.h"
#include <cstdint>
#include <sstream>
#include <iomanip>
#include <chrono>
#include <thread>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <vector>
#include <string>
#include <iostream>
#include <cstring>
#include <picojson.h>

void miningThread(MiningThreadData* data) {
    threadSafePrint("Mining thread " + std::to_string(data->getId()) + " started");
    
    // Initialize RandomX VM for this thread
    threadSafePrint("Initializing VM for thread " + std::to_string(data->getId()));
    if (!RandomXManager::initializeVM(data->getId())) {
        threadSafePrint("Failed to initialize VM for thread " + std::to_string(data->getId()));
        return;
    }
    
    // Debug VM state
    if (config.debugMode) {
        threadSafePrint("RandomX VM State for thread " + std::to_string(data->getId()) + ":");
        threadSafePrint("  Dataset Size: " + std::to_string(RANDOMX_DATASET_SIZE) + " bytes");
        threadSafePrint("  Dataset Items: " + std::to_string(RANDOMX_DATASET_ITEM_COUNT));
        threadSafePrint("  Item Size: " + std::to_string(RANDOMX_DATASET_ITEM_SIZE) + " bytes");
    }
    
    threadSafePrint("VM initialized successfully for thread " + std::to_string(data->getId()));
    
    // Mark thread as running
    data->isRunning = true;
    MiningStats::updateThreadStats(data);
    
    while (!data->shouldStop) {
        try {
            Job job;
            {
                std::unique_lock<std::mutex> lock(data->jobMutex);
                data->jobCondition.wait(lock, [data]() {
                    return !data->jobQueue.empty() || data->shouldStop;
                });
                if (data->shouldStop) break;
                job = data->jobQueue.front();
                data->jobQueue.pop();
            }

            // Set target in RandomXManager
            RandomXManager::setTarget(job.target);
            RandomXManager::setJobInfo(job.height, job.jobId);

            if (config.debugMode) {
                std::stringstream ss;
                ss << "[" << getCurrentTimestamp() << "] randomx  new job:" << std::endl;
                ss << "  Height: " << job.height << std::endl;
                ss << "  Target: 0x" << job.target << std::endl;
                ss << "  Difficulty: " << job.difficulty << std::endl;
                ss << "  Blob: " << job.blob << std::endl;
                ss << "  Seed Hash: " << job.seedHash << std::endl;
                threadSafePrint(ss.str(), true);
            }

            // Set nonce to 0 for this job
            uint32_t nonce = 0;

            // Prepare input for hashing
            std::vector<uint8_t> input = hexToBytes(job.blob);
            if (input.size() != 76) {
                threadSafePrint("Error: Invalid blob size", true);
                continue;
            }

            // Set nonce in big-endian order
            input[39] = (nonce >> 24) & 0xFF;
            input[40] = (nonce >> 16) & 0xFF;
            input[41] = (nonce >> 8) & 0xFF;
            input[42] = nonce & 0xFF;

            if (config.debugMode) {
                std::stringstream ss;
                ss << "[" << getCurrentTimestamp() << "] randomx  first hash:" << std::endl;
                ss << "  Input: " << bytesToHex(input) << std::endl;
                ss << "  Nonce: 0x" << std::hex << nonce << std::endl;
                threadSafePrint(ss.str(), true);
            }

            // Calculate first hash
            if (RandomXManager::calculateHash(data->getVM(), input, nonce)) {
                if (config.debugMode) {
                    std::stringstream ss;
                    ss << "[" << getCurrentTimestamp() << "] randomx  first hash:" << std::endl;
                    ss << "  Input: " << bytesToHex(input) << std::endl;
                    ss << "  Nonce: 0x" << std::hex << std::setw(8) << std::setfill('0') << nonce << std::endl;
                    ss << "  Hash: " << bytesToHex(RandomXManager::getLastHash()) << std::endl;
                    ss << "  Target: 0x" << job.target << std::endl;
                    threadSafePrint(ss.str(), true);
                }
            }

            // Update thread stats
            data->hashes++;
            data->lastUpdate = std::chrono::steady_clock::now();
            MiningStats::updateThreadStats(data);

            // Update global stats
            MiningStats::updateGlobalStats(data);

            // Main mining loop
            while (!data->shouldStop && job.jobId == data->currentJobId) {
                nonce++;

                // Set nonce in big-endian order
                input[39] = (nonce >> 24) & 0xFF;
                input[40] = (nonce >> 16) & 0xFF;
                input[41] = (nonce >> 8) & 0xFF;
                input[42] = nonce & 0xFF;

                // Calculate hash
                if (RandomXManager::calculateHash(data->getVM(), input, nonce)) {
                    // Found a valid share - submit it
                    std::string nonceHex = Utils::formatHex(nonce, 8);
                    std::string hashHex = bytesToHex(RandomXManager::getLastHash());
                    
                    if (config.debugMode) {
                        threadSafePrint("\nFound valid share!", true);
                        threadSafePrint("  Job ID: " + job.jobId, true);
                        threadSafePrint("  Nonce: " + nonceHex, true);
                        threadSafePrint("  Hash: " + hashHex, true);
                    }
                    
                    // Submit share through PoolClient
                    bool accepted = false;
                    int retries = 3;
                    while (retries > 0 && !accepted) {
                        accepted = PoolClient::submitShare(job.jobId, nonceHex, hashHex, "rx/0");
                        if (!accepted && retries > 1) {
                            threadSafePrint("Share submission failed, retrying... (" + std::to_string(retries-1) + " attempts left)", true);
                            std::this_thread::sleep_for(std::chrono::milliseconds(100));
                        }
                        retries--;
                    }

                    if (accepted) {
                        threadSafePrint("Share accepted by pool!", true);
                        data->acceptedShares++;
                    } else {
                        threadSafePrint("Share rejected by pool", true);
                        data->rejectedShares++;
                    }

                    MiningStats::updateThreadStats(data);
                    MiningStats::updateGlobalStats(data);
                }

                // Update stats every 30 seconds
                auto now = std::chrono::steady_clock::now();
                if (now - data->lastUpdate >= std::chrono::seconds(30)) {
                    updateMiningStats(data, now);
                }
            }
        }
        catch (const std::exception& e) {
            threadSafePrint("Mining thread " + std::to_string(data->getId()) + " error: " + e.what(), true);
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
    }
    
    data->isRunning = false;
    threadSafePrint("Mining thread " + std::to_string(data->getId()) + " stopped");
} 