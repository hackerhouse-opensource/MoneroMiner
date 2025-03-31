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
#include <cstring>
#include <filesystem>

// Add RandomX constants if not defined by randomx.h
#ifndef RANDOMX_DATASET_ITEM_COUNT
#define RANDOMX_DATASET_ITEM_COUNT 0x4000000ULL
#endif

#ifndef RANDOMX_DATASET_SIZE
#define RANDOMX_DATASET_SIZE (RANDOMX_DATASET_ITEM_COUNT * RANDOMX_DATASET_ITEM_SIZE)
#endif

#ifndef RANDOMX_DATASET_ITEM_SIZE
#define RANDOMX_DATASET_ITEM_SIZE 64ULL
#endif

// Global variables declared in MoneroMiner.h
extern bool debugMode;
extern std::atomic<bool> showedInitMessage;

// Static member initialization
std::mutex RandomXManager::vmMutex;
std::mutex RandomXManager::datasetMutex;
std::mutex RandomXManager::seedHashMutex;
std::mutex RandomXManager::initMutex;
std::mutex RandomXManager::hashMutex;
std::unordered_map<int, randomx_vm*> RandomXManager::vms;
randomx_cache* RandomXManager::cache = nullptr;
randomx_dataset* RandomXManager::dataset = nullptr;
std::string RandomXManager::currentSeedHash;
bool RandomXManager::initialized = false;
std::vector<MiningThreadData*> RandomXManager::threadData;
std::string RandomXManager::currentTargetHex;
std::vector<uint8_t> RandomXManager::lastHash;
uint64_t RandomXManager::currentHeight = 0;
std::string RandomXManager::currentJobId;

// Add these static member variables at the top with other static members
uint256_t RandomXManager::expandedTarget;
uint256_t RandomXManager::hashValue;
uint32_t RandomXManager::currentTarget;
std::string RandomXManager::lastHashHex;

