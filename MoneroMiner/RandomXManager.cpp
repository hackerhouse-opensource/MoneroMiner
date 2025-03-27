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
std::mutex RandomXManager::initMutex;
std::unordered_map<int, randomx_vm*> RandomXManager::vms;
randomx_cache* RandomXManager::cache = nullptr;
randomx_dataset* RandomXManager::dataset = nullptr;
std::string RandomXManager::currentSeedHash;
bool RandomXManager::initialized = false;
std::vector<MiningThreadData*> RandomXManager::threadData;
std::string RandomXManager::datasetPath = "randomx_dataset.bin";
std::string RandomXManager::currentTargetHex;
std::string RandomXManager::lastHashHex;
std::mutex RandomXManager::hashMutex;
std::vector<uint8_t> RandomXManager::lastHash;

bool RandomXManager::initialize(const std::string& seedHash) {
    std::lock_guard<std::mutex> lock(initMutex);
    
    // If we already have a dataset with this seed hash, no need to reinitialize
    if (currentSeedHash == seedHash && dataset != nullptr) {
        threadSafePrint("Using existing RandomX dataset for seed hash: " + seedHash, true);
        return true;
    }

    // Clean up existing resources
    if (dataset != nullptr) {
        randomx_release_dataset(dataset);
        dataset = nullptr;
    }
    if (cache != nullptr) {
        randomx_release_cache(cache);
        cache = nullptr;
    }

    // Set up dataset path
    datasetPath = getDatasetPath(seedHash);
    
    // Try to load existing dataset
    if (std::filesystem::exists(datasetPath)) {
        threadSafePrint("Loading existing RandomX dataset from: " + datasetPath, true);
        if (!loadDataset(seedHash)) {
            threadSafePrint("Failed to load existing dataset, will create new one", true);
        } else {
            currentSeedHash = seedHash;
            return true;
        }
    }

    // Create new dataset
    threadSafePrint("Creating new RandomX dataset...", true);
    
    // Allocate and initialize cache
    cache = randomx_alloc_cache(static_cast<randomx_flags>(RANDOMX_FLAG_DEFAULT));
    if (!cache) {
        threadSafePrint("Failed to allocate RandomX cache", true);
        return false;
    }

    // Initialize cache
    threadSafePrint("Initializing RandomX cache...", true);
    randomx_init_cache(cache, seedHash.c_str(), seedHash.length());

    // Allocate dataset
    dataset = randomx_alloc_dataset(static_cast<randomx_flags>(RANDOMX_FLAG_DEFAULT));
    if (!dataset) {
        threadSafePrint("Failed to allocate RandomX dataset", true);
        randomx_release_cache(cache);
        cache = nullptr;
        return false;
    }

    // Initialize dataset in parallel
    threadSafePrint("Initializing RandomX dataset...", true);
    const int numThreads = std::thread::hardware_concurrency();
    std::vector<std::thread> threads;
    std::atomic<uint32_t> progress(0);
    const uint32_t totalItems = RANDOMX_DATASET_ITEM_COUNT;

    for (int i = 0; i < numThreads; i++) {
        threads.emplace_back([&progress, totalItems, i, numThreads]() {
            const uint32_t itemsPerThread = totalItems / numThreads;
            const uint32_t startItem = i * itemsPerThread;
            const uint32_t endItem = (i == numThreads - 1) ? totalItems : (i + 1) * itemsPerThread;
            
            randomx_init_dataset(dataset, cache, startItem, endItem - startItem);
            progress += (endItem - startItem);
            
            // Print progress
            uint32_t currentProgress = progress.load();
            if (currentProgress % (totalItems / 10) == 0) {
                threadSafePrint("Dataset initialization progress: " + 
                              std::to_string((currentProgress * 100) / totalItems) + "%", true);
            }
        });
    }

    // Wait for all threads to complete
    for (auto& thread : threads) {
        thread.join();
    }

    threadSafePrint("Dataset initialization complete", true);

    // Save dataset to file
    threadSafePrint("Saving dataset to file: " + datasetPath, true);
    if (!saveDataset(seedHash)) {
        threadSafePrint("Failed to save dataset to file", true);
    }

    // Release cache as it's no longer needed
    randomx_release_cache(cache);
    cache = nullptr;

    // Update current seed hash
    currentSeedHash = seedHash;
    threadSafePrint("RandomX initialization complete", true);
    return true;
}

bool RandomXManager::loadDataset(const std::string& seedHash) {
    // Allocate dataset if not already allocated
    if (!dataset) {
        dataset = randomx_alloc_dataset(static_cast<randomx_flags>(RANDOMX_FLAG_DEFAULT));
        if (!dataset) {
            threadSafePrint("Failed to allocate dataset for loading", true);
            return false;
        }
    }

    std::ifstream file(datasetPath, std::ios::binary);
    if (!file.is_open()) {
        threadSafePrint("Failed to open dataset file for reading: " + datasetPath, true);
        return false;
    }

    try {
        // Read and verify dataset size
        uint64_t fileDatasetSize;
        file.read(reinterpret_cast<char*>(&fileDatasetSize), sizeof(fileDatasetSize));
        if (fileDatasetSize != RANDOMX_DATASET_SIZE) {
            threadSafePrint("Invalid dataset size in file", true);
            file.close();
            return false;
        }

        // Read and verify seed hash
        uint32_t seedHashLength;
        file.read(reinterpret_cast<char*>(&seedHashLength), sizeof(seedHashLength));
        std::string fileSeedHash(seedHashLength, '\0');
        file.read(&fileSeedHash[0], seedHashLength);
        
        if (fileSeedHash != seedHash) {
            threadSafePrint("Seed hash mismatch in file", true);
            file.close();
            return false;
        }

        // Read dataset data
        void* datasetMemory = randomx_get_dataset_memory(dataset);
        if (!datasetMemory) {
            threadSafePrint("Failed to get dataset memory", true);
            file.close();
            return false;
        }

        file.read(reinterpret_cast<char*>(datasetMemory), RANDOMX_DATASET_SIZE);
        file.close();
        threadSafePrint("Dataset loaded successfully", true);
        return true;
    }
    catch (const std::exception& e) {
        threadSafePrint("Error loading dataset: " + std::string(e.what()), true);
        file.close();
        return false;
    }
}

