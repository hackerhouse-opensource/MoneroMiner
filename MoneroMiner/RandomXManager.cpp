#include "RandomXManager.h"
#include "Utils.h"
#include "Types.h"
#include "Constants.h"
#include "MiningStats.h"
#include "MiningThreadData.h"
#include <fstream>
#include <thread>
#include <vector>
#include <mutex>
#include <sstream>
#include <iomanip>
#include "RandomXFlags.h"
#include <cstring>

// RandomX constants
#define RANDOMX_DATASET_SIZE (RANDOMX_DATASET_ITEM_COUNT * RANDOMX_DATASET_ITEM_SIZE)
#define RANDOMX_DATASET_ITEM_COUNT 262144
#define RANDOMX_DATASET_ITEM_SIZE 64

// Global variables declared in MoneroMiner.h
extern bool debugMode;
extern std::atomic<bool> showedInitMessage;

// Static member initialization
randomx_cache* RandomXManager::cache = nullptr;
randomx_dataset* RandomXManager::dataset = nullptr;
std::mutex RandomXManager::mutex;
bool RandomXManager::initialized = false;
std::string RandomXManager::currentSeedHash;
std::mutex RandomXManager::seedHashMutex;
std::mutex RandomXManager::initMutex;
std::vector<MiningThreadData*> RandomXManager::threadData;
randomx_vm* RandomXManager::currentVM = nullptr;

bool RandomXManager::initializeDataset() {
    std::lock_guard<std::mutex> lock(mutex);
    
    if (initialized) {
        return true;
    }

    // Initialize RandomX flags
    int flags = RANDOMX_FLAG_DEFAULT;
    flags |= RANDOMX_FLAG_FULL_MEM;
    flags |= RANDOMX_FLAG_JIT;
    flags |= RANDOMX_FLAG_HARD_AES;
    flags |= RANDOMX_FLAG_LARGE_PAGES;

    // Create cache
    cache = randomx_alloc_cache(flags);
    if (!cache) {
        threadSafePrint("Failed to allocate RandomX cache", true);
        return false;
    }

    // Initialize cache with a default key
    randomx_init_cache(cache, nullptr, 0);

    // Allocate dataset
    dataset = randomx_alloc_dataset(flags);
    if (!dataset) {
        threadSafePrint("Failed to allocate RandomX dataset", true);
        randomx_release_cache(cache);
        cache = nullptr;
        return false;
    }

    // Initialize dataset
    threadSafePrint("Initializing RandomX dataset (this may take a few minutes)...", true);
    randomx_init_dataset(dataset, cache, 0, randomx_dataset_item_count());
    threadSafePrint("RandomX dataset initialization complete", true);

    initialized = true;
    return true;
}

void RandomXManager::cleanup() {
    std::lock_guard<std::mutex> lock(mutex);
    
    if (dataset) {
        randomx_release_dataset(dataset);
        dataset = nullptr;
    }
    
    if (cache) {
        randomx_release_cache(cache);
        cache = nullptr;
    }
    
    initialized = false;
}

randomx_vm* RandomXManager::createVM() {
    std::lock_guard<std::mutex> lock(mutex);
    
    if (!initialized || !cache || !dataset) {
        threadSafePrint("Cannot create VM: cache or dataset not initialized", true);
        return nullptr;
    }

    // If we already have a VM, return it
    if (currentVM) {
        return currentVM;
    }

    // Create new VM with consistent flags
    RandomXFlags flags(RANDOMX_FLAG_JIT | RANDOMX_FLAG_HARD_AES | RANDOMX_FLAG_FULL_MEM);
    currentVM = randomx_create_vm(flags.get(), cache, dataset);
    
    if (!currentVM) {
        threadSafePrint("Failed to create RandomX VM", true);
        return nullptr;
    }

    return currentVM;
}

void RandomXManager::destroyVM(randomx_vm* vm) {
    if (vm) {
        randomx_destroy_vm(vm);
    }
}

bool RandomXManager::calculateHash(randomx_vm* vm, const uint8_t* input, size_t inputSize, uint8_t* output) {
    if (!vm) {
        return false;
    }

    randomx_calculate_hash(vm, input, inputSize, output);
    return true;
}

