#include "RandomXManager.h"
#include "Config.h"
#include "Utils.h"  // ADD THIS - was missing!
#include "Globals.h"  // ADD THIS
#include <fstream>
#include <sstream>
#include <iomanip>

static constexpr size_t MAX_BLOB_SIZE = 128;

// Static member initialization
int RandomXManager::flags = RANDOMX_FLAG_DEFAULT;
static int cacheAllocFlags = RANDOMX_FLAG_DEFAULT;

std::shared_mutex RandomXManager::vmMutex;
std::mutex RandomXManager::initMutex;
std::mutex RandomXManager::hashMutex;
std::mutex RandomXManager::cacheMutex;
std::mutex RandomXManager::seedHashMutex;
std::mutex RandomXManager::targetMutex;
std::unordered_map<int, randomx_vm*> RandomXManager::vms;
randomx_cache* RandomXManager::cache = nullptr;
randomx_dataset* RandomXManager::dataset = nullptr;
std::string RandomXManager::currentSeedHash;
bool RandomXManager::initialized = false;
bool RandomXManager::useLightMode = false;
std::vector<uint8_t> RandomXManager::lastHash;
double RandomXManager::currentDifficulty = 0.0;
uint256_t RandomXManager::expandedTarget;

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
    
    // FIX: std::chrono::high_resolution_clock
    auto start = std::chrono::high_resolution_clock::now();
    
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
    
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
    double seconds = duration.count() / 1000.0;
    
    Utils::threadSafePrint("Dataset initialized in " + std::to_string(seconds) + " seconds", true);
    
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
        
        // FIX: std::filesystem functions
        if (std::filesystem::exists(datasetFileName)) {
            size_t fileSize = std::filesystem::file_size(datasetFileName);
            
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
    
    if (!config.debugMode) {
        Utils::threadSafePrint("RandomX ready", true);
    } else {
        Utils::threadSafePrint("=== RANDOMX READY ===", true);
        Utils::threadSafePrint("Flags: 0x" + Utils::formatHex(static_cast<uint64_t>(flags), 8), true);
    }
    
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

    // Only log in debug mode
    if (config.debugMode) {
        Utils::threadSafePrint("Creating VM for thread " + std::to_string(threadId), true);
    }
    
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
    if (config.debugMode) {
        Utils::threadSafePrint("VM created successfully for thread " + std::to_string(threadId), true);
    }
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
    if (targetHex.length() != 8) {
        return false;
    }
    
    try {
        // Parse pool's target value
        uint32_t poolTarget = static_cast<uint32_t>(std::stoul(targetHex, nullptr, 16));
        
        // Monero difficulty calculation:
        // difficulty = max_target / current_target
        // where max_target = 0xFFFFFFFFFFFFFFFF for the 64-bit comparison
        
        // For Nanopool's 0xf3220000:
        // difficulty = 0xFFFFFFFFFFFFFFFF / 0xf3220000 = ~4.4 million (not 4 billion!)
        
        // The pool sends the HIGH 32 bits of a 64-bit difficulty representation
        // So we need: difficulty = 0xFFFFFFFF / (poolTarget >> 24)
        
        // Actually, the pool target IS the difficulty encoded
        // Nanopool sends: 0xf3220000 which means difficulty = 0xFFFFFFFFFFFFFFFF / 0x00000000f3220000
        
        uint64_t targetDiff = static_cast<uint64_t>(poolTarget);
        currentDifficulty = 0xFFFFFFFFFFFFFFFFULL / targetDiff;
        
        if (config.debugMode) {
            std::stringstream ss;
            ss << "=== TARGET SET ===" << std::endl;
            ss << "Raw target: 0x" << std::hex << poolTarget << std::endl;
            ss << "Difficulty: " << std::dec << currentDifficulty << std::endl;
            ss << "Target threshold: 0x" << std::hex << std::setw(16) << std::setfill('0') 
               << (0xFFFFFFFFFFFFFFFFULL / currentDifficulty);
            Utils::threadSafePrint(ss.str(), true);
        }
        
        return true;
    }
    catch (const std::exception& e) {
        Utils::threadSafePrint("Error parsing target: " + std::string(e.what()), true);
        return false;
    }
}

