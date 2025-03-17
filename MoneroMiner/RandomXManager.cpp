#include "RandomXManager.h"
#include "Utils.h"
#include "Types.h"
#include "Constants.h"
#include "MiningStats.h"
#include <fstream>
#include <thread>
#include <vector>
#include <mutex>
#include <sstream>
#include <iomanip>
#include "RandomXFlags.h"

// Global variables declared in MoneroMiner.h
extern bool debugMode;
extern std::atomic<bool> showedInitMessage;

namespace RandomXManager {
    // Internal variables
    randomx_cache* currentCache = nullptr;
    randomx_dataset* currentDataset = nullptr;
    std::string currentSeedHash;
    std::mutex cacheMutex;
    std::mutex seedHashMutex;
    std::mutex initMutex;
    std::vector<MiningThreadData*> threadData;
    randomx_vm* currentVM = nullptr;

    randomx_vm* createVM() {
        std::lock_guard<std::mutex> lock(cacheMutex);
        if (!currentCache || !currentDataset) {
            threadSafePrint("Cannot create VM: cache or dataset not initialized");
            return nullptr;
        }

        RandomXFlags flags(RANDOMX_FLAG_JIT | RANDOMX_FLAG_HARD_AES | RANDOMX_FLAG_FULL_MEM);
        randomx_vm* vm = randomx_create_vm(flags.get(), currentCache, currentDataset);
        
        if (!vm) {
            threadSafePrint("Failed to create RandomX VM");
        } else {
            threadSafePrint("Successfully created RandomX VM");
        }
        
        return vm;
    }

    void calculateHash(randomx_vm* vm, const uint8_t* input, size_t inputSize, uint8_t* output) {
        if (!vm || !input || !output) {
            threadSafePrint("Invalid parameters for hash calculation");
            return;
        }

        randomx_calculate_hash_first(vm, input, inputSize);
        randomx_calculate_hash_last(vm, output);
    }

    void destroyVM(randomx_vm* vm) {
        if (vm) {
            randomx_destroy_vm(vm);
        }
    }

    bool isDatasetValid(const std::string& filename, const std::string& currentSeedHash) {
        std::ifstream file(filename, std::ios::binary);
        if (!file.is_open()) {
            return false;
        }

        // Read seed hash from file
        std::string storedSeedHash;
        std::getline(file, storedSeedHash);
        
        // Compare with current seed hash
        return storedSeedHash == currentSeedHash;
    }

    void saveDataset(randomx_dataset* dataset, const std::string& filename, const std::string& seedHash) {
        std::ofstream file(filename, std::ios::binary);
        if (!file.is_open()) {
            threadSafePrint("Failed to open dataset file for writing: " + filename);
            return;
        }

        // Write seed hash
        file << seedHash << std::endl;

        // Write dataset data
        const uint8_t* data = static_cast<const uint8_t*>(randomx_get_dataset_memory(dataset));
        if (!data) {
            threadSafePrint("Failed to get dataset memory");
            return;
        }

        file.write(reinterpret_cast<const char*>(data), randomx_dataset_item_count() * RANDOMX_DATASET_ITEM_SIZE);
        threadSafePrint("Dataset saved to: " + filename);
    }

    void loadDataset(randomx_dataset* dataset, const std::string& filename) {
        std::ifstream file(filename, std::ios::binary);
        if (!file.is_open()) {
            threadSafePrint("Failed to open dataset file: " + filename);
            return;
        }

        // Skip seed hash line
        std::string line;
        std::getline(file, line);

        // Read dataset data
        uint8_t* data = static_cast<uint8_t*>(randomx_get_dataset_memory(dataset));
        if (!data) {
            threadSafePrint("Failed to get dataset memory");
            return;
        }

        file.read(reinterpret_cast<char*>(data), randomx_dataset_item_count() * RANDOMX_DATASET_ITEM_SIZE);
        threadSafePrint("Dataset loaded from: " + filename);
    }

