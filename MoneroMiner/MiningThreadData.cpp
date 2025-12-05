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
        
        // CORRECT METHOD: Both hash and target are stored as little-endian 256-bit integers
        // But we need to compare them as if reading the hex string left-to-right
        // 
        // The hash "69cc5d11daad457e..." as bytes is: 69 cc 5d 11 da ad 45 7e ...
        // The target "a657f6d7f5220000..." as bytes is: a6 57 f6 d7 f5 22 00 00 ...
        //
        // We compare byte-by-byte from START (index 0) to END (index 31)
        // This is comparing from MOST significant to LEAST significant
        
        // Compare as little-endian 64-bit integers (first 8 bytes matter most)
        // Build uint64 from first 8 bytes and compare
        uint64_t hash64 = 0, target64 = 0;
        for (int i = 0; i < 8; i++) {
            hash64 |= (static_cast<uint64_t>(hashOut[i]) << (i * 8));
            target64 |= (static_cast<uint64_t>(targetBytes[i]) << (i * 8));
        }
        
        if (hash64 < target64) {
            return true;  // Hash meets difficulty
        } else if (hash64 > target64) {
            return false;
        }
        
        // First 8 bytes equal - check remaining bytes
        for (int i = 8; i < 32; i++) {
            if (hashOut[i] < targetBytes[i]) {
                return true;
            }
            if (hashOut[i] > targetBytes[i]) {
                return false;
            }
        }
        
        return true; // All bytes equal
    }
    catch (...) {
        return false;
    }
}