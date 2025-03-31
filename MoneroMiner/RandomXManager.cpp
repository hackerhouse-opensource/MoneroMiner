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
uint64_t RandomXManager::currentHeight = 0;
std::string RandomXManager::currentJobId;

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

bool RandomXManager::calculateHash(randomx_vm* vm, const std::vector<uint8_t>& input, uint64_t nonce) {
    if (!vm || input.empty()) return false;

    // Ensure input is at least 43 bytes
    std::vector<uint8_t> inputData = input;
    if (inputData.size() < 43) {
        inputData.resize(43);
    }

    // Insert nonce at bytes 39-42 (big-endian)
    inputData[39] = (nonce >> 24) & 0xFF;
    inputData[40] = (nonce >> 16) & 0xFF;
    inputData[41] = (nonce >> 8) & 0xFF;
    inputData[42] = nonce & 0xFF;

    // Calculate hash
    uint8_t output[32];
    randomx_calculate_hash(vm, inputData.data(), inputData.size(), output);

    // Store hash in thread-safe manner
    {
        std::lock_guard<std::mutex> lock(hashMutex);
        lastHash.assign(output, output + 32);
        lastHashHex = bytesToHex(std::vector<uint8_t>(output, output + 32));
    }

    // Debug output every 10,000 hashes or first hash
    static uint64_t hashCount = 0;
    if (debugMode && (++hashCount % 10000 == 0 || hashCount == 1)) {
        std::stringstream ss;
        ss << "\nRandomX Hash Calculation:" << std::endl;
        ss << "  Input data: " << bytesToHex(inputData) << std::endl;
        ss << "  Nonce: 0x" << std::hex << std::setw(8) << std::setfill('0') << nonce << std::endl;
        ss << "  Hash output: " << lastHashHex << std::endl;
        ss << "  Target: 0x" << currentTargetHex << std::endl;
        
        // Expand target for comparison
        std::string target = currentTargetHex;
        if (target.substr(0, 2) == "0x") target = target.substr(2);
        uint32_t compact = std::stoul(target, nullptr, 16);
        uint8_t exponent = (compact >> 24) & 0xFF;
        uint32_t mantissa = compact & 0x00FFFFFF;
        
        ss << "\nTarget Expansion:" << std::endl;
        ss << "  Compact target: 0x" << target << std::endl;
        ss << "  Exponent: 0x" << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(exponent) << std::endl;
        ss << "  Mantissa: 0x" << std::hex << std::setw(6) << std::setfill('0') << mantissa << std::endl;
        
        // Create 256-bit target
        uint256_t full_target;
        full_target.words[0] = static_cast<uint64_t>(mantissa);
        full_target.shift_left(232);  // Place mantissa at start of Word 0
        
        ss << "\nExpanded Target (256-bit):" << std::endl;
        ss << "  Word 0: 0x" << std::hex << std::setw(16) << std::setfill('0') << full_target.words[0] << std::endl;
        ss << "  Word 1: 0x" << std::hex << std::setw(16) << std::setfill('0') << full_target.words[1] << std::endl;
        ss << "  Word 2: 0x" << std::hex << std::setw(16) << std::setfill('0') << full_target.words[2] << std::endl;
        ss << "  Word 3: 0x" << std::hex << std::setw(16) << std::setfill('0') << full_target.words[3] << std::endl;
        
        // Convert hash to 256-bit integer for comparison
        uint256_t hash_value;
        for (int i = 0; i < 32; i++) {
            int word_idx = i / 8;
            int byte_idx = 7 - (i % 8);
            hash_value.words[word_idx] |= (static_cast<uint64_t>(output[i]) << (byte_idx * 8));
        }
        
        ss << "\nHash Value (256-bit):" << std::endl;
        ss << "  Word 0: 0x" << std::hex << std::setw(16) << std::setfill('0') << hash_value.words[0] << std::endl;
        ss << "  Word 1: 0x" << std::hex << std::setw(16) << std::setfill('0') << hash_value.words[1] << std::endl;
        ss << "  Word 2: 0x" << std::hex << std::setw(16) << std::setfill('0') << hash_value.words[2] << std::endl;
        ss << "  Word 3: 0x" << std::hex << std::setw(16) << std::setfill('0') << hash_value.words[3] << std::endl;
        
        ss << "\nShare Validation:" << std::endl;
        bool meetsTarget = hash_value < full_target;
        ss << "  Hash " << (meetsTarget ? "meets" : "does not meet") << " target" << std::endl;
        
        threadSafePrint(ss.str(), true);
    }

    return checkHash(output, currentTargetHex);
}

