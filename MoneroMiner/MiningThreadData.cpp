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
        // CRITICAL: Each word is constructed from 8 consecutive bytes in LE order
        std::array<uint64_t, 4> hashValue = {0, 0, 0, 0};
        for (int wordIdx = 0; wordIdx < 4; wordIdx++) {
            uint64_t word = 0;
            int baseByteIdx = wordIdx * 8;
            // Build word from bytes in little-endian order
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
        
        // Compare 256-bit values from MSW to LSW
        bool isValid = false;
        for (int i = 3; i >= 0; i--) {
            if (hashValue[i] < targetValue[i]) {
                isValid = true;
                break;
            }
            if (hashValue[i] > targetValue[i]) {
                isValid = false;
                break;
            }
        }
        
        // Debug output
        if (config.debugMode && (isValid || (totalHashes % 10000 == 0))) {
            std::stringstream ss;
            ss << "\n[T" << threadId << " PoW CHECK @ " << totalHashes << " hashes]\n";
            
            // Display full hash (MSW to LSW for readability)
            ss << "  Hash (BE display): 0x";
            for (int i = 3; i >= 0; i--) {
                ss << std::hex << std::setw(16) << std::setfill('0') << hashValue[i];
            }
            
            ss << "\n  Target (BE):       0x";
            for (int i = 3; i >= 0; i--) {
                ss << std::hex << std::setw(16) << std::setfill('0') << targetValue[i];
            }
            
            ss << "\n  Word-by-word (MSW→LSW):";
            bool decided = false;
            for (int i = 3; i >= 0; i--) {
                ss << "\n    [" << i << "] Hash=0x" << std::hex << std::setw(16) << std::setfill('0') << hashValue[i]
                   << " vs Target=0x" << std::setw(16) << std::setfill('0') << targetValue[i];
                
                if (!decided) {
                    if (hashValue[i] < targetValue[i]) {
                        ss << " ✓ VALID";
                        decided = true;
                    } else if (hashValue[i] > targetValue[i]) {
                        ss << " ✗ FAIL";
                        decided = true;
                    } else {
                        ss << " = (continue)";
                    }
                }
            }
            
            ss << "\n  Result: " << (isValid ? "**VALID SHARE**" : "Does not meet target");
            Utils::threadSafePrint(ss.str(), true);
        }
        
        return isValid;
        
    }
    catch (...) {
        return false;
    }
}