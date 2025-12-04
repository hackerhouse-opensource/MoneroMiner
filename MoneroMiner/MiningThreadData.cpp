#include "MiningThreadData.h"
#include "RandomXManager.h"
#include "Utils.h"
#include "Config.h"
#include "Globals.h"
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
    const std::vector<uint8_t>& target,
    std::vector<uint8_t>& hashOut)
{
    if (!vm || blob.empty() || target.size() != 32) {
        return false;
    }

    hashOut.resize(RANDOMX_HASH_SIZE);
    
    // Calculate RandomX hash (outputs little-endian)
    randomx_calculate_hash(vm, blob.data(), blob.size(), hashOut.data());
    
    // CRITICAL: Direct uint64_t comparison - NO byte swapping!
    // Both hash and target are already in little-endian byte order
    uint64_t hashLSW = *reinterpret_cast<const uint64_t*>(hashOut.data());
    uint64_t targetLSW = *reinterpret_cast<const uint64_t*>(target.data());
    
    // Valid if hash < target
    return hashLSW < targetLSW;
}