    bool initializeRandomX(const std::string& seedHash) {
        std::lock_guard<std::mutex> lock(initMutex);
        
        // Check if we already have a dataset for this seed hash
        if (!currentSeedHash.empty() && currentSeedHash == seedHash) {
            threadSafePrint("Dataset already initialized for seed hash: " + seedHash, true);
            return true;
        }
        
        // Try to load existing dataset
        std::string datasetFile = "randomx_dataset_" + seedHash + ".bin";
        if (isDatasetValid(datasetFile, seedHash)) {
            threadSafePrint("Found valid dataset file: " + datasetFile, true);
            
            // Allocate and initialize cache
            currentCache = randomx_alloc_cache(static_cast<randomx_flags>(RANDOMX_FLAG_DEFAULT));
            if (!currentCache) {
                threadSafePrint("Failed to allocate RandomX cache");
                return false;
            }
            
            threadSafePrint("Allocated RandomX cache");
            
            // Initialize cache with seed hash
            randomx_init_cache(currentCache, hexToBytes(seedHash).data(), seedHash.length() / 2);
            threadSafePrint("Initialized RandomX cache with seed hash");
            
            // Allocate dataset
            currentDataset = randomx_alloc_dataset(static_cast<randomx_flags>(RANDOMX_FLAG_DEFAULT));
            if (!currentDataset) {
                threadSafePrint("Failed to allocate RandomX dataset");
                return false;
            }
            
            threadSafePrint("Allocated RandomX dataset");
            
            // Load dataset from file
            loadDataset(currentDataset, datasetFile);
            
            // Store the seed hash
            currentSeedHash = seedHash;
            return true;
        }
        
        // Clean up existing state
        if (currentVM) {
            randomx_destroy_vm(currentVM);
            currentVM = nullptr;
        }
        if (currentDataset) {
            randomx_release_dataset(currentDataset);
            currentDataset = nullptr;
        }
        if (currentCache) {
            randomx_release_cache(currentCache);
            currentCache = nullptr;
        }
        
        // Allocate and initialize cache
        currentCache = randomx_alloc_cache(static_cast<randomx_flags>(RANDOMX_FLAG_DEFAULT));
        if (!currentCache) {
            threadSafePrint("Failed to allocate RandomX cache");
            return false;
        }
        
        threadSafePrint("Allocated RandomX cache");
        
        // Initialize cache with seed hash
        randomx_init_cache(currentCache, hexToBytes(seedHash).data(), seedHash.length() / 2);
        threadSafePrint("Initialized RandomX cache with seed hash");
        
        // Allocate and initialize dataset
        currentDataset = randomx_alloc_dataset(static_cast<randomx_flags>(RANDOMX_FLAG_DEFAULT));
        if (!currentDataset) {
            threadSafePrint("Failed to allocate RandomX dataset");
            return false;
        }
        
        threadSafePrint("Allocated RandomX dataset");
        
        // Initialize dataset using multiple threads
        const int threadCount = std::thread::hardware_concurrency();
        std::vector<std::thread> threads;
        
        threadSafePrint("Initializing dataset using " + std::to_string(threadCount) + " threads...");
        
        // Create local variables for the lambda
        randomx_dataset* dataset = currentDataset;
        randomx_cache* cache = currentCache;
        
        for (int i = 0; i < threadCount; ++i) {
            threads.emplace_back([i, threadCount, dataset, cache]() {
                try {
                    uint32_t startItem = (i * randomx_dataset_item_count()) / threadCount;
                    uint32_t itemCount = (randomx_dataset_item_count() / threadCount);
                    randomx_init_dataset(dataset, cache, startItem, itemCount);
                } catch (const std::exception& e) {
                    threadSafePrint("Error in dataset initialization thread: " + std::string(e.what()));
                }
            });
        }
        
        // Wait for all threads to complete
        for (auto& thread : threads) {
            thread.join();
        }
        
        threadSafePrint("Dataset initialization complete");
        
        // Save dataset to file
        saveDataset(currentDataset, datasetFile, seedHash);
        
        // Store the seed hash
        currentSeedHash = seedHash;
        
        return true;
    }

    void handleSeedHashChange(const std::string& newSeedHash) {
        if (newSeedHash.empty()) {
            threadSafePrint("Warning: Received empty seed hash", true);
            return;
        }
        
        if (currentSeedHash != newSeedHash) {
            threadSafePrint("Seed hash changed from " + (currentSeedHash.empty() ? "none" : currentSeedHash) + " to " + newSeedHash, true);
            initializeRandomX(newSeedHash);
        }
    }
} 