#include "RandomXManager.h"
#include "Globals.h"
#include "Utils.h"
#include "Types.h"
#include "Constants.h"
#include "MiningStats.h"
#include "MiningThreadData.h"
#include "randomx.h"
#include <fstream>
#include <thread>
#include <vector>
#include <mutex>
#include <sstream>
#include <iomanip>
#include "RandomXFlags.h"
#include <cstring>
#include <filesystem>
#include <iostream>

// RandomX constants
#define RANDOMX_DATASET_SIZE (RANDOMX_DATASET_ITEM_COUNT * RANDOMX_DATASET_ITEM_SIZE)
#define RANDOMX_DATASET_ITEM_COUNT 262144
#define RANDOMX_DATASET_ITEM_SIZE 64

// Global variables declared in MoneroMiner.h
extern bool debugMode;
extern std::atomic<bool> showedInitMessage;

// Static member initialization
std::mutex RandomXManager::vmMutex;
std::mutex RandomXManager::datasetMutex;
std::mutex RandomXManager::seedHashMutex;
std::unordered_map<int, randomx_vm*> RandomXManager::vms;
randomx_cache* RandomXManager::cache = nullptr;
randomx_dataset* RandomXManager::dataset = nullptr;
std::string RandomXManager::currentSeedHash;
bool RandomXManager::initialized = false;
std::vector<MiningThreadData*> RandomXManager::threadData;
std::string RandomXManager::datasetPath = "randomx_dataset.bin";

bool RandomXManager::initialize(const std::string& seedHash) {
    threadSafePrint("Starting RandomX initialization with seed hash: " + seedHash, true);
    
    // Calculate dataset size
    const uint64_t datasetSize = randomx_dataset_item_count() * RANDOMX_DATASET_ITEM_SIZE;
    threadSafePrint("RandomX dataset allocation: " + std::to_string(datasetSize) + " bytes", true);
    
    // Initialize flags
    randomx_flags flags = randomx_get_flags();
    flags |= RANDOMX_FLAG_FULL_MEM;
    flags |= RANDOMX_FLAG_JIT;
    flags |= RANDOMX_FLAG_HARD_AES;
    flags |= RANDOMX_FLAG_SECURE;
    
    threadSafePrint("RandomX flags: " + std::to_string(flags), true);
    
    // Allocate and initialize cache
    cache = randomx_alloc_cache(flags);
    if (!cache) {
        threadSafePrint("Failed to allocate RandomX cache", true);
        return false;
    }
    
    // Initialize cache with seed hash
    randomx_init_cache(cache, seedHash.c_str(), seedHash.length());
    
    // Allocate dataset
    dataset = randomx_alloc_dataset(flags);
    if (!dataset) {
        threadSafePrint("Failed to allocate RandomX dataset", true);
        randomx_release_cache(cache);
        cache = nullptr;
        return false;
    }
    
    // Initialize dataset
    threadSafePrint("Creating RandomX dataset...", true);
    randomx_init_dataset(dataset, cache, 0, randomx_dataset_item_count());
    
    threadSafePrint("RandomX initialization complete", true);
    return true;
}

void RandomXManager::cleanup() {
    std::lock_guard<std::mutex> vmLock(vmMutex);
    std::lock_guard<std::mutex> datasetLock(datasetMutex);
    
    // Clean up all VMs
    for (auto& [threadId, vm] : vms) {
        if (vm) {
            randomx_destroy_vm(vm);
        }
    }
    vms.clear();

    // Release dataset
    if (dataset) {
        randomx_release_dataset(dataset);
        dataset = nullptr;
    }

    // Release cache
    if (cache) {
        randomx_release_cache(cache);
        cache = nullptr;
    }

    // Reset initialization state
    initialized = false;
    currentSeedHash.clear();

    if (debugMode) {
        threadSafePrint("RandomX cleanup complete", true);
    }
}