bool RandomXManager::verifyHash(const std::vector<uint8_t>& input, const uint8_t* expectedHash) {
    uint8_t calculatedHash[32];
    randomx_vm* vm = createVM();
    if (!vm) {
        return false;
    }

    bool result = calculateHash(vm, input.data(), input.size(), calculatedHash);
    destroyVM(vm);

    if (!result) {
        return false;
    }

    return std::memcmp(calculatedHash, expectedHash, 32) == 0;
}

bool RandomXManager::isDatasetValid(const std::string& seedHash) {
    std::ifstream file("randomx_dataset.bin.seed", std::ios::binary);
    if (!file.is_open()) {
        threadSafePrint("No existing dataset found", true);
        return false;
    }

    std::string storedSeedHash;
    std::getline(file, storedSeedHash);
    file.close();

    // Remove any trailing whitespace
    while (!storedSeedHash.empty() && (storedSeedHash.back() == '\n' || storedSeedHash.back() == '\r')) {
        storedSeedHash.pop_back();
    }

    if (storedSeedHash != seedHash) {
        threadSafePrint("Dataset seed hash mismatch", true);
        return false;
    }

    // Check if dataset file exists and has correct size
    std::ifstream datasetFile("randomx_dataset.bin", std::ios::binary | std::ios::ate);
    if (!datasetFile.is_open()) {
        threadSafePrint("Dataset file not found", true);
        return false;
    }

    std::streamsize size = datasetFile.tellg();
    datasetFile.close();

    if (size != RANDOMX_DATASET_SIZE) {
        threadSafePrint("Dataset file has incorrect size", true);
        return false;
    }

    threadSafePrint("Found valid dataset for seed hash: " + seedHash, true);
    return true;
}

bool RandomXManager::saveDataset(const std::string& seedHash) {
    if (!dataset) {
        threadSafePrint("Cannot save dataset: no dataset allocated", true);
        return false;
    }

    // Save seed hash
    std::ofstream seedFile("randomx_dataset.bin.seed", std::ios::binary);
    if (!seedFile.is_open()) {
        threadSafePrint("Failed to open seed hash file for writing", true);
        return false;
    }
    seedFile << seedHash << std::endl;
    seedFile.close();

    // Save dataset
    std::ofstream datasetFile("randomx_dataset.bin", std::ios::binary);
    if (!datasetFile.is_open()) {
        threadSafePrint("Failed to open dataset file for writing", true);
        return false;
    }

    const uint8_t* data = static_cast<const uint8_t*>(randomx_get_dataset_memory(dataset));
    datasetFile.write(reinterpret_cast<const char*>(data), RANDOMX_DATASET_SIZE);
    datasetFile.close();

    threadSafePrint("Dataset saved successfully", true);
    return true;
}

bool RandomXManager::loadDataset(const std::string& seedHash) {
    if (!dataset) {
        threadSafePrint("Cannot load dataset: no dataset allocated", true);
        return false;
    }

    std::ifstream datasetFile("randomx_dataset.bin", std::ios::binary);
    if (!datasetFile.is_open()) {
        threadSafePrint("Failed to open dataset file for reading", true);
        return false;
    }

    uint8_t* data = static_cast<uint8_t*>(randomx_get_dataset_memory(dataset));
    datasetFile.read(reinterpret_cast<char*>(data), RANDOMX_DATASET_SIZE);
    datasetFile.close();

    threadSafePrint("Dataset loaded successfully", true);
    return true;
}