bool RandomXManager::saveDataset(const std::string& seedHash) {
    if (!dataset) {
        threadSafePrint("Cannot save dataset: dataset is null", true);
        return false;
    }

    std::ofstream file(datasetPath, std::ios::binary);
    if (!file.is_open()) {
        threadSafePrint("Failed to open dataset file for writing: " + datasetPath, true);
        return false;
    }

    try {
        // Write dataset size
        uint64_t datasetSize = RANDOMX_DATASET_SIZE;
        file.write(reinterpret_cast<const char*>(&datasetSize), sizeof(datasetSize));

        // Write seed hash length and seed hash
        uint32_t seedHashLength = static_cast<uint32_t>(seedHash.length());
        file.write(reinterpret_cast<const char*>(&seedHashLength), sizeof(seedHashLength));
        file.write(seedHash.c_str(), seedHashLength);

        // Write dataset data
        void* datasetMemory = randomx_get_dataset_memory(dataset);
        if (!datasetMemory) {
            threadSafePrint("Failed to get dataset memory", true);
            file.close();
            return false;
        }

        file.write(reinterpret_cast<const char*>(datasetMemory), RANDOMX_DATASET_SIZE);
        file.close();
        threadSafePrint("Dataset saved successfully", true);
        return true;
    }
    catch (const std::exception& e) {
        threadSafePrint("Error saving dataset: " + std::string(e.what()), true);
        file.close();
        return false;
    }
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
    if (!vm || blob.empty()) {
        if (debugMode) {
            threadSafePrint("Invalid parameters for hash calculation", true);
        }
        return false;
    }

    try {
        // Create a copy of the input vector
        std::vector<uint8_t> input = blob;
        
        // Ensure input is exactly 76 bytes
        if (input.size() != 76) {
            if (debugMode) {
                threadSafePrint("Invalid blob size: " + std::to_string(input.size()) + " bytes", true);
            }
            return false;
        }

        // Insert nonce at the last 4 bytes (bytes 72-75)
        for (int i = 0; i < 4; i++) {
            input[72 + i] = static_cast<uint8_t>((nonce >> (i * 8)) & 0xFF);
        }

        // Debug output for input data (only for first hash in batch)
        if (debugMode && nonce % 1000 == 0) {
            std::string debugMsg = "Calculating hash with nonce: 0x" + std::to_string(nonce);
            threadSafePrint(debugMsg, true);
        }

        // Calculate hash
        uint8_t output[32];
        randomx_calculate_hash(vm, input.data(), input.size(), output);

        // Convert hash to hex string
        std::stringstream ss;
        for (int i = 0; i < 32; i++) {
            ss << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(output[i]);
        }
        lastHashHex = ss.str();

        // Store hash with mutex protection
        {
            std::lock_guard<std::mutex> lock(hashMutex);
            lastHash = std::vector<uint8_t>(output, output + 32);
        }

        // Debug output for hash calculation (only for first hash in batch)
        if (debugMode && nonce % 1000 == 0) {
            std::string debugMsg = "Hash: " + lastHashHex + " Target: 0x" + currentTargetHex;
            threadSafePrint(debugMsg, true);
        }

        // Convert target from compact to expanded format
        uint64_t targetValue = std::stoull(currentTargetHex, nullptr, 16);
        uint32_t exponent = (targetValue >> 24) & 0xFF;
        uint32_t mantissa = targetValue & 0xFFFFFF;
        
        // Calculate expanded target
        uint64_t expandedTarget = 0;
        if (exponent <= 3) {
            expandedTarget = mantissa >> (8 * (3 - exponent));
        } else {
            expandedTarget = static_cast<uint64_t>(mantissa) << (8 * (exponent - 3));
        }

        // Compare hash with target
        bool meetsTarget = false;
        for (int i = 0; i < 8; i++) {
            uint64_t hashPart = 0;
            for (int j = 0; j < 8; j++) {
                hashPart = (hashPart << 8) | output[7 - i - j];
            }
            if (hashPart < expandedTarget) {
                meetsTarget = true;
                break;
            } else if (hashPart > expandedTarget) {
                break;
            }
        }

        if (meetsTarget && debugMode) {
            threadSafePrint("Found hash meeting target: " + lastHashHex, true);
        }

        return meetsTarget;
    }
    catch (const std::exception& e) {
        if (debugMode) {
            threadSafePrint("Exception in calculateHash: " + std::string(e.what()), true);
        }
        return false;
    }
    catch (...) {
        if (debugMode) {
            threadSafePrint("Unknown exception in calculateHash", true);
        }
        return false;
    }
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

    // Compare the calculated hash with the expected hash
    std::string calculatedHash = getLastHashHex();
    std::string expectedHashHex;
    for (int i = 0; i < 32; i++) {
        char hex[3];
        snprintf(hex, sizeof(hex), "%02x", expectedHash[i]);
        expectedHashHex += hex;
    }
    return calculatedHash == expectedHashHex;
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

std::string RandomXManager::getLastHashHex() {
    std::lock_guard<std::mutex> lock(hashMutex);
    return lastHashHex;
}

void RandomXManager::setTarget(const std::string& targetHex) {
    std::lock_guard<std::mutex> lock(hashMutex);
    currentTargetHex = targetHex;
} 