randomx_vm* RandomXManager::createVM(int threadId) {
    threadSafePrint("Creating VM for thread " + std::to_string(threadId), true);
    
    uint32_t flags = RANDOMX_FLAG_DEFAULT;
    flags |= RANDOMX_FLAG_FULL_MEM;
    flags |= RANDOMX_FLAG_JIT;
    flags |= RANDOMX_FLAG_HARD_AES;
    flags |= RANDOMX_FLAG_SECURE;
    
    randomx_vm* vm = randomx_create_vm(flags, nullptr, dataset);
    if (!vm) {
        threadSafePrint("Failed to create VM for thread " + std::to_string(threadId), true);
        return nullptr;
    }
    
    threadSafePrint("VM created successfully for thread " + std::to_string(threadId), true);
    return vm;
}

void RandomXManager::destroyVM(randomx_vm* vm) {
    if (vm) {
        randomx_destroy_vm(vm);
    }
}

bool RandomXManager::calculateHash(randomx_vm* vm, const std::vector<uint8_t>& blob, uint64_t nonce) {
    if (!vm) {
        threadSafePrint("Cannot calculate hash: VM is null", true);
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
    uint8_t output[32];
    randomx_calculate_hash(vm, input.data(), input.size(), output);
    return true;
}

bool RandomXManager::verifyHash(const uint8_t* input, size_t inputSize, const uint8_t* expectedHash, int threadId) {
    std::vector<uint8_t> inputVec(input, input + inputSize);
    uint64_t nonce = 0; // For verification, we don't need a specific nonce
    
    auto it = vms.find(threadId);
    if (it == vms.end() || !it->second) {
        return false;
    }

    if (!calculateHash(it->second, inputVec, nonce)) {
        return false;
    }

    return true;
}

void RandomXManager::initializeDataset(const std::string& seedHash) {
    if (!dataset || !cache) {
        threadSafePrint("Cannot initialize dataset: missing dataset or cache", true);
        return;
    }

    if (debugMode) {
        threadSafePrint("Starting dataset initialization...", true);
    }

    // Initialize dataset from cache
    randomx_init_dataset(dataset, cache, 0, randomx_dataset_item_count());

    if (debugMode) {
        threadSafePrint("Dataset initialization complete", true);
    }
}

bool RandomXManager::loadDataset(const std::string& seedHash) {
    // Dataset is always initialized from cache
    return true;
}

bool RandomXManager::saveDataset(const std::string& seedHash) {
    // Dataset is always initialized from cache
    return true;
}

bool RandomXManager::validateDataset(const std::string& seedHash) {
    if (!dataset || !cache) {
        return false;
    }

    // Create a temporary VM to validate the dataset
    randomx_flags flags = randomx_get_flags();
    flags |= RANDOMX_FLAG_FULL_MEM;
    flags |= RANDOMX_FLAG_JIT;
    flags |= RANDOMX_FLAG_HARD_AES;
    flags |= RANDOMX_FLAG_SECURE;

    randomx_vm* vm = randomx_create_vm(flags, cache, dataset);
    if (!vm) {
        return false;
    }

    // Test hash calculation
    uint8_t testInput[32] = {0};
    uint8_t testOutput[32];
    randomx_calculate_hash(vm, testInput, sizeof(testInput), testOutput);

    randomx_destroy_vm(vm);
    return true;
}

std::string RandomXManager::getDatasetPath(const std::string& seedHash) {
    return "randomx_dataset_" + seedHash + ".bin";
}

void RandomXManager::handleSeedHashChange(const std::string& newSeedHash) {
    std::lock_guard<std::mutex> lock(seedHashMutex);
    
    if (newSeedHash != currentSeedHash) {
        // Clean up existing VMs
        {
            std::lock_guard<std::mutex> vmLock(vmMutex);
            for (auto& [threadId, vm] : vms) {
                if (vm) {
                    randomx_destroy_vm(vm);
                }
            }
            vms.clear();
        }

        // Initialize with new seed hash
        if (!initialize(newSeedHash)) {
            threadSafePrint("Failed to initialize RandomX with new seed hash: " + newSeedHash, true);
            return;
        }

        // Notify all mining threads to reinitialize their VMs
        for (auto* data : threadData) {
            if (data) {
                data->updateJob(Job()); // Trigger VM reinitialization
            }
        }
    }
} 