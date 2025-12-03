#include "MiningThreadData.h"
#include "RandomXManager.h"
#include "Utils.h"
#include <chrono>
#include <vector>

MiningThreadData::MiningThreadData(int id) : threadId(id) {
    // Constructor body
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