bool RandomXManager::checkTarget(const uint8_t* hash) {
    if (!hash) return false;
    
    uint256_t hashValue;
    hashValue = uint256_t();
    
    // Read hash as little-endian 256-bit integer
    for (int wordIdx = 0; wordIdx < 4; wordIdx++) {
        uint64_t word = 0;
        int baseByteIdx = wordIdx * 8;
        
        for (int byteInWord = 0; byteInWord < 8; byteInWord++) {
            word |= static_cast<uint64_t>(hash[baseByteIdx + byteInWord]) << (byteInWord * 8);
        }
        
        hashValue.data[wordIdx] = word;
    }
    
    // First check higher order words - they must all be zero for a valid share
    if (hashValue.data[3] > 0 || hashValue.data[2] > 0 || hashValue.data[1] > 0) {
        return false;
    }
    
    // Now compare the least significant word
    bool valid = hashValue.data[0] < expandedTarget.data[0];
    
    if (valid) {
        std::lock_guard<std::mutex> lock(hashMutex);
        lastHash.assign(hash, hash + RANDOMX_HASH_SIZE);
        
        Utils::threadSafePrint("\n*** VALID SHARE FOUND ***", true);
        Utils::threadSafePrint("Hash LSW:   0x" + Utils::formatHex(hashValue.data[0], 16), true);
        Utils::threadSafePrint("Target LSW: 0x" + Utils::formatHex(expandedTarget.data[0], 16), true);
        Utils::threadSafePrint("Full hash:  " + Utils::bytesToHex(hash, 32), true);
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
    randomx_vm* vm = nullptr;
    {
        std::shared_lock<std::shared_mutex> vmLock(vmMutex);
        auto it = vms.find(threadId);
        if (it == vms.end() || !it->second) return false;
        vm = it->second;
    }
    
    if (!initialized || input.empty() || input.size() > MAX_BLOB_SIZE) return false;
    
    alignas(64) uint8_t blob[MAX_BLOB_SIZE];
    alignas(64) uint8_t hash[RANDOMX_HASH_SIZE];
    
    // CRITICAL FIX: DON'T insert nonce - it's already in the input blob!
    // The calling code (MoneroMiner.cpp) already wrote the nonce to the blob
    std::memcpy(blob, input.data(), input.size());
    
    // Calculate hash directly
    randomx_calculate_hash(vm, blob, input.size(), hash);
    
    // Debug logging (only every 10000th hash)
    static std::atomic<uint64_t> hashCounter{0};
    uint64_t count = hashCounter.fetch_add(1);
    
    if (config.debugMode && (count % 10000 == 0)) {
        std::stringstream ss;
        ss << "\n[RandomX] Hash #" << count;
        ss << "\n  Input blob (first 50 bytes): ";
        for (size_t i = 0; i < 50 && i < input.size(); i++) {
            ss << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(input[i]) << " ";
        }
        ss << "\n  Hash LSW: 0x" << std::hex << std::setw(16) << std::setfill('0');
        uint64_t hashLSW = 0;
        for (int i = 0; i < 8; i++) {
            hashLSW |= static_cast<uint64_t>(hash[i]) << (i * 8);
        }
        ss << hashLSW;
        ss << " | Target LSW: 0x" << std::hex << std::setw(16) << std::setfill('0') << expandedTarget.data[0];
        Utils::threadSafePrint(ss.str(), true);
    }
    
    bool wouldBeValid = checkTarget(hash);
    
    if (wouldBeValid) {
        Utils::threadSafePrint("\n!!! VALID SHARE DETECTED !!!", true);
    }
    
    return wouldBeValid;
}

std::string RandomXManager::getTargetHex() {
    std::lock_guard<std::mutex> lock(targetMutex);
    
    std::stringstream ss;
    ss << std::hex << std::setfill('0');
    
    for (int i = 3; i >= 0; i--) {
        ss << std::setw(16) << expandedTarget.data[i];  // FIX
    }
    
    return ss.str();
}

double RandomXManager::getDifficulty() {  // KEEP AS double
    std::lock_guard<std::mutex> lock(targetMutex);
    
    // FIX: Use .data[] instead of .words[]
    if (expandedTarget.data[3] == 0 && expandedTarget.data[2] == 0 && expandedTarget.data[1] == 0) {
        if (expandedTarget.data[0] == 0) return 1.0;
        return static_cast<double>(0xFFFFFFFFFFFFFFFFULL) / static_cast<double>(expandedTarget.data[0]);
    }
    
    // Approximate for higher difficulties
    return static_cast<double>(0xFFFFFFFFFFFFFFFFULL) / (static_cast<double>(expandedTarget.data[0]) + 1.0);
}

double RandomXManager::getTargetThreshold() {
    std::lock_guard<std::mutex> lock(targetMutex);
    
    double result = static_cast<double>(expandedTarget.data[0]);  // FIX
    result += static_cast<double>(expandedTarget.data[1]) * 18446744073709551616.0;  // FIX
    
    return result;
}

randomx_dataset* RandomXManager::getDataset() {
    return dataset;
}

randomx_cache* RandomXManager::getCache() {
    return cache;
}

randomx_flags RandomXManager::getVMFlags() {
    return static_cast<randomx_flags>(flags);  // FIX: changed from vmFlags to flags
}