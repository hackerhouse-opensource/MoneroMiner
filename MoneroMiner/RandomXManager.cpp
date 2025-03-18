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

// RandomX constants
#define RANDOMX_DATASET_SIZE (RANDOMX_DATASET_ITEM_COUNT * RANDOMX_DATASET_ITEM_SIZE)
#define RANDOMX_DATASET_ITEM_COUNT 262144
#define RANDOMX_DATASET_ITEM_SIZE 64

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

        // Use Monero's required flags: JIT, LARGE_PAGES, HARD_AES, FULL_MEM
        RandomXFlags flags(RANDOMX_FLAG_JIT | RANDOMX_FLAG_HARD_AES | RANDOMX_FLAG_FULL_MEM | RANDOMX_FLAG_LARGE_PAGES);
        randomx_vm* vm = randomx_create_vm(flags.get(), currentCache, currentDataset);
        
        if (!vm) {
            // If large pages fail, try without them
            flags = RandomXFlags(RANDOMX_FLAG_JIT | RANDOMX_FLAG_HARD_AES | RANDOMX_FLAG_FULL_MEM);
            vm = randomx_create_vm(flags.get(), currentCache, currentDataset);
            if (!vm) {
                threadSafePrint("Failed to create RandomX VM");
                return nullptr;
            }
        }
        
        threadSafePrint("Successfully created RandomX VM");
        
        return vm;
    }

    void calculateHash(randomx_vm* vm, const uint8_t* input, size_t inputSize, uint8_t* output) {
        if (!vm || !input || !output) {
            threadSafePrint("Invalid parameters for hash calculation");
            return;
        }

        // First hash
        uint8_t firstHash[32];
        randomx_calculate_hash_first(vm, input, inputSize);
        randomx_calculate_hash_last(vm, firstHash);

        // Second hash - hash the result of the first hash
        randomx_calculate_hash_first(vm, firstHash, 32);
        randomx_calculate_hash_last(vm, output);
    }

    void destroyVM(randomx_vm* vm) {
        if (vm) {
            randomx_destroy_vm(vm);
        }
    }

    bool isDatasetValid(const std::string& seedHash) {
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

    bool saveDataset(const std::string& seedHash) {
        if (!currentDataset) {
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

        const uint8_t* data = static_cast<const uint8_t*>(randomx_get_dataset_memory(currentDataset));
        datasetFile.write(reinterpret_cast<const char*>(data), RANDOMX_DATASET_SIZE);
        datasetFile.close();

        threadSafePrint("Dataset saved successfully", true);
        return true;
    }

    bool loadDataset(const std::string& seedHash) {
        if (!currentDataset) {
            threadSafePrint("Cannot load dataset: no dataset allocated", true);
            return false;
        }

        std::ifstream datasetFile("randomx_dataset.bin", std::ios::binary);
        if (!datasetFile.is_open()) {
            threadSafePrint("Failed to open dataset file for reading", true);
            return false;
        }

        uint8_t* data = static_cast<uint8_t*>(randomx_get_dataset_memory(currentDataset));
        datasetFile.read(reinterpret_cast<char*>(data), RANDOMX_DATASET_SIZE);
        datasetFile.close();

        threadSafePrint("Dataset loaded successfully", true);
        return true;
    }

    bool initializeRandomX(const std::string& seedHash) {
        std::lock_guard<std::mutex> lock(initMutex);
        
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

        // Check if we have a valid dataset for this seed hash
        if (isDatasetValid(seedHash)) {
            threadSafePrint("Using existing dataset", true);
            currentCache = randomx_alloc_cache(RANDOMX_FLAG_DEFAULT);
            if (!currentCache) {
                threadSafePrint("Failed to allocate RandomX cache", true);
                return false;
            }
            threadSafePrint("Allocated RandomX cache", true);

            currentDataset = randomx_alloc_dataset(RANDOMX_FLAG_DEFAULT);
            if (!currentDataset) {
                threadSafePrint("Failed to allocate RandomX dataset", true);
                randomx_release_cache(currentCache);
                currentCache = nullptr;
                return false;
            }
            threadSafePrint("Allocated RandomX dataset", true);

            if (!loadDataset(seedHash)) {
                threadSafePrint("Failed to load dataset", true);
                randomx_release_dataset(currentDataset);
                randomx_release_cache(currentCache);
                currentDataset = nullptr;
                currentCache = nullptr;
                return false;
            }

            currentVM = randomx_create_vm(RANDOMX_FLAG_DEFAULT, currentCache, currentDataset);
            if (!currentVM) {
                threadSafePrint("Failed to create RandomX VM", true);
                randomx_release_dataset(currentDataset);
                randomx_release_cache(currentCache);
                currentDataset = nullptr;
                currentCache = nullptr;
                return false;
            }
            threadSafePrint("Created RandomX VM", true);
            return true;
        }

        // Initialize new dataset
        currentCache = randomx_alloc_cache(RANDOMX_FLAG_DEFAULT);
        if (!currentCache) {
            threadSafePrint("Failed to allocate RandomX cache", true);
            return false;
        }
        threadSafePrint("Allocated RandomX cache", true);

        currentDataset = randomx_alloc_dataset(RANDOMX_FLAG_DEFAULT);
        if (!currentDataset) {
            threadSafePrint("Failed to allocate RandomX dataset", true);
            randomx_release_cache(currentCache);
            currentCache = nullptr;
            return false;
        }
        threadSafePrint("Allocated RandomX dataset", true);

        // Initialize cache with seed hash
        randomx_init_cache(currentCache, seedHash.c_str(), seedHash.length());
        threadSafePrint("Initialized RandomX cache with seed hash", true);

        // Initialize dataset using multiple threads
        const int numThreads = std::thread::hardware_concurrency();
        threadSafePrint("Initializing dataset using " + std::to_string(numThreads) + " threads...", true);
        
        std::vector<std::thread> threads;
        for (int i = 0; i < numThreads; i++) {
            threads.emplace_back([i, numThreads]() {
                try {
                    uint64_t startItem = (RANDOMX_DATASET_ITEM_COUNT * i) / numThreads;
                    uint64_t itemCount = (RANDOMX_DATASET_ITEM_COUNT * (i + 1)) / numThreads - startItem;
                    randomx_init_dataset(currentDataset, currentCache, 
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
            randomx_release_dataset(currentDataset);
            randomx_release_cache(currentCache);
            currentDataset = nullptr;
            currentCache = nullptr;
            return false;
        }

        currentVM = randomx_create_vm(RANDOMX_FLAG_DEFAULT, currentCache, currentDataset);
        if (!currentVM) {
            threadSafePrint("Failed to create RandomX VM", true);
            randomx_release_dataset(currentDataset);
            randomx_release_cache(currentCache);
            currentDataset = nullptr;
            currentCache = nullptr;
            return false;
        }
        threadSafePrint("Created RandomX VM", true);
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