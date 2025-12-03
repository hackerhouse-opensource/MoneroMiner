#include "RandomXManager.h"
#include "Globals.h"
#include "Utils.h"
#include "Types.h"
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

// Static member initialization
int RandomXManager::flags = RANDOMX_FLAG_DEFAULT;
static int cacheAllocFlags = RANDOMX_FLAG_DEFAULT;

std::shared_mutex RandomXManager::vmMutex;
std::mutex RandomXManager::initMutex;
std::mutex RandomXManager::hashMutex;
std::mutex RandomXManager::cacheMutex;
std::mutex RandomXManager::seedHashMutex;
std::unordered_map<int, randomx_vm*> RandomXManager::vms;
randomx_cache* RandomXManager::cache = nullptr;
randomx_dataset* RandomXManager::dataset = nullptr;
std::string RandomXManager::currentSeedHash;
bool RandomXManager::initialized = false;
bool RandomXManager::useLightMode = false;
std::vector<uint8_t> RandomXManager::lastHash;
uint256_t RandomXManager::expandedTarget;
double RandomXManager::currentDifficulty = 0.0;

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
    Utils::threadSafePrint("Detected CPU flags: 0x" + Utils::formatHex(static_cast<uint64_t>(detectedFlags), 8), true);
    
    cacheAllocFlags = detectedFlags & ~RANDOMX_FLAG_FULL_MEM;
    flags = detectedFlags | RANDOMX_FLAG_FULL_MEM;
    useLightMode = false;
    
    Utils::threadSafePrint("Mode: FULL (2GB dataset)", true);
    Utils::threadSafePrint("Cache flags: 0x" + Utils::formatHex(static_cast<uint64_t>(cacheAllocFlags), 8), true);
    Utils::threadSafePrint("VM/Dataset flags: 0x" + Utils::formatHex(static_cast<uint64_t>(flags), 8), true);
    
    cache = randomx_alloc_cache(static_cast<randomx_flags>(cacheAllocFlags));
    if (!cache) {
        Utils::threadSafePrint("Cache allocation failed, trying default flags", true);
        cache = randomx_alloc_cache(RANDOMX_FLAG_DEFAULT);
        if (!cache) {
            Utils::threadSafePrint("Cache allocation failed completely", true);
            return false;
        }
        cacheAllocFlags = RANDOMX_FLAG_DEFAULT;
        flags = RANDOMX_FLAG_DEFAULT;
        useLightMode = true;
        Utils::threadSafePrint("WARNING: Falling back to LIGHT mode", true);
    }

    std::vector<uint8_t> seedBytes = Utils::hexToBytes(seedHash);
    if (seedBytes.size() != 32) {
        Utils::threadSafePrint("ERROR: Invalid seed hash length: " + std::to_string(seedBytes.size()), true);
        return false;
    }
    
    randomx_init_cache(cache, seedBytes.data(), seedBytes.size());
    Utils::threadSafePrint("Cache initialized with seed hash: " + seedHash.substr(0, 16) + "...", true);
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

    Utils::threadSafePrint("Allocating dataset with flags: 0x" + Utils::formatHex(static_cast<uint64_t>(flags), 8), true);
    
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
    
    for (unsigned int t = 0; t < numThreads; t++) {
        unsigned long start = t * itemsPerThread;
        unsigned long count = (t == numThreads - 1) ? (itemCount - start) : itemsPerThread;
        threads.emplace_back([start, count]() {
            randomx_init_dataset(dataset, cache, start, count);
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

bool RandomXManager::initialize(const std::string& seedHash) {
    std::lock_guard<std::mutex> lock(initMutex);
    
    if (seedHash == currentSeedHash && cache != nullptr && initialized) {
        if (useLightMode || dataset != nullptr) {
            Utils::threadSafePrint("RandomX already initialized for seed hash", true);
            return true;
        }
    }

    Utils::threadSafePrint("=== INITIALIZING RANDOMX ===", true);
    Utils::threadSafePrint("Seed hash: " + seedHash, true);
    
    if (!initializeCache(seedHash)) {
        Utils::threadSafePrint("Failed to initialize RandomX cache", true);
        return false;
    }
    
    if (!useLightMode) {
        std::string datasetFileName = "randomx_dataset_" + seedHash.substr(0, 16) + ".bin";
        bool loadedDataset = false;
        
        if (std::filesystem::exists(datasetFileName)) {
            auto fileSize = std::filesystem::file_size(datasetFileName);
            unsigned long itemCount = randomx_dataset_item_count();
            size_t expectedMinSize = static_cast<size_t>(itemCount) * RANDOMX_DATASET_ITEM_SIZE;
            
            if (fileSize >= expectedMinSize) {
                Utils::threadSafePrint("Loading dataset from disk...", true);
                if (loadDataset(datasetFileName)) {
                    loadedDataset = true;
                }
            } else {
                std::filesystem::remove(datasetFileName);
            }
        }

        if (!loadedDataset) {
            Utils::threadSafePrint("=== CREATING 2GB RANDOMX DATASET ===", true);
            if (!createDataset()) {
                useLightMode = true;
                flags = cacheAllocFlags;
            } else {
                saveDataset(datasetFileName);
            }
        }
    }

    currentSeedHash = seedHash;
    initialized = true;
    
    Utils::threadSafePrint("=== RANDOMX READY ===", true);
    Utils::threadSafePrint("Flags: 0x" + Utils::formatHex(static_cast<uint64_t>(flags), 8), true);
    
    return true;
}

bool RandomXManager::createVM(int threadId) {
    std::unique_lock<std::shared_mutex> lock(vmMutex);
    
    if (!initialized || !cache) {
        Utils::threadSafePrint("Cannot create VM: RandomX not initialized", true);
        return false;
    }
    
    if (!useLightMode && !dataset) {
        Utils::threadSafePrint("Cannot create VM: dataset required for full mode", true);
        return false;
    }

    auto it = vms.find(threadId);
    if (it != vms.end() && it->second != nullptr) {
        return true;
    }

    Utils::threadSafePrint("Creating VM for thread " + std::to_string(threadId), true);
    
    randomx_vm* vm = randomx_create_vm(
        static_cast<randomx_flags>(flags), 
        cache, 
        useLightMode ? nullptr : dataset
    );
    
    if (!vm) {
        Utils::threadSafePrint("VM creation failed, trying fallback...", true);
        int fallbackFlags = cacheAllocFlags & ~RANDOMX_FLAG_FULL_MEM;
        vm = randomx_create_vm(static_cast<randomx_flags>(fallbackFlags), cache, nullptr);
        if (!vm) {
            Utils::threadSafePrint("VM creation failed completely", true);
            return false;
        }
    }

    vms[threadId] = vm;
    Utils::threadSafePrint("VM created successfully for thread " + std::to_string(threadId), true);
    return true;
}

bool RandomXManager::initializeVM(int threadId) {
    if (!initialized) return false;
    return createVM(threadId);
}

randomx_vm* RandomXManager::getVM(int threadId) {
    std::shared_lock<std::shared_mutex> lock(vmMutex);
    auto it = vms.find(threadId);
    return (it != vms.end()) ? it->second : nullptr;
}

bool RandomXManager::loadDataset(const std::string& filename) {
    unsigned long itemCount = randomx_dataset_item_count();
    size_t actualDatasetSize = static_cast<size_t>(itemCount) * RANDOMX_DATASET_ITEM_SIZE;
    
    if (!dataset) {
        dataset = randomx_alloc_dataset(static_cast<randomx_flags>(flags));
        if (!dataset) return false;
    }

    std::ifstream file(filename, std::ios::binary);
    if (!file.is_open()) return false;

    void* datasetMemory = randomx_get_dataset_memory(dataset);
    if (!datasetMemory) { file.close(); return false; }

    file.read(reinterpret_cast<char*>(datasetMemory), actualDatasetSize);
    file.close();
    return true;
}

bool RandomXManager::saveDataset(const std::string& filename) {
    if (!dataset) return false;

    unsigned long itemCount = randomx_dataset_item_count();
    size_t actualDatasetSize = static_cast<size_t>(itemCount) * RANDOMX_DATASET_ITEM_SIZE;

    std::ofstream file(filename, std::ios::binary);
    if (!file.is_open()) return false;

    void* datasetMemory = randomx_get_dataset_memory(dataset);
    if (!datasetMemory) { file.close(); return false; }

    file.write(reinterpret_cast<const char*>(datasetMemory), actualDatasetSize);
    file.close();
    return true;
}

std::string RandomXManager::getDatasetPath(const std::string& seedHash) {
    return "randomx_dataset_" + seedHash + ".bin";
}

void RandomXManager::cleanupVM(int threadId) {
    std::unique_lock<std::shared_mutex> lock(vmMutex);
    auto it = vms.find(threadId);
    if (it != vms.end() && it->second) {
        randomx_destroy_vm(it->second);
        vms.erase(it);
    }
}

void RandomXManager::destroyVM(randomx_vm* vm) {
    if (!vm) return;
    std::unique_lock<std::shared_mutex> lock(vmMutex);
    for (auto it = vms.begin(); it != vms.end(); ++it) {
        if (it->second == vm) {
            randomx_destroy_vm(vm);
            vms.erase(it);
            break;
        }
    }
}

void RandomXManager::cleanup() {
    std::lock_guard<std::mutex> lock(initMutex);
    {
        std::unique_lock<std::shared_mutex> vmLock(vmMutex);
        for (auto& [threadId, vm] : vms) {
            if (vm) randomx_destroy_vm(vm);
        }
        vms.clear();
    }
    if (cache) { randomx_release_cache(cache); cache = nullptr; }
    if (dataset) { randomx_release_dataset(dataset); dataset = nullptr; }
    initialized = false;
    currentSeedHash.clear();
}

bool RandomXManager::setTargetAndDifficulty(const std::string& targetHex) {
    // Parse compact target from pool
    uint32_t compactTarget = std::stoul(targetHex, nullptr, 16);
    
    // Extract mantissa from compact format
    uint32_t mantissa = compactTarget & 0x00FFFFFF;
    
    // Calculate difficulty: 0xFFFFFFFF / mantissa
    uint64_t difficulty = 0xFFFFFFFFULL / static_cast<uint64_t>(mantissa);
    currentDifficulty = static_cast<double>(difficulty);
    
    // CORRECT: Build 256-bit target = (2^256 - 1) / difficulty
    // This is done by setting all bits initially, then dividing
    
    // For big-endian comparison, we need:
    // MSW (word[3]) gets the quotient
    // Lower words get 0xFFFFFFFFFFFFFFFF (remainder portion)
    
    // Clear target
    expandedTarget.words[0] = 0;
    expandedTarget.words[1] = 0; 
    expandedTarget.words[2] = 0;
    expandedTarget.words[3] = 0;
    
    if (difficulty <= 1) {
        // Maximum target (all bits set)
        expandedTarget.words[0] = 0xFFFFFFFFFFFFFFFFULL;
        expandedTarget.words[1] = 0xFFFFFFFFFFFFFFFFULL;
        expandedTarget.words[2] = 0xFFFFFFFFFFFFFFFFULL;
        expandedTarget.words[3] = 0xFFFFFFFFFFFFFFFFULL;
    } else {
        // Divide 2^256 by difficulty
        // Approximation: Only the MSW matters for our difficulty range
        // word[3] = 2^64 / difficulty (approximately, for the most significant part)
        // But we need ALL 256 bits!
        
        // Better approach: Fill lower words with 0xFF, calculate MSW properly
        expandedTarget.words[0] = 0xFFFFFFFFFFFFFFFFULL;
        expandedTarget.words[1] = 0xFFFFFFFFFFFFFFFFULL;
        expandedTarget.words[2] = 0xFFFFFFFFFFFFFFFFULL;
        
        // For the MSW: we need (2^256 / difficulty) >> 192
        // Which is approximately: (2^64 / difficulty) * (2^192 / 2^256)
        // Simplified: 0xFFFFFFFFFFFFFFFF / difficulty gives us the right ballpark
        expandedTarget.words[3] = 0xFFFFFFFFFFFFFFFFULL / difficulty;
    }
    
    if (config.debugMode) {
        Utils::threadSafePrint("\n=== MONERO TARGET (FULL PRECISION) ===", true);
        Utils::threadSafePrint("Difficulty: " + std::to_string(difficulty), true);
        Utils::threadSafePrint("Target MSW: 0x" + Utils::formatHex(expandedTarget.words[3], 16), true);
        Utils::threadSafePrint("Target words[2]: 0x" + Utils::formatHex(expandedTarget.words[2], 16), true);
        Utils::threadSafePrint("Expected: ~" + std::to_string(difficulty) + " hashes per share\n", true);
    }
    
    return true;
}

bool RandomXManager::checkTarget(const uint8_t* hash) {
    if (!hash) return false;
    
    // Build big-endian representation of hash
    uint256_t hashValue;
    hashValue.clear();
    
    for (int wordIdx = 0; wordIdx < 4; wordIdx++) {
        uint64_t word = 0;
        int baseByteIdx = (3 - wordIdx) * 8;
        
        for (int byteInWord = 0; byteInWord < 8; byteInWord++) {
            word = (word << 8) | static_cast<uint64_t>(hash[baseByteIdx + byteInWord]);
        }
        
        hashValue.words[wordIdx] = word;
    }
    
    // Compare hash < target
    bool valid = false;
    {
        std::lock_guard<std::mutex> lock(hashMutex);
        
        for (int i = 3; i >= 0; i--) {
            if (hashValue.words[i] < expandedTarget.words[i]) {
                valid = true;
                break;
            } else if (hashValue.words[i] > expandedTarget.words[i]) {
                valid = false;
                break;
            }
        }
    }
    
    // Debug output every 10000 hashes or on valid share
    static std::atomic<uint64_t> hashCount{0};
    uint64_t count = hashCount.fetch_add(1);
    
    if (valid || (config.debugMode && count % 10000 == 0)) {
        std::stringstream ss;
        
        if (valid) {
            ss << "\n=== VALID SHARE FOUND ===\n";
        } else {
            ss << "\n[Debug: Hash check at " << count << "]\n";
        }
        
        ss << "Hash (BE, full 256-bit):\n";
        for (int i = 3; i >= 0; i--) {
            ss << "  Word[" << i << "]: 0x" << std::hex << std::setw(16) << std::setfill('0') << hashValue.words[i] << "\n";
        }
        
        ss << "Target (BE, full 256-bit):\n";
        for (int i = 3; i >= 0; i--) {
            ss << "  Word[" << i << "]: 0x" << std::hex << std::setw(16) << std::setfill('0') << expandedTarget.words[i] << "\n";
        }
        
        ss << "\nComparison:\n";
        for (int i = 3; i >= 0; i--) {
            ss << "  Hash[" << i << "] ";
            if (hashValue.words[i] < expandedTarget.words[i]) {
                ss << "<";
            } else if (hashValue.words[i] > expandedTarget.words[i]) {
                ss << ">";
            } else {
                ss << "==";
            }
            ss << " Target[" << i << "]\n";
            
            if (hashValue.words[i] != expandedTarget.words[i]) {
                break;
            }
        }
        
        ss << "Result: " << (valid ? "VALID" : "INVALID") << "\n";
        
        Utils::threadSafePrint(ss.str(), true);
    }
    
    if (valid) {
        std::lock_guard<std::mutex> hashLock(hashMutex);
        lastHash.assign(hash, hash + RANDOMX_HASH_SIZE);
    }
    
    return valid;
}

std::vector<uint8_t> RandomXManager::getLastHash() {
    std::lock_guard<std::mutex> lock(hashMutex);
    return lastHash;
}

std::string RandomXManager::getLastHashHex() {
    std::lock_guard<std::mutex> lock(hashMutex);
    return Utils::bytesToHex(lastHash);
}

void RandomXManager::handleSeedHashChange(const std::string& newSeedHash) {
    std::lock_guard<std::mutex> lock(seedHashMutex);
    if (newSeedHash != currentSeedHash) {
        {
            std::unique_lock<std::shared_mutex> vmLock(vmMutex);
            for (auto& [threadId, vm] : vms) {
                if (vm) randomx_destroy_vm(vm);
            }
            vms.clear();
        }
        initialize(newSeedHash);
    }
}

bool RandomXManager::calculateHashForThread(int threadId, const std::vector<uint8_t>& input, uint64_t nonce) {
    std::shared_lock<std::shared_mutex> vmLock(vmMutex);
    
    auto it = vms.find(threadId);
    if (it == vms.end() || !it->second) return false;
    
    randomx_vm* vm = it->second;
    if (!initialized || input.empty() || input.size() > MAX_BLOB_SIZE) return false;
    if (!useLightMode && !dataset) return false;
    
    alignas(16) uint8_t blob[MAX_BLOB_SIZE];
    std::memset(blob, 0, sizeof(blob));
    std::memcpy(blob, input.data(), input.size());
    
    // Insert nonce at bytes 39-42 (little-endian)
    if (input.size() >= 43) {
        uint32_t nonce32 = static_cast<uint32_t>(nonce);
        blob[39] = static_cast<uint8_t>(nonce32 & 0xFF);
        blob[40] = static_cast<uint8_t>((nonce32 >> 8) & 0xFF);
        blob[41] = static_cast<uint8_t>((nonce32 >> 16) & 0xFF);
        blob[42] = static_cast<uint8_t>((nonce32 >> 24) & 0xFF);
    }
    
    alignas(16) uint8_t hash[RANDOMX_HASH_SIZE];
    std::memset(hash, 0, sizeof(hash));
    
    // Calculate hash
    randomx_calculate_hash(vm, blob, input.size(), hash);
    
    // Check target
    bool valid = checkTarget(hash);
    
    if (valid) {
        std::lock_guard<std::mutex> hashLock(hashMutex);
        // Store hash as-is from RandomX (already in correct byte order)
        lastHash.assign(hash, hash + RANDOMX_HASH_SIZE);
        
        if (config.debugMode) {
            // Debug: print first 8 bytes and last 8 bytes
            std::stringstream ss;
            ss << "Hash bytes (first 8): ";
            for (int i = 0; i < 8; i++) {
                ss << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(hash[i]);
            }
            ss << "\nHash bytes (last 8): ";
            for (int i = 24; i < 32; i++) {
                ss << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(hash[i]);
            }
            Utils::threadSafePrint(ss.str(), true);
        }
    }
    
    return valid;
}