std::string RandomXManager::getLastHashHex() {
    std::lock_guard<std::mutex> lock(hashMutex);
    return bytesToHex(lastHash);
}

bool RandomXManager::checkHash(const uint8_t* hash, const std::string& targetHex) {
    // Extract exponent and mantissa from target
    uint32_t target = std::stoul(targetHex, nullptr, 16);
    uint8_t exponent = (target >> 24) & 0xFF;
    uint32_t mantissa = target & 0x00FFFFFF;

    // Create 256-bit target
    uint256_t targetValue;
    targetValue.words[0] = static_cast<uint64_t>(mantissa) << 40;  // Shift left by 40 bits to place at start of Word 0
    targetValue.words[1] = 0;
    targetValue.words[2] = 0;
    targetValue.words[3] = 0;

    // Create 256-bit hash value
    uint256_t hashValue;
    memcpy(hashValue.words, hash, 32);

    // Only show debug output for first hash and every 10,000th hash
    static uint64_t hashCount = 0;
    hashCount++;
    bool shouldShowDebug = hashCount == 1 || hashCount % 10000 == 0;

    if (shouldShowDebug) {
        std::stringstream ss;
        ss << "\nTarget Expansion:" << std::endl;
        ss << "  Compact target: 0x" << targetHex << std::endl;
        ss << "  Exponent: 0x" << std::hex << static_cast<int>(exponent) << std::endl;
        ss << "  Mantissa: 0x" << std::hex << mantissa << std::endl;
        ss << std::endl;
        ss << "Expanded Target (256-bit):" << std::endl;
        ss << "  Word 0: 0x" << std::hex << std::setw(16) << std::setfill('0') << targetValue.words[0] << std::endl;
        ss << "  Word 1: 0x" << std::hex << std::setw(16) << std::setfill('0') << targetValue.words[1] << std::endl;
        ss << "  Word 2: 0x" << std::hex << std::setw(16) << std::setfill('0') << targetValue.words[2] << std::endl;
        ss << "  Word 3: 0x" << std::hex << std::setw(16) << std::setfill('0') << targetValue.words[3] << std::endl;
        ss << std::endl;
        ss << "Hash Value (256-bit):" << std::endl;
        ss << "  Word 0: 0x" << std::hex << std::setw(16) << std::setfill('0') << hashValue.words[0] << std::endl;
        ss << "  Word 1: 0x" << std::hex << std::setw(16) << std::setfill('0') << hashValue.words[1] << std::endl;
        ss << "  Word 2: 0x" << std::hex << std::setw(16) << std::setfill('0') << hashValue.words[2] << std::endl;
        ss << "  Word 3: 0x" << std::hex << std::setw(16) << std::setfill('0') << hashValue.words[3] << std::endl;
        ss << std::endl;
        ss << "Share Validation:" << std::endl;
        ss << "  Hash " << (hashValue < targetValue ? "meets" : "does not meet") << " target" << std::endl;
        threadSafePrint(ss.str(), true);
    }

    return hashValue < targetValue;
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

void RandomXManager::setTarget(const std::string& targetHex) {
    std::lock_guard<std::mutex> lock(hashMutex);
    currentTargetHex = targetHex;
} 