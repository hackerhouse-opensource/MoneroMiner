#include "MiningThreadData.h"
#include "RandomXManager.h"
#include "Utils.h"
#include "Config.h"
#include "Globals.h"
#include "Types.h"
#include <array>
#include <sstream>
#include <iomanip>

MiningThreadData::MiningThreadData(int id) : threadId(id) {
}

MiningThreadData::~MiningThreadData() {
    RandomXManager::cleanupVM(threadId);
}

bool MiningThreadData::initializeVM() {
    if (vm != nullptr) {
        return true; // Already initialized
    }

    // Get the global dataset/cache from RandomXManager
    randomx_dataset* dataset = RandomXManager::getDataset();
    randomx_cache* cache = RandomXManager::getCache();
    
    if (!dataset && !cache) {
        return false; // Neither dataset nor cache available
    }

    // Get flags from RandomXManager
    randomx_flags flags = RandomXManager::getVMFlags();
    
    // Create VM with dataset (preferred) or cache
    if (dataset) {
        vm = randomx_create_vm(flags, cache, dataset);
    } else {
        vm = randomx_create_vm(flags, cache, nullptr);
    }
    
    return vm != nullptr;
}

bool MiningThreadData::calculateHash(const std::vector<uint8_t>& input, uint64_t nonce) {
    bool result = RandomXManager::calculateHashForThread(threadId, input, nonce);
    incrementHashCount();
    return result;
}

bool MiningThreadData::calculateHashAndCheckTarget(
    const std::vector<uint8_t>& blob,
    const std::vector<uint8_t>& targetBytes,
    std::vector<uint8_t>& hashOut)
{
    if (!vm || blob.empty()) {
        return false;
    }

    try {
        // Calculate RandomX hash
        randomx_calculate_hash(vm, blob.data(), blob.size(), hashOut.data());
        
        totalHashes++;
        
        // Convert hash and target to uint256_t using the constructor
        uint256_t hashValue(hashOut.data());
        uint256_t targetValue(targetBytes.data());
        
        // Use the built-in comparison operator
        bool isValid = hashValue < targetValue;
        
        // Debug output
        if (config.debugMode && (isValid || (totalHashes % 10000 == 0))) {
            std::stringstream ss;
            ss << "\n[T" << threadId << " PoW @ " << totalHashes << " hashes]\n";
            ss << "  Hash:   " << hashValue.toHex() << "\n";
            ss << "  Target: " << targetValue.toHex() << "\n";
            ss << "  Result: " << (isValid ? "VALID SHARE FOUND!" : "does not meet target");
            
            if (isValid) {
                ss << "\n  >>> SUBMITTING SHARE <<<";
            }
            
            Utils::threadSafePrint(ss.str(), true);
        }
        
        return isValid;
        
    }
    catch (...) {
        return false;
    }
}