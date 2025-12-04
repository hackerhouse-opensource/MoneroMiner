#include "MiningThreadData.h"
#include "RandomXManager.h"
#include "Utils.h"
#include "Config.h"
#include "Globals.h"
#include "Difficulty.h"
#include <chrono>
#include <vector>
#include <sstream>
#include <iomanip>

MiningThreadData::MiningThreadData(int id) : threadId(id) {
}

MiningThreadData::~MiningThreadData() {
    RandomXManager::cleanupVM(threadId);
}

bool MiningThreadData::initializeVM() {
    return RandomXManager::initializeVM(threadId);
}

bool MiningThreadData::calculateHash(const std::vector<uint8_t>& input, uint64_t nonce) {
    bool result = RandomXManager::calculateHashForThread(threadId, input, nonce);
    incrementHashCount();
    return result;
}

bool MiningThreadData::calculateHashAndCheckTarget(
    const std::vector<uint8_t>& blob,
    const std::vector<uint8_t>& target,
    std::vector<uint8_t>& hashOut
) {
    randomx_vm* vm = RandomXManager::getVM(threadId);
    if (!vm || hashOut.size() != 32) {
        return false;
    }
    
    randomx_calculate_hash(vm, blob.data(), blob.size(), hashOut.data());
    incrementHashCount();
    
    // CRITICAL FIX: Proper 256-bit little-endian comparison
    uint256_t hashAsUint256;
    hashAsUint256.fromLittleEndian(hashOut.data());
    
    uint256_t targetAsUint256;
    targetAsUint256.fromLittleEndian(target.data());  // Target is also little-endian!
    
    bool valid = (hashAsUint256 <= targetAsUint256);
    
    static std::atomic<uint64_t> debugCounter{0};
    uint64_t count = debugCounter.fetch_add(1);
    
    if (config.debugMode && count < 10) {
        std::stringstream ss;
        ss << "[T" << threadId << "] Hash #" << count << "\n";
        ss << "  Hash (LE): ";
        for (int i = 0; i < 32; i++) {
            ss << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(hashOut[i]);
        }
        ss << "\n  Hash as uint256 (LE interpretation): " << hashAsUint256.toHex();
        ss << "\n  Target as uint256 (LE interpretation): " << targetAsUint256.toHex();
        ss << "\n  Valid: " << (valid ? "YES" : "NO");
        Utils::threadSafePrint(ss.str(), true);
    }
    
    if (valid && config.debugMode) {
        Utils::threadSafePrint("[T" + std::to_string(threadId) + "] *** VALID SHARE FOUND ***", true);
    }
    
    return valid;
}