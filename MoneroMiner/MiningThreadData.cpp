#include "MiningThreadData.h"
#include "RandomXManager.h"
#include "Utils.h"
#include "Config.h"
#include "Globals.h"
#include "Types.h"
#include <array>  // ADD THIS - required for std::array
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
        
        // Convert hash to 256-bit integer (little-endian)
        std::array<uint64_t, 4> hashValue = {0, 0, 0, 0};
        for (int wordIdx = 0; wordIdx < 4; wordIdx++) {
            uint64_t word = 0;
            int baseByteIdx = wordIdx * 8;
            for (int byteInWord = 0; byteInWord < 8; byteInWord++) {
                word |= static_cast<uint64_t>(hashOut[baseByteIdx + byteInWord]) << (byteInWord * 8);
            }
            hashValue[wordIdx] = word;
        }
        
        // Convert target bytes to 256-bit integer (little-endian)
        std::array<uint64_t, 4> targetValue = {0, 0, 0, 0};
        for (int wordIdx = 0; wordIdx < 4; wordIdx++) {
            uint64_t word = 0;
            int baseByteIdx = wordIdx * 8;
            for (int byteInWord = 0; byteInWord < 8; byteInWord++) {
                word |= static_cast<uint64_t>(targetBytes[baseByteIdx + byteInWord]) << (byteInWord * 8);
            }
            targetValue[wordIdx] = word;
        }
        
        // CORRECTED: Compare 256-bit values from MSW to LSW
        // Valid share: hash < target (strictly less than)
        bool isValid = false;
        for (int i = 3; i >= 0; i--) {
            if (hashValue[i] < targetValue[i]) {
                // Hash word is less than target word - definitely valid
                isValid = true;
                break;
            }
            if (hashValue[i] > targetValue[i]) {
                // Hash word is greater than target word - definitely invalid
                isValid = false;
                break;
            }
            // If equal, continue to next word (lower significance)
        }
        // If all words are equal, hash == target, which is NOT valid (we need hash < target)
        
        // Debug output for valid shares or periodic checks
        if (config.debugMode && (isValid || (totalHashes % 10000 == 0))) {
            std::stringstream ss;
            ss << "\n[T" << threadId << " PoW CHECK @ " << totalHashes << " hashes]\n";
            
            // Show as little-endian 64-bit words
            ss << "  Hash (LE):   0x";
            for (int i = 0; i < 4; i++) {
                ss << std::hex << std::setw(16) << std::setfill('0') << hashValue[i];
            }
            
            ss << "\n  Target (LE): 0x";
            for (int i = 0; i < 4; i++) {
                ss << std::hex << std::setw(16) << std::setfill('0') << targetValue[i];
            }
            
            ss << "\n  Comparison (MSW to LSW):";
            bool decided = false;
            for (int i = 3; i >= 0; i--) {
                ss << "\n    Word[" << i << "]: Hash=0x" << std::hex << std::setw(16) << std::setfill('0') << hashValue[i]
                   << " vs Target=0x" << std::setw(16) << std::setfill('0') << targetValue[i];
                
                if (!decided) {
                    if (hashValue[i] < targetValue[i]) {
                        ss << " [VALID - hash < target]";
                        decided = true;
                    } else if (hashValue[i] > targetValue[i]) {
                        ss << " [INVALID - hash > target]";
                        decided = true;
                    } else {
                        ss << " [EQUAL - check next word]";
                    }
                } else {
                    ss << " [not checked]";
                }
            }
            
            ss << "\n  Final result: " << (isValid ? "VALID SHARE!" : "Does not meet target");
            
            if (isValid) {
                ss << "\n  Hash meets difficulty " << std::dec << (0xFFFFFFFFFFFFFFFFULL / (targetValue[0] > 0 ? targetValue[0] : 1));
            }
            
            Utils::threadSafePrint(ss.str(), true);
        }
        
        return isValid;
        
    }
    catch (...) {
        return false;
    }
}