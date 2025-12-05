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
    const std::vector<uint8_t>& targetBytes,
    std::vector<uint8_t>& hashOut)
{
    if (!vm || blob.empty()) {
        return false;
    }

    try {
        randomx_calculate_hash(vm, blob.data(), blob.size(), hashOut.data());
        
        // CRITICAL FIX: Compare as BIG-ENDIAN 256-bit integers!
        // Even though the bytes are stored little-endian, we compare from
        // the MOST significant byte [31] down to LEAST significant [0]
        // This matches how pools validate shares
        for (int i = 31; i >= 0; i--) {
            if (hashOut[i] > targetBytes[i]) {
                return false; // Hash is greater than target - INVALID
            }
            if (hashOut[i] < targetBytes[i]) {
                return true;  // Hash is less than target - VALID
            }
            // If equal, continue to next byte
        }
        
        return true; // All bytes equal - hash exactly equals target (valid)
    }
    catch (...) {
        return false;
    }
}