bool RandomXManager::initializeRandomX(const std::string& seedHash) {
    std::lock_guard<std::mutex> lock(initMutex);
    
    // Clean up existing state
    if (currentVM) {
        randomx_destroy_vm(currentVM);
        currentVM = nullptr;
    }
    if (dataset) {
        randomx_release_dataset(dataset);
        dataset = nullptr;
    }
    if (cache) {
        randomx_release_cache(cache);
        cache = nullptr;
    }

    // Use consistent flags across all RandomX operations
    RandomXFlags flags(RANDOMX_FLAG_JIT | RANDOMX_FLAG_HARD_AES | RANDOMX_FLAG_FULL_MEM);

    // Check if we have a valid dataset for this seed hash
    if (isDatasetValid(seedHash)) {
        threadSafePrint("Using existing dataset for seed hash: " + seedHash, true);
        
        // Allocate cache and dataset
        cache = randomx_alloc_cache(flags.get());
        if (!cache) {
            threadSafePrint("Failed to allocate RandomX cache", true);
            return false;
        }

        dataset = randomx_alloc_dataset(flags.get());
        if (!dataset) {
            threadSafePrint("Failed to allocate RandomX dataset", true);
            randomx_release_cache(cache);
            cache = nullptr;
            return false;
        }

        // Load the dataset from file
        if (!loadDataset(seedHash)) {
            threadSafePrint("Failed to load dataset from file", true);
            randomx_release_dataset(dataset);
            randomx_release_cache(cache);
            dataset = nullptr;
            cache = nullptr;
            return false;
        }

        // Initialize cache with seed hash
        randomx_init_cache(cache, seedHash.c_str(), seedHash.length());

        // Create VM
        currentVM = randomx_create_vm(flags.get(), cache, dataset);
        if (!currentVM) {
            threadSafePrint("Failed to create RandomX VM", true);
            randomx_release_dataset(dataset);
            randomx_release_cache(cache);
            dataset = nullptr;
            cache = nullptr;
            return false;
        }

        initialized = true;
        threadSafePrint("Successfully initialized RandomX with existing dataset", true);
        return true;
    }

    // If we get here, we need to create a new dataset
    threadSafePrint("Creating new dataset for seed hash: " + seedHash, true);
    
    // Allocate cache and dataset
    cache = randomx_alloc_cache(flags.get());
    if (!cache) {
        threadSafePrint("Failed to allocate RandomX cache", true);
        return false;
    }

    dataset = randomx_alloc_dataset(flags.get());
    if (!dataset) {
        threadSafePrint("Failed to allocate RandomX dataset", true);
        randomx_release_cache(cache);
        cache = nullptr;
        return false;
    }

    // Initialize cache with seed hash
    randomx_init_cache(cache, seedHash.c_str(), seedHash.length());

    // Initialize dataset using multiple threads
    const int numThreads = std::thread::hardware_concurrency();
    threadSafePrint("Initializing dataset using " + std::to_string(numThreads) + " threads...", true);
    
    std::vector<std::thread> threads;
    for (int i = 0; i < numThreads; i++) {
        threads.emplace_back([i, numThreads]() {
            try {
                uint64_t startItem = (RANDOMX_DATASET_ITEM_COUNT * i) / numThreads;
                uint64_t itemCount = (RANDOMX_DATASET_ITEM_COUNT * (i + 1)) / numThreads - startItem;
                randomx_init_dataset(dataset, cache,
                    static_cast<unsigned long>(startItem),
                    static_cast<unsigned long>(itemCount));
            } catch (const std::exception& e) {
                threadSafePrint("Error in dataset initialization thread: " + std::string(e.what()), true);
            }
        });
    }

    for (auto& thread : threads) {
        thread.join();
    }

    // Save the dataset for future use
    if (!saveDataset(seedHash)) {
        threadSafePrint("Failed to save dataset", true);
        randomx_release_dataset(dataset);
        randomx_release_cache(cache);
        dataset = nullptr;
        cache = nullptr;
        return false;
    }

    // Create VM
    currentVM = randomx_create_vm(flags.get(), cache, dataset);
    if (!currentVM) {
        threadSafePrint("Failed to create RandomX VM", true);
        randomx_release_dataset(dataset);
        randomx_release_cache(cache);
        dataset = nullptr;
        cache = nullptr;
        return false;
    }

    initialized = true;
    threadSafePrint("Successfully initialized RandomX with new dataset", true);
    return true;
}

void RandomXManager::handleSeedHashChange(const std::string& newSeedHash) {
    std::lock_guard<std::mutex> lock(seedHashMutex);
    if (currentSeedHash != newSeedHash) {
        threadSafePrint("Seed hash changed from " + (currentSeedHash.empty() ? "none" : currentSeedHash) + " to " + newSeedHash, true);
        currentSeedHash = newSeedHash;
        
        // Clean up existing state
        if (currentVM) {
            randomx_destroy_vm(currentVM);
            currentVM = nullptr;
        }
        
        // Initialize with new seed hash
        if (!initializeRandomX(newSeedHash)) {
            threadSafePrint("Failed to initialize RandomX with new seed hash", true);
        }
    }
} 