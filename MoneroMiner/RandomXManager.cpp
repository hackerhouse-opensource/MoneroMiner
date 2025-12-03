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

#ifndef RANDOMX_DATASET_ITEM_SIZE
#define RANDOMX_DATASET_ITEM_SIZE 64ULL
#endif

static constexpr size_t MAX_BLOB_SIZE = 128;

// Now using properly compiled RandomX library - can use full mode
int RandomXManager::flags = RANDOMX_FLAG_DEFAULT;
static int cacheAllocFlags = RANDOMX_FLAG_DEFAULT;
static bool useLightMode = false;  // Full mode now that CRT matches

extern bool debugMode;
extern std::atomic<bool> showedInitMessage;

// Static member initialization
std::shared_mutex RandomXManager::vmMutex;
std::mutex RandomXManager::datasetMutex;
std::mutex RandomXManager::seedHashMutex;
std::mutex RandomXManager::initMutex;
std::mutex RandomXManager::hashMutex;
std::mutex RandomXManager::cacheMutex;
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
uint256_t RandomXManager::expandedTarget;
uint256_t RandomXManager::hashValue;
uint32_t RandomXManager::currentTarget = 0;
std::string RandomXManager::lastHashHex;
double RandomXManager::currentDifficulty;

bool RandomXManager::initializeCache(const std::string& seedHash) {
    std::lock_guard<std::mutex> lock(cacheMutex);
    
    if (cache != nullptr) {
        if (currentSeedHash == seedHash) {
            return true;
        }
        randomx_release_cache(cache);
        cache = nullptr;
    }

    int detectedFlags = randomx_get_flags();
    Utils::threadSafePrint("Detected CPU flags: 0x" + Utils::formatHex(detectedFlags, 8), true);
    
    if (useLightMode) {
        // Light mode: no FULL_MEM, simpler memory model
        cacheAllocFlags = detectedFlags & ~RANDOMX_FLAG_FULL_MEM;
        flags = cacheAllocFlags;  // Same flags for VM in light mode
        Utils::threadSafePrint("Using LIGHT MODE (more compatible, slower)", true);
    } else {
        cacheAllocFlags = detectedFlags & ~RANDOMX_FLAG_FULL_MEM;
        flags = detectedFlags | RANDOMX_FLAG_FULL_MEM;
    }
    
    Utils::threadSafePrint("Cache flags: 0x" + Utils::formatHex(cacheAllocFlags, 8), true);
    Utils::threadSafePrint("VM flags: 0x" + Utils::formatHex(flags, 8), true);
    
    cache = randomx_alloc_cache(static_cast<randomx_flags>(cacheAllocFlags));
    if (!cache) {
        Utils::threadSafePrint("Cache allocation failed, trying default", true);
        cache = randomx_alloc_cache(RANDOMX_FLAG_DEFAULT);
        if (!cache) {
            Utils::threadSafePrint("Cache allocation failed completely", true);
            return false;
        }
        cacheAllocFlags = RANDOMX_FLAG_DEFAULT;
        flags = RANDOMX_FLAG_DEFAULT;
    }

    std::vector<uint8_t> seedBytes = Utils::hexToBytes(seedHash);
    randomx_init_cache(cache, seedBytes.data(), seedBytes.size());
    Utils::threadSafePrint("Cache initialized with seed hash", true);
    currentSeedHash = seedHash;
    return true;
}