bool RandomXManager::initialize(const std::string& seedHash) {
    std::lock_guard<std::mutex> lock(initMutex);
    
    // If we already have a dataset with this seed hash, no need to reinitialize
    if (currentSeedHash == seedHash && dataset != nullptr) {
        threadSafePrint("Using existing RandomX dataset for seed hash: " + seedHash, true);
        return true;
    }

    // Try to load existing dataset first
    std::string datasetPath = getDatasetPath(seedHash);
    threadSafePrint("Loading existing RandomX dataset from: " + datasetPath, true);
    
    // Clean up existing resources
    if (dataset != nullptr) {
        randomx_release_dataset(dataset);
        dataset = nullptr;
    }
    if (cache != nullptr) {
        randomx_release_cache(cache);
        cache = nullptr;
    }

    // Initialize with flags
    uint32_t flags = RANDOMX_FLAG_JIT | RANDOMX_FLAG_HARD_AES | RANDOMX_FLAG_FULL_MEM;
    
    // Try to load existing dataset
    dataset = randomx_alloc_dataset(flags);
    if (!dataset) {
        threadSafePrint("Failed to allocate dataset memory", true);
        return false;
    }

    if (std::filesystem::exists(datasetPath)) {
        if (loadDataset(seedHash)) {
            currentSeedHash = seedHash;
            threadSafePrint("Dataset loaded successfully", true);
            return true;
        }
        threadSafePrint("Failed to load existing dataset, will create new one", true);
    }

    // Create new dataset
    threadSafePrint("Creating new RandomX dataset...", true);
    
    // Initialize cache first
    cache = randomx_alloc_cache(flags);
    if (!cache) {
        threadSafePrint("Failed to allocate RandomX cache", true);
        randomx_release_dataset(dataset);
        dataset = nullptr;
        return false;
    }

    // Initialize cache
    threadSafePrint("Initializing RandomX cache...", true);
    randomx_init_cache(cache, seedHash.c_str(), seedHash.length());

    // Initialize dataset in parallel
    threadSafePrint("Initializing RandomX dataset...", true);
    const uint32_t numThreads = std::min(std::thread::hardware_concurrency(), 8u);
    std::vector<std::thread> threads;
    std::atomic<uint32_t> progress(0);
    const uint32_t itemsPerThread = randomx_dataset_item_count() / numThreads;

    try {
        for (uint32_t i = 0; i < numThreads; i++) {
            threads.emplace_back([i, itemsPerThread, numThreads, &progress]() {
                const uint32_t startItem = i * itemsPerThread;
                const uint32_t endItem = (i == numThreads - 1) ? 
                    randomx_dataset_item_count() : (i + 1) * itemsPerThread;
                
                randomx_init_dataset(dataset, cache, startItem, endItem - startItem);
                progress += (endItem - startItem);

                if (config.debugMode) {
                    uint32_t percent = (progress * 100) / randomx_dataset_item_count();
                    threadSafePrint("Dataset initialization: " + std::to_string(percent) + "% complete", true);
                }
            });
        }

        // Wait for all threads to complete
        for (auto& thread : threads) {
            thread.join();
        }
    }
    catch (const std::exception& e) {
        threadSafePrint("Error during dataset initialization: " + std::string(e.what()), true);
        for (auto& thread : threads) {
            if (thread.joinable()) thread.join();
        }
        randomx_release_dataset(dataset);
        randomx_release_cache(cache);
        dataset = nullptr;
        cache = nullptr;
        return false;
    }

    // Save dataset for future use
    if (!saveDataset(seedHash)) {
        threadSafePrint("Warning: Failed to save dataset", true);
    }

    // Update current seed hash
    currentSeedHash = seedHash;
    
    // Release cache as it's no longer needed
    randomx_release_cache(cache);
    cache = nullptr;

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

    std::ifstream file(getDatasetPath(seedHash), std::ios::binary);
    if (!file.is_open()) {
        threadSafePrint("Failed to open dataset file for reading: " + getDatasetPath(seedHash), true);
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

    std::ofstream file(getDatasetPath(seedHash), std::ios::binary);
    if (!file.is_open()) {
        threadSafePrint("Failed to open dataset file for writing: " + getDatasetPath(seedHash), true);
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

bool RandomXManager::calculateHash(randomx_vm* vm, const std::vector<uint8_t>& input, uint64_t nonce) {
    if (!vm || input.empty()) {
        return false;
    }

    // Ensure lastHash is properly sized
    if (lastHash.size() != 32) {
        lastHash.resize(32);
    }

    // Calculate hash
    randomx_calculate_hash(vm, input.data(), input.size(), lastHash.data());

    // Check if hash meets target
    bool meetsTarget = checkHash(lastHash.data(), currentTargetHex);

    // Show debug output if debug mode is enabled
    static uint64_t hashCount = 0;
    hashCount++;
    
    if (config.debugMode && (hashCount == 1 || hashCount % 10000 == 0)) {
        std::stringstream ss;
        ss << "\nRandomX Hash Calculation:" << std::endl;
        ss << "  Input data: " << bytesToHex(input) << std::endl;
        ss << "  Nonce: 0x" << std::hex << std::setw(8) << std::setfill('0') << nonce << std::endl;
        ss << "  Hash output: " << bytesToHex(lastHash) << std::endl;
        ss << "  Target: 0x" << currentTargetHex << std::endl;
        threadSafePrint(ss.str(), true);
    }

    if (meetsTarget) {
        std::stringstream ss;
        ss << "\nFound valid share!" << std::endl;
        ss << "  Hash: " << bytesToHex(lastHash) << std::endl;
        ss << "  Target: 0x" << currentTargetHex << std::endl;
        threadSafePrint(ss.str(), true);
    }

    return meetsTarget;
}

std::string RandomXManager::getLastHashHex() {
    std::lock_guard<std::mutex> lock(hashMutex);
    return bytesToHex(lastHash);
}

bool RandomXManager::checkHash(const uint8_t* hash, const std::string& targetHex) {
    if (!hash) {
        threadSafePrint("Error: Null hash pointer in checkHash", true);
        return false;
    }

    // Extract exponent and mantissa from target
    uint32_t target = std::stoul(targetHex, nullptr, 16);
    uint8_t exponent = (target >> 24) & 0xFF;
    uint32_t mantissa = target & 0x00FFFFFF;

    // Create 256-bit target
    uint256_t targetValue;
    targetValue.words[0] = static_cast<uint64_t>(mantissa) << 40;  // Shift left by 40 bits
    targetValue.words[1] = 0;
    targetValue.words[2] = 0;
    targetValue.words[3] = 0;

    // Create 256-bit hash value
    uint256_t hashValue;
    for (int i = 0; i < 32; i++) {
        int word_idx = i / 8;
        int byte_idx = 7 - (i % 8);
        hashValue.words[word_idx] |= (static_cast<uint64_t>(hash[i]) << (byte_idx * 8));
    }

    // Show debug output if debug mode is enabled
    if (config.debugMode) {
        std::stringstream ss;
        ss << "\nTarget Expansion:" << std::endl;
        ss << "  Compact target: 0x" << targetHex << std::endl;
        ss << "  Exponent: 0x" << std::hex << std::setw(2) << std::setfill('0') 
           << static_cast<int>(exponent) << std::endl;
        ss << "  Mantissa: 0x" << std::hex << std::setw(6) << std::setfill('0') 
           << mantissa << std::endl;
        
        ss << "\nExpanded Target (256-bit):" << std::endl;
        for (int i = 0; i < 4; i++) {
            ss << "  Word " << i << ": 0x" << std::hex << std::setw(16) << std::setfill('0') 
               << targetValue.words[i] << std::endl;
        }
        
        ss << "\nHash Value (256-bit):" << std::endl;
        for (int i = 0; i < 4; i++) {
            ss << "  Word " << i << ": 0x" << std::hex << std::setw(16) << std::setfill('0') 
               << hashValue.words[i] << std::endl;
        }
        
        ss << "\nShare Validation:" << std::endl;
        bool meetsTarget = hashValue < targetValue;
        ss << "  Hash " << (meetsTarget ? "meets" : "does not meet") << " target" << std::endl;
        
        threadSafePrint(ss.str(), true);
    }

    return hashValue < targetValue;
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

void RandomXManager::setTarget(const std::string& targetHex) {
    std::lock_guard<std::mutex> lock(hashMutex);
    currentTargetHex = targetHex;
} 