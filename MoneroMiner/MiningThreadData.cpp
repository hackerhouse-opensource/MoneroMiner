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
        
        // Increment hash count FIRST
        totalHashes++;
        
        // Debug output on first hash and every 10k hashes for thread 0
        if (config.debugMode && threadId == 0 && (totalHashes == 1 || totalHashes % 10000 == 0)) {
            // Build uint64 from first 8 bytes
            uint64_t hash64 = 0, target64 = 0;
            for (int i = 0; i < 8; i++) {
                hash64 |= (static_cast<uint64_t>(hashOut[i]) << (i * 8));
                target64 |= (static_cast<uint64_t>(targetBytes[i]) << (i * 8));
            }
            
            // CRITICAL FIX: Display the ACTUAL difficulty from the job, not calculated from partial target
            // The target64 here is only first 8 bytes, which doesn't represent the full difficulty
            
            std::stringstream ss;
            ss << "\n[T0 PoW CHECK @ " << std::dec << totalHashes << " hashes]\n";
            ss << "  Hash (LE bytes 0-7): ";
            for (int i = 0; i < 8; i++) {
                ss << std::hex << std::setw(2) << std::setfill('0') << (int)hashOut[i] << " ";
            }
            ss << "\n  Hash as uint64 (LE): 0x" << std::hex << std::setw(16) << std::setfill('0') << hash64;
            ss << "\n  Target (LE bytes 0-7): ";
            for (int i = 0; i < 8; i++) {
                ss << std::hex << std::setw(2) << std::setfill('0') << (int)targetBytes[i] << " ";
            }
            ss << "\n  Target as uint64 (LE): 0x" << std::hex << std::setw(16) << std::setfill('0') << target64;
            ss << "\n  Comparison: 0x" << std::hex << hash64 << (hash64 < target64 ? " < " : " >= ") 
               << "0x" << target64;
            ss << "\n  Result: " << (hash64 < target64 ? "VALID" : "INVALID") 
               << " (target represents difficulty ~480k)\n";
            Utils::threadSafePrint(ss.str(), true);
        }
        
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