bool RandomXManager::createDataset() {
    if (!cache) {
        Utils::threadSafePrint("Cannot create dataset: no cache", true);
        return false;
    }

    if (dataset) {
        randomx_release_dataset(dataset);
        dataset = nullptr;
    }

    Utils::threadSafePrint("Allocating dataset with flags: 0x" + Utils::formatHex(flags, 8), true);
    
    // Use the same flags that will be used for VM (includes FULL_MEM)
    dataset = randomx_alloc_dataset(static_cast<randomx_flags>(flags));
    if (!dataset) {
        Utils::threadSafePrint("Dataset allocation failed, trying FULL_MEM only", true);
        flags = RANDOMX_FLAG_FULL_MEM;
        dataset = randomx_alloc_dataset(RANDOMX_FLAG_FULL_MEM);
        if (!dataset) {
            Utils::threadSafePrint("Dataset allocation failed", true);
            return false;
        }
    }

    unsigned long itemCount = randomx_dataset_item_count();
    Utils::threadSafePrint("Initializing " + std::to_string(itemCount) + " dataset items...", true);

    unsigned int numThreads = std::thread::hardware_concurrency();
    if (numThreads == 0) numThreads = 1;
    
    auto startTime = std::chrono::high_resolution_clock::now();
    
    std::vector<std::thread> threads;
    unsigned long itemsPerThread = itemCount / numThreads;
    
    randomx_dataset* localDataset = dataset;
    randomx_cache* localCache = cache;
    
    for (unsigned int t = 0; t < numThreads; t++) {
        unsigned long start = t * itemsPerThread;
        unsigned long count = (t == numThreads - 1) ? (itemCount - start) : itemsPerThread;
        
        threads.emplace_back([localDataset, localCache, start, count, t]() {
            randomx_init_dataset(localDataset, localCache, start, count);
        });
    }
    
    for (auto& thread : threads) {
        thread.join();
    }
    
    auto endTime = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::seconds>(endTime - startTime);
    Utils::threadSafePrint("Dataset initialized in " + std::to_string(duration.count()) + " seconds", true);
    
    return true;
}

bool RandomXManager::warmupDataset() {
    // Warmup is optional - if it fails, mining still works
    // The OS will page-fault data into memory on first access anyway
    
    if (!dataset) {
        Utils::threadSafePrint("Warmup skipped: no dataset", true);
        return false;
    }
    
    void* datasetMem = randomx_get_dataset_memory(dataset);
    if (!datasetMem) {
        Utils::threadSafePrint("Warmup skipped: null dataset memory", true);
        return false;
    }
    
    Utils::threadSafePrint("Warming up dataset cache...", true);
    auto startTime = std::chrono::high_resolution_clock::now();
    
    // Use library-reported item count for accurate size calculation
    unsigned long itemCount = randomx_dataset_item_count();
    const size_t actualSize = static_cast<size_t>(itemCount) * RANDOMX_DATASET_ITEM_SIZE;
    const size_t pageSize = 4096;
    const size_t numPages = actualSize / pageSize;
    
    Utils::threadSafePrint("Dataset actual size: " + std::to_string(actualSize / (1024*1024)) + " MB (" + 
        std::to_string(numPages) + " pages)", true);
    
    // Simple single-threaded warmup to avoid race conditions
    // This is safer and warmup is a one-time cost anyway
    try {
        uint8_t* mem = static_cast<uint8_t*>(datasetMem);
        volatile uint8_t dummy = 0;
        
        // Touch every 64th page (256KB stride) for speed while still warming TLB
        const size_t stride = 64;
        for (size_t page = 0; page < numPages; page += stride) {
            size_t offset = page * pageSize;
            if (offset < actualSize) {
                dummy ^= mem[offset];
            }
        }
        
        auto endTime = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime);
        
        Utils::threadSafePrint("Dataset warmed up in " + std::to_string(duration.count()) + " ms", true);
        return true;
    }
    catch (...) {
        Utils::threadSafePrint("Warmup failed (non-fatal, continuing)", true);
        return false;
    }
}

bool RandomXManager::initialize(const std::string& seedHash) {
    std::lock_guard<std::mutex> lock(initMutex);
    
    if (seedHash == currentSeedHash && cache != nullptr && initialized) {
        // In light mode, we don't need dataset
        if (useLightMode || dataset != nullptr) {
            Utils::threadSafePrint("RandomX already initialized for seed hash: " + seedHash, true);
            return true;
        }
    }

    Utils::threadSafePrint("Initializing RandomX cache...", true);
    auto cacheStart = std::chrono::high_resolution_clock::now();
    
    if (!initializeCache(seedHash)) {
        Utils::threadSafePrint("Failed to initialize RandomX cache", true);
        return false;
    }
    
    auto cacheEnd = std::chrono::high_resolution_clock::now();
    auto cacheDuration = std::chrono::duration_cast<std::chrono::milliseconds>(cacheEnd - cacheStart);
    Utils::threadSafePrint("Cache initialized in " + std::to_string(cacheDuration.count()) + " ms", true);
    
    // Skip dataset in light mode
    if (!useLightMode) {
        // Step 2: Try to load or create dataset
        std::string datasetFileName = "randomx_dataset_" + seedHash + ".bin";
        bool loadedDataset = false;
        
        // Delete old dataset files that have wrong size
        if (std::filesystem::exists(datasetFileName)) {
            auto fileSize = std::filesystem::file_size(datasetFileName);
            unsigned long itemCount = randomx_dataset_item_count();
            size_t expectedMinSize = static_cast<size_t>(itemCount) * RANDOMX_DATASET_ITEM_SIZE;
            
            if (fileSize < expectedMinSize) {
                Utils::threadSafePrint("Dataset file has wrong size (" + std::to_string(fileSize) + 
                    " bytes), deleting...", true);
                std::filesystem::remove(datasetFileName);
            }
        }
        
        if (std::filesystem::exists(datasetFileName)) {
            Utils::threadSafePrint("Found existing dataset file, loading from disk...", true);
            auto loadStart = std::chrono::high_resolution_clock::now();
            
            if (loadDataset(datasetFileName)) {
                auto loadEnd = std::chrono::high_resolution_clock::now();
                auto loadDuration = std::chrono::duration_cast<std::chrono::milliseconds>(loadEnd - loadStart);
                
                Utils::threadSafePrint("Dataset loaded from disk in " + 
                    std::to_string(loadDuration.count() / 1000.0) + " seconds", true);
                loadedDataset = true;
            } else {
                Utils::threadSafePrint("Failed to load dataset, will recreate", true);
                try {
                    std::filesystem::remove(datasetFileName);
                    Utils::threadSafePrint("Deleted corrupted dataset file", true);
                } catch (const std::exception& e) {
                    Utils::threadSafePrint("Failed to delete dataset file: " + std::string(e.what()), true);
                }
            }
        }

        if (!loadedDataset) {
            // Create new dataset
            Utils::threadSafePrint("=== CREATING RANDOMX DATASET ===", true);
            Utils::threadSafePrint("This will take 1-2 minutes but is done only once per seed hash change (~2 days)", true);
            
            if (!createDataset()) {
                Utils::threadSafePrint("Failed to create dataset", true);
                return false;
            }

            // Save dataset for future use
            Utils::threadSafePrint("Saving dataset to disk for future fast startup...", true);
            auto saveStart = std::chrono::high_resolution_clock::now();
            
            if (saveDataset(datasetFileName)) {
                auto saveEnd = std::chrono::high_resolution_clock::now();
                auto saveDuration = std::chrono::duration_cast<std::chrono::milliseconds>(saveEnd - saveStart);
                
                Utils::threadSafePrint("Dataset saved in " + 
                    std::to_string(saveDuration.count() / 1000.0) + " seconds", true);
            } else {
                Utils::threadSafePrint("Warning: Failed to save dataset (will need to recreate next time)", true);
            }
        }
    } else {
        Utils::threadSafePrint("Skipping dataset (light mode)", true);
    }

    currentSeedHash = seedHash;
    initialized = true;
    
    Utils::threadSafePrint("=== RANDOMX READY FOR MINING ===", true);
    Utils::threadSafePrint("Mode: " + std::string(useLightMode ? "LIGHT (compatible)" : "FULL_MEM + JIT"), true);
    Utils::threadSafePrint("Flags: 0x" + Utils::formatHex(flags, 8), true);
    
    return true;
}

bool RandomXManager::createVM(int threadId) {
    std::unique_lock<std::shared_mutex> lock(vmMutex);
    
    if (!initialized || !cache) {
        Utils::threadSafePrint("Cannot create VM: not initialized", true);
        return false;
    }
    
    // In light mode, dataset can be null
    if (!useLightMode && !dataset) {
        Utils::threadSafePrint("Cannot create VM: no dataset (full mode)", true);
        return false;
    }

    auto it = vms.find(threadId);
    if (it != vms.end() && it->second != nullptr) {
        return true;
    }

    Utils::threadSafePrint("Creating VM for thread " + std::to_string(threadId) + 
        " with flags 0x" + Utils::formatHex(flags, 8) + 
        (useLightMode ? " (light mode)" : " (full mode)"), true);
    
    // In light mode, pass nullptr for dataset
    randomx_vm* vm = randomx_create_vm(
        static_cast<randomx_flags>(flags), 
        cache, 
        useLightMode ? nullptr : dataset
    );
    
    if (!vm) {
        Utils::threadSafePrint("VM creation failed, trying with default flags", true);
        vm = randomx_create_vm(RANDOMX_FLAG_DEFAULT, cache, nullptr);
        if (!vm) {
            Utils::threadSafePrint("Failed to create VM", true);
            return false;
        }
        flags = RANDOMX_FLAG_DEFAULT;
    }

    vms[threadId] = vm;
    Utils::threadSafePrint("VM created successfully for thread " + std::to_string(threadId), true);
    return true;
}

bool RandomXManager::initializeVM(int threadId) {
    if (!initialized) {
        Utils::threadSafePrint("Cannot initialize VM: RandomX not initialized", true);
        return false;
    }

    if (!createVM(threadId)) {
        Utils::threadSafePrint("Failed to create VM for thread " + std::to_string(threadId), true);
        return false;
    }

    return true;
}

randomx_vm* RandomXManager::getVM(int threadId) {
    std::shared_lock<std::shared_mutex> lock(vmMutex);  // Shared lock for reading
    if (vms.count(threadId)) {
        return vms[threadId];
    }
    return nullptr;
}

bool RandomXManager::loadDataset(const std::string& seedHash) {
    // Get the actual dataset size from the library
    unsigned long itemCount = randomx_dataset_item_count();
    size_t actualDatasetSize = static_cast<size_t>(itemCount) * RANDOMX_DATASET_ITEM_SIZE;
    
    // Allocate dataset if not already allocated
    if (!dataset) {
        dataset = randomx_alloc_dataset(static_cast<randomx_flags>(flags));
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
        if (fileDatasetSize != actualDatasetSize) {
            threadSafePrint("Invalid dataset size in file: " + std::to_string(fileDatasetSize) + 
                " vs expected " + std::to_string(actualDatasetSize), true);
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

        file.read(reinterpret_cast<char*>(datasetMemory), actualDatasetSize);
        file.close();
        threadSafePrint("Dataset loaded successfully (" + std::to_string(actualDatasetSize / (1024*1024)) + " MB)", true);
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

    // Get the actual dataset size from the library
    unsigned long itemCount = randomx_dataset_item_count();
    size_t actualDatasetSize = static_cast<size_t>(itemCount) * RANDOMX_DATASET_ITEM_SIZE;

    std::ofstream file(getDatasetPath(seedHash), std::ios::binary);
    if (!file.is_open()) {
        threadSafePrint("Failed to open dataset file for writing: " + getDatasetPath(seedHash), true);
        return false;
    }

    try {
        // Write dataset size
        uint64_t datasetSize = actualDatasetSize;
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

        file.write(reinterpret_cast<const char*>(datasetMemory), actualDatasetSize);
        file.close();
        threadSafePrint("Dataset saved successfully (" + std::to_string(actualDatasetSize / (1024*1024)) + " MB)", true);
        return true;
    }
    catch (const std::exception& e) {
        threadSafePrint("Error saving dataset: " + std::string(e.what()), true);
        file.close();
        return false;
    }
}

void RandomXManager::cleanupVM(int threadId) {
    std::unique_lock<std::shared_mutex> lock(vmMutex);  // Exclusive lock for destruction
    
    auto it = vms.find(threadId);
    if (it != vms.end() && it->second != nullptr) {
        Utils::threadSafePrint("Cleaning up VM for thread " + std::to_string(threadId), true);
        randomx_destroy_vm(it->second);
        vms.erase(it);
    }
}

void RandomXManager::cleanup() {
    std::lock_guard<std::mutex> lock(initMutex);
    
    // Cleanup VMs with exclusive lock
    {
        std::unique_lock<std::shared_mutex> vmLock(vmMutex);  // Exclusive lock
        for (auto& [threadId, vm] : vms) {
            if (vm) {
                Utils::threadSafePrint("Cleaning up VM for thread " + std::to_string(threadId), true);
                randomx_destroy_vm(vm);
            }
        }
        vms.clear();
    }

    // Cleanup cache and dataset
    if (cache) {
        Utils::threadSafePrint("Releasing RandomX cache", true);
        randomx_release_cache(cache);
        cache = nullptr;
    }

    if (dataset) {
        Utils::threadSafePrint("Releasing RandomX dataset", true);
        randomx_release_dataset(dataset);
        dataset = nullptr;
    }

    initialized = false;
    currentSeedHash.clear();
    Utils::threadSafePrint("RandomX cleanup complete", true);
}

bool RandomXManager::calculateHash(randomx_vm* vm, const std::vector<uint8_t>& input, uint64_t nonce) {
    // CRITICAL: Hold shared lock for ENTIRE hash calculation
    // This prevents VM destruction while we're using it
    std::shared_lock<std::shared_mutex> vmLock(vmMutex);
    
    if (!vm) {
        Utils::threadSafePrint("ERROR: VM is null", true);
        return false;
    }
    
    // Verify VM is still valid in our map
    bool vmValid = false;
    for (const auto& [id, storedVm] : vms) {
        if (storedVm == vm) {
            vmValid = true;
            break;
        }
    }
    if (!vmValid) {
        Utils::threadSafePrint("ERROR: VM no longer valid (was destroyed)", true);
        return false;
    }
    
    if (!initialized || !dataset) {
        Utils::threadSafePrint("ERROR: RandomX not initialized", true);
        return false;
    }
    
    if (input.empty()) {
        Utils::threadSafePrint("ERROR: Input is empty", true);
        return false;
    }
    
    size_t blobSize = input.size();
    if (blobSize > MAX_BLOB_SIZE) {
        Utils::threadSafePrint("ERROR: Input too large: " + std::to_string(blobSize), true);
        return false;
    }
    
    // Use stack buffer with consistent size
    uint8_t blob[MAX_BLOB_SIZE];
    std::memset(blob, 0, sizeof(blob));
    std::memcpy(blob, input.data(), blobSize);
    
    // Write nonce at position 39 (little-endian) - Monero nonce position
    if (blobSize >= 43) {
        uint32_t nonce32 = static_cast<uint32_t>(nonce);
        blob[39] = (nonce32 >> 0) & 0xFF;
        blob[40] = (nonce32 >> 8) & 0xFF;
        blob[41] = (nonce32 >> 16) & 0xFF;
        blob[42] = (nonce32 >> 24) & 0xFF;
    }
    
    uint8_t hash[RANDOMX_HASH_SIZE];
    std::memset(hash, 0, sizeof(hash));
    
    // Calculate hash - VM is protected by shared lock
    randomx_calculate_hash(vm, blob, blobSize, hash);
    
    // Check target (still under shared lock to ensure consistency)
    bool valid = checkTarget(hash);
    
    if (valid) {
        std::lock_guard<std::mutex> hashLock(hashMutex);
        lastHash.assign(hash, hash + RANDOMX_HASH_SIZE);
        Utils::threadSafePrint("!!! SHARE FOUND !!! Nonce: " + std::to_string(nonce), true);
    }
    
    return valid;
    // vmLock releases here - VM destruction can now proceed if pending
}

bool RandomXManager::verifyVM(randomx_vm* vm) {
    if (!vm || !cache || !dataset) {
        return false;
    }
    
    // Don't lock vmMutex here as it may already be locked by caller
    // Just verify VM memory accessibility
    try {
        volatile uint8_t* vmMemTest = static_cast<uint8_t*>(randomx_get_dataset_memory(dataset));
        if (!vmMemTest) {
            return false;
        }
    } catch (...) {
        return false;
    }
    
    return true;
}

bool RandomXManager::prepareInput(const std::vector<uint8_t>& input, uint64_t nonce, std::vector<uint8_t>& localInput) {
    if (input.empty()) {
        Utils::threadSafePrint("Error: Empty input", true);
        return false;
    }

    try {
        localInput = input;
        localInput.reserve(localInput.size() + sizeof(nonce));
        
        // Append nonce in little-endian
        for (size_t i = 0; i < sizeof(nonce); ++i) {
            localInput.push_back(static_cast<uint8_t>((nonce >> (i * 8)) & 0xFF));
        }

        return true;
    }
    catch (const std::exception& e) {
        Utils::threadSafePrint("Error preparing input: " + std::string(e.what()), true);
        return false;
    }
}

bool RandomXManager::verifyHashResult(const uint8_t* hashBuffer) {
    if (!hashBuffer) {
        return false;
    }

    for (int i = 0; i < RANDOMX_HASH_SIZE; i++) {
        if (hashBuffer[i] != 0) {
            return true;
        }
    }
    
    return false;
}

bool RandomXManager::processHashResult(const uint8_t* hashBuffer) {
    try {
        bool isValid = checkTarget(hashBuffer);
        
        if (isValid) {
            std::lock_guard<std::mutex> lock(hashMutex);
            lastHash.assign(hashBuffer, hashBuffer + RANDOMX_HASH_SIZE);
        }

        return isValid;
    }
    catch (const std::exception& e) {
        Utils::threadSafePrint("Error processing hash result: " + std::string(e.what()), true);
        return false;
    }
}

bool RandomXManager::checkTarget(const uint8_t* hash) {
    if (!hash) {
        return false;
    }

    // Convert hash to uint256_t (little-endian comparison for Monero)
    // The hash bytes are in big-endian order from RandomX
    uint256_t localHashValue;
    localHashValue.clear();
    
    for (int i = 0; i < 32; i++) {
        int wordIndex = (31 - i) / 8;
        int bytePosition = 7 - (i % 8);
        localHashValue.words[wordIndex] |= static_cast<uint64_t>(hash[i]) << (bytePosition * 8);
    }

    bool isValid = false;
    {
        std::lock_guard<std::mutex> lock(hashMutex);
        isValid = localHashValue <= expandedTarget;
    }

    return isValid;
}

std::string RandomXManager::getLastHashHex() {
    std::lock_guard<std::mutex> lock(hashMutex);
    return Utils::bytesToHex(lastHash);
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
    std::lock_guard<std::mutex> lock(datasetMutex);
    
    // Get actual dataset size from library
    unsigned long itemCount = randomx_dataset_item_count();
    size_t actualDatasetSize = static_cast<size_t>(itemCount) * RANDOMX_DATASET_ITEM_SIZE;
    
    if (dataset != nullptr) {
        if (currentSeedHash == seedHash) {
            Utils::threadSafePrint("RandomX already initialized for seed hash: " + seedHash, true);
            return;
        }
        randomx_release_dataset(dataset);
        dataset = nullptr;
    }

    // Try to load existing dataset
    std::string datasetPath = getDatasetPath(seedHash);
    std::ifstream datasetFile(datasetPath, std::ios::binary);
    
    if (datasetFile.good()) {
        dataset = randomx_alloc_dataset(static_cast<randomx_flags>(flags));
        if (!dataset) {
            Utils::threadSafePrint("Failed to allocate memory for dataset", true);
            return;
        }

        datasetFile.read(reinterpret_cast<char*>(randomx_get_dataset_memory(dataset)), actualDatasetSize);
        if (static_cast<size_t>(datasetFile.gcount()) == actualDatasetSize) {
            Utils::threadSafePrint("Dataset loaded successfully", true);
            Utils::threadSafePrint("Loaded existing dataset from: " + datasetPath, true);
            currentSeedHash = seedHash;
            return;
        }
        
        randomx_release_dataset(dataset);
        dataset = nullptr;
    }

    // Create new dataset if loading failed
    Utils::threadSafePrint("Failed to open dataset file for reading: " + datasetPath, true);
    Utils::threadSafePrint("Creating new RandomX dataset...", true);
    
    dataset = randomx_alloc_dataset(static_cast<randomx_flags>(flags));
    if (!dataset) {
        Utils::threadSafePrint("Failed to allocate memory for dataset", true);
        return;
    }

    Utils::threadSafePrint("Initializing dataset...", true);
    randomx_init_dataset(dataset, cache, 0, itemCount);
    
    // Save dataset to file
    std::ofstream outFile(datasetPath, std::ios::binary);
    if (outFile.good()) {
        outFile.write(reinterpret_cast<const char*>(randomx_get_dataset_memory(dataset)), actualDatasetSize);
        Utils::threadSafePrint("Dataset saved to file: " + datasetPath, true);
    } else {
        Utils::threadSafePrint("Failed to save dataset to file: " + datasetPath, true);
    }

    currentSeedHash = seedHash;
    Utils::threadSafePrint("Dataset initialization complete", true);
}

std::string RandomXManager::getDatasetPath(const std::string& seedHash) {
    // Remove the redundant "randomx_dataset_" prefix from the filename
    return "randomx_dataset_" + seedHash + ".bin";
}

void RandomXManager::handleSeedHashChange(const std::string& newSeedHash) {
    std::lock_guard<std::mutex> lock(seedHashMutex);
    
    if (newSeedHash != currentSeedHash) {
        Utils::threadSafePrint("Seed hash changing from " + currentSeedHash + " to " + newSeedHash, true);
        
        // CRITICAL: Acquire EXCLUSIVE lock - this will WAIT for all shared locks
        // (i.e., all in-flight hash calculations) to complete before proceeding
        {
            Utils::threadSafePrint("Waiting for in-flight hash calculations to complete...", true);
            std::unique_lock<std::shared_mutex> vmLock(vmMutex);
            Utils::threadSafePrint("All hash calculations complete, destroying VMs...", true);
            
            for (auto& [threadId, vm] : vms) {
                if (vm) {
                    Utils::threadSafePrint("Destroying VM for thread " + std::to_string(threadId), true);
                    randomx_destroy_vm(vm);
                }
            }
            vms.clear();
            Utils::threadSafePrint("All VMs destroyed", true);
        }

        // Initialize with new seed hash
        if (!initialize(newSeedHash)) {
            Utils::threadSafePrint("Failed to initialize RandomX with new seed hash: " + newSeedHash, true);
            return;
        }

        // Notify all mining threads to reinitialize their VMs
        for (auto* data : threadData) {
            if (data) {
                data->updateJob(Job());
            }
        }
        
        Utils::threadSafePrint("Seed hash change complete", true);
    }
}

void RandomXManager::destroyVM(randomx_vm* vm) {
    if (vm) {
        std::unique_lock<std::shared_mutex> lock(vmMutex);  // Exclusive lock for destruction
        
        // Find and remove the VM from the vms map
        for (auto it = vms.begin(); it != vms.end(); ++it) {
            if (it->second == vm) {
                Utils::threadSafePrint("Destroying VM for thread " + std::to_string(it->first), true);
                randomx_destroy_vm(vm);
                vms.erase(it);
                break;
            }
        }
    }
}

bool RandomXManager::setTargetAndDifficulty(const std::string& targetHex) {
    // Convert hex target to uint32_t
    uint32_t compactTarget = std::stoul(targetHex, nullptr, 16);
    
    // Reset expanded target
    expandedTarget.clear();
    
    // For target 0xf3220000:
    // The first byte (0xf3) is the exponent (243 decimal)
    // The actual target value is 0x220000
    uint8_t exponent = (compactTarget >> 24) & 0xFF;
    uint32_t mantissa = compactTarget & 0x00FFFFFF;
    
    // Calculate difficulty as (2^32) / mantissa
    double calculatedDifficulty = static_cast<double>(0xFFFFFFFF) / mantissa;
    
    // Place mantissa at the beginning of the 256-bit number
    expandedTarget.words[3] = static_cast<uint64_t>(mantissa) << 40;  // Put mantissa in most significant bits
    expandedTarget.words[2] = 0;
    expandedTarget.words[1] = 0;
    expandedTarget.words[0] = 0;

    if (config.debugMode) {
        std::stringstream ss;
        ss << "Target expansion details:" << std::endl;
        ss << "  Compact target: 0x" << targetHex << std::endl;
        ss << "  Exponent: " << std::dec << (int)exponent << " (0x" << std::hex << (int)exponent << ")" << std::endl;
        ss << "  Mantissa: 0x" << std::hex << std::setw(6) << std::setfill('0') << mantissa << std::endl;
        ss << "\nExpanded target (256-bit):" << std::endl;
        ss << "0x";
        for (int i = 3; i >= 0; i--) {
            ss << std::hex << std::setw(16) << std::setfill('0') << expandedTarget.words[i];
        }
        ss << std::endl;
        ss << "Calculated difficulty: " << std::fixed << std::setprecision(6) << calculatedDifficulty << std::endl;
        Utils::threadSafePrint(ss.str());
    }

    currentDifficulty = calculatedDifficulty;
    return true;
}