#include "MiningThread.h"
#include "MiningThreadData.h"
#include "RandomXManager.h"
#include "MiningStats.h"
#include "Globals.h"
#include "Utils.h"
#include "Constants.h"
#include "Types.h"
#include <cstdint>
#include <sstream>
#include <iomanip>
#include <chrono>
#include <thread>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <vector>
#include <string>
#include <iostream>
#include <cstring>
#include <picojson.h>

bool submitShare(MiningThreadData* data, uint64_t nonce, const std::string& jobId, const std::vector<uint8_t>& hash) {
    std::stringstream ss;
    ss << std::hex << std::setfill('0');
    
    // Convert hash to hex string
    std::string hashHex;
    for (const auto& byte : hash) {
        ss << std::setw(2) << static_cast<int>(byte);
    }
    hashHex = ss.str();
    
    // Clear stringstream for reuse
    ss.str("");
    ss.clear();
    
    // Convert nonce to hex string
    ss << std::setw(16) << nonce;
    std::string nonceHex = ss.str();

    // Prepare submit request
    picojson::object submitRequest;
    submitRequest["id"] = picojson::value(static_cast<double>(2)); // Use different ID than login
    submitRequest["jsonrpc"] = picojson::value("2.0");
    submitRequest["method"] = picojson::value("submit");
    
    picojson::object params;
    params["id"] = picojson::value(PoolClient::getSessionId());
    params["job_id"] = picojson::value(jobId);
    params["nonce"] = picojson::value(nonceHex);
    params["result"] = picojson::value(hashHex);
    
    submitRequest["params"] = picojson::value(params);

    std::string submitJson = picojson::value(submitRequest).serialize();
    Utils::threadSafePrint("Submitting share: " + submitJson, true);

    // Send share to pool
    std::string response;
    if (!PoolClient::sendData(submitJson, response)) {
        Utils::threadSafePrint("Failed to send share submission request", true);
        return false;
    }

    Utils::threadSafePrint("Received share submission response: " + response, true);

    // Parse response
    picojson::value v;
    std::string err = picojson::parse(v, response);
    if (!err.empty()) {
        Utils::threadSafePrint("Failed to parse share submission response: " + err, true);
        return false;
    }

    if (!v.is<picojson::object>()) {
        Utils::threadSafePrint("Invalid share submission response format", true);
        return false;
    }

    picojson::object obj = v.get<picojson::object>();
    
    // Check for error
    if (obj.count("error") && !obj["error"].is<picojson::null>()) {
        Utils::threadSafePrint("Share rejected by pool: " + v.serialize(), true);
        MiningStats::incrementRejectedShares();
        return false;
    }

    // Check result
    if (obj.count("result") && obj["result"].is<picojson::object>()) {
        picojson::object result = obj["result"].get<picojson::object>();
        if (result.count("status") && result["status"].get<std::string>() == "OK") {
            Utils::threadSafePrint("Share accepted by pool!", true);
            MiningStats::incrementAcceptedShares();
            return true;
        }
    }

    Utils::threadSafePrint("Unexpected share submission response format: " + v.serialize(), true);
    MiningStats::incrementRejectedShares();
    return false;
}

void miningThread(MiningThreadData* data) {
    Utils::threadSafePrint("Mining thread " + std::to_string(data->getId()) + " started", true);
    
    // Initialize VM
    if (!RandomXManager::initializeVM(data->getId())) {
        Utils::threadSafePrint("Failed to initialize VM for thread " + std::to_string(data->getId()), true);
        return;
    }

    randomx_vm* vm = RandomXManager::getVM(data->getId());
    if (!vm) {
        Utils::threadSafePrint("Failed to get VM for thread " + std::to_string(data->getId()), true);
        return;
    }
    
    data->setVM(vm);
    data->setIsRunning(true);
    MiningStats::updateThreadStats(data);
    
    // Calculate nonce range for this thread
    uint32_t numThreads = config.numThreads;
    uint32_t threadId = data->getId();
    uint32_t nonceRange = 0xFFFFFFFF / numThreads;
    uint32_t nonceStart = nonceRange * threadId;
    uint32_t nonceEnd = (threadId == numThreads - 1) ? 0xFFFFFFFF : nonceRange * (threadId + 1);
    uint32_t nonce = nonceStart;

    while (true) {
        // Get current job
        std::unique_ptr<Job> currentJob = data->getCurrentJob();
        if (!currentJob) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            continue;
        }

        // Calculate hash
        if (!RandomXManager::calculateHash(data->getVM(), currentJob->getBlob(), data->getCurrentNonce())) {
            Utils::threadSafePrint("Failed to calculate hash for thread " + std::to_string(data->getThreadId()), true);
            continue;
        }

        // Check if hash meets target
        const std::vector<uint8_t>& lastHash = RandomXManager::getLastHash();
        if (RandomXManager::checkTarget(lastHash.data())) {
            Utils::threadSafePrint("\n!!! FOUND VALID SHARE !!!", true);
            
            // Submit share
            if (submitShare(data, data->getCurrentNonce(), currentJob->getJobId(), lastHash)) {
                Utils::threadSafePrint("Successfully submitted share for job " + currentJob->getJobId(), true);
            } else {
                Utils::threadSafePrint("Failed to submit share for job " + currentJob->getJobId(), true);
            }
        }

        // Increment nonce
        data->incrementNonce();
    }
    
    data->setIsRunning(false);
} 