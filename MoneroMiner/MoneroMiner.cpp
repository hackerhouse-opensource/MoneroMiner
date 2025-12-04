/**
 * MoneroMiner.cpp - Lightweight High Performance Monero (XMR) CPU Miner
 * 
 * Implementation file containing the core mining functionality.
 * 
 * Author: Hacker Fantastic (https://hacker.house)
 * License: Attribution-NonCommercial-NoDerivatives 4.0 International
 * https://creativecommons.org/licenses/by-nc-nd/4.0/
 */
 
#include "Config.h"
#include "PoolClient.h"
#include "RandomXManager.h"
#include "MiningStats.h"
#include "Utils.h"
#include "Job.h"
#include "Globals.h"
#include <iostream>
#include <thread>
#include <vector>
#include <mutex>
#include <condition_variable>
#include <csignal>
#include <chrono>
#include <iomanip>
#include <sstream>
#include <fstream>
#include <unordered_set>
#include <array> // Add for 256-bit target

// Global variable declarations (not definitions)
extern std::atomic<uint32_t> activeJobId;
extern std::atomic<uint32_t> notifiedJobId;
extern std::atomic<bool> newJobAvailable;
extern std::atomic<uint64_t> acceptedShares;
extern std::atomic<uint64_t> rejectedShares;
extern std::atomic<uint64_t> jsonRpcId;
extern std::string sessionId;
extern std::vector<MiningThreadData*> threadData;

// Global variables
std::vector<std::thread> miningThreads;
std::thread jobListenerThread;

// Forward declarations
void printHelp();
bool validateConfig();
void signalHandler(int signum);
void printConfig();
std::string createSubmitPayload(const std::string& sessionId, const std::string& jobId,
                              const std::string& nonceHex, const std::string& hashHex,
                              const std::string& algo);
void handleShareResponse(const std::string& response, bool& accepted);
std::string sendAndReceive(SOCKET sock, const std::string& payload);
bool submitShare(const std::string& jobId, const std::string& nonce, const std::string& hash, const std::string& algo);
void handleLoginResponse(const std::string& response);
void processNewJob(const picojson::object& jobObj);
void miningThread(MiningThreadData* data);
bool loadConfig();
bool startMining();

// Utility: Convert difficulty to 256-bit target (big-endian)
std::array<uint8_t, 32> difficultyToTarget(uint64_t difficulty) {
    std::array<uint8_t, 32> target{};
    if (difficulty == 0) {
        // Set target to max (all 0xFF)
        target.fill(0xFF);
        return target;
    }
    // Calculate: target = (2^256 - 1) / difficulty
    // We'll use manual 256-bit division
    // 2^256 - 1 = all bytes 0xFF
    // We'll use a reference implementation for this calculation
    // For Monero, pools send target as a 32-byte hex string, but we need to calculate it if only difficulty is given

    // Use reference: xmrig's implementation
    // target = (uint256_t(1) << 256) / difficulty;
    // We'll use a simple approach for now:
    // Only support up to 64-bit difficulty, so high bytes are zero
    uint64_t quotient = ~0ULL / difficulty;
    // Place quotient in the last 8 bytes (big-endian)
    for (int i = 0; i < 8; ++i) {
        target[24 + i] = (quotient >> (8 * (7 - i))) & 0xFF;
    }
    // The rest are zero
    return target;
}

// Utility: Print 256-bit value as hex
std::string print256Hex(const uint8_t* bytes) {
    std::stringstream ss;
    for (int i = 0; i < 32; ++i) {
        ss << std::hex << std::setw(2) << std::setfill('0') << (int)bytes[i];
    }
    return ss.str();
}

// Utility: Compare 256-bit big-endian values
bool meetsTarget256(const uint8_t* hash, const uint8_t* target) {
    for (int i = 0; i < 32; ++i) {
        if (hash[i] < target[i]) return true;
        if (hash[i] > target[i]) return false;
    }
    return true; // hash == target
}

void printHelp() {
    std::cout << "MoneroMiner - A Monero (XMR) mining program\n\n"
              << "Usage: MoneroMiner [options]\n\n"
              << "Options:\n"
              << "  --help               Show this help message\n"
              << "  --debug              Enable debug output\n"
              << "  --logfile            Enable logging to file\n"
              << "  --threads N          Number of mining threads (default: 1)\n"
              << "  --pool ADDRESS:PORT  Pool address and port (default: xmr-eu1.nanopool.org:14444)\n"
              << "  --wallet ADDRESS      Your Monero wallet address\n"
              << "  --worker NAME        Worker name (default: worker1)\n"
              << "  --password X         Pool password (default: x)\n"
              << "  --useragent AGENT    User agent string (default: MoneroMiner/1.0.0)\n\n"
              << "Example:\n"
              << "  MoneroMiner --debug --logfile --threads 4 --wallet YOUR_WALLET_ADDRESS\n"
              << std::endl;
}

bool validateConfig() {
    if (config.walletAddress.empty()) {
        Utils::threadSafePrint("Error: Wallet address is required\n", false);
        return false;
    }
    if (config.numThreads <= 0) {
        config.numThreads = std::thread::hardware_concurrency();
        if (config.numThreads == 0) {
            config.numThreads = 4;
        }
        Utils::threadSafePrint("Using " + std::to_string(config.numThreads) + " threads", false);
    }
    return true;
}

void signalHandler(int signum) {
    Utils::threadSafePrint("Received signal " + std::to_string(signum) + ", shutting down...", false);
    shouldStop = true;
}

void printConfig() {
    std::cout << "Current Configuration:" << std::endl;
    std::cout << "  Pool Address: " << config.poolAddress << ":" << config.poolPort << std::endl;
    std::cout << "  Wallet: " << config.walletAddress << std::endl;
    std::cout << "  Worker Name: " << config.workerName << std::endl;
    std::cout << "  User Agent: " << config.userAgent << std::endl;
    std::cout << "  Threads: " << config.numThreads << std::endl;
    std::cout << "  Debug Mode: " << (config.debugMode ? "Yes" : "No") << std::endl;
    std::cout << "  Log File: " << (config.useLogFile ? config.logFileName : "Disabled") << std::endl;
    std::cout << std::endl;
}

void miningThread(MiningThreadData* data) {
    try {
        if (!data || !data->initializeVM()) {
            Utils::threadSafePrint("Thread " + std::to_string(data->getThreadId()) + " init failed", true);
            return;
        }

        uint64_t localNonce = static_cast<uint64_t>(data->getThreadId()) * 0x10000000ULL;
        uint64_t maxNonce = localNonce + 0x10000000ULL;
        std::string lastJobId;
        auto lastHashrateUpdate = std::chrono::steady_clock::now();
        uint64_t hashesInPeriod = 0;
        uint64_t hashesTotal = 0;
        std::vector<uint8_t> workingBlob;
        workingBlob.reserve(128);
        std::vector<uint8_t> hashResult(32);
        uint64_t debugHashCounter = 0;

        if (config.debugMode) {
            std::string msg = "[T" + std::to_string(data->getThreadId()) + "] Started | Nonce range: 0x" + Utils::formatHex(localNonce, 8) + " - 0x" + Utils::formatHex(maxNonce, 8) + "\n";
            Utils::threadSafePrint(msg, true);
        }

        while (!shouldStop) {
            try {
                Job jobCopy;
                std::string currentJobId;
                
                {
                    std::lock_guard<std::mutex> lock(PoolClient::jobMutex);
                    if (PoolClient::jobQueue.empty()) {
                        std::this_thread::sleep_for(std::chrono::milliseconds(100));
                        continue;
                    }
                    
                    // CRITICAL: Use copy constructor, NOT assignment!
                    jobCopy = PoolClient::jobQueue.front();  // This calls operator=
                    currentJobId = jobCopy.getJobId();
                }
                
                // Remove the debug spam - only log once per job change
                if (currentJobId != lastJobId) {
                    if (config.debugMode) {
                        std::stringstream ss;
                        ss << "\n[T" << data->getThreadId() << "] *** NEW JOB *** " << currentJobId
                           << " | Height: " << jobCopy.height
                           << " | Diff: " << jobCopy.difficulty
                           << " | Previous job hashes: " << hashesTotal << "\n";
                        ss << "Target (LE hex): ";
                        for (int i = 0; i < 32; i++) ss << std::hex << std::setw(2) << std::setfill('0') << (int)jobCopy.targetBytes[i];
                        
                        Utils::threadSafePrint(ss.str(), true);
                    }
                    
                    lastJobId = currentJobId;
                    localNonce = static_cast<uint64_t>(data->getThreadId()) * 0x10000000ULL;
                    maxNonce = localNonce + 0x10000000ULL;
                    hashesInPeriod = 0;
                    hashesTotal = 0;
                    debugHashCounter = 0;
                    lastHashrateUpdate = std::chrono::steady_clock::now();
                    
                    // Don't loop here - process immediately
                    continue;
                }

                if (localNonce >= maxNonce) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(100));
                    continue;
                }

                workingBlob = jobCopy.getBlobBytes();
                if (workingBlob.empty() || workingBlob.size() < 76) {
                    if (config.debugMode) {
                        Utils::threadSafePrint("[T" + std::to_string(data->getThreadId()) + "] FATAL: Blob too short", true);
                    }
                    std::this_thread::sleep_for(std::chrono::milliseconds(100));
                    continue;
                }

                if (jobCopy.nonceOffset + 4 > workingBlob.size()) {
                    if (config.debugMode) {
                        Utils::threadSafePrint("[T" + std::to_string(data->getThreadId()) + "] FATAL: Invalid nonce offset", true);
                    }
                    std::this_thread::sleep_for(std::chrono::milliseconds(100));
                    continue;
                }

                // CRITICAL: Insert nonce as LITTLE-ENDIAN 32-bit value
                uint32_t nonce32 = static_cast<uint32_t>(localNonce & 0xFFFFFFFFULL);
                size_t offset = jobCopy.nonceOffset;
                workingBlob[offset + 0] = static_cast<uint8_t>((nonce32 >>  0) & 0xFF);
                workingBlob[offset + 1] = static_cast<uint8_t>((nonce32 >>  8) & 0xFF);
                workingBlob[offset + 2] = static_cast<uint8_t>((nonce32 >> 16) & 0xFF);
                workingBlob[offset + 3] = static_cast<uint8_t>((nonce32 >> 24) & 0xFF);

                // Calculate hash
                bool hashOk = false;
                {
                    std::fill(hashResult.begin(), hashResult.end(), 0);
                    hashOk = data->calculateHashAndCheckTarget(workingBlob, std::vector<uint8_t>(jobCopy.targetBytes, jobCopy.targetBytes + 32), hashResult);
                }

                debugHashCounter++;
                
                // Enhanced debug output every 10,000 hashes with detailed byte comparison
                if (config.debugMode && (debugHashCounter % 10000 == 0)) {
                    std::stringstream ss;
                    ss << "[T" << data->getThreadId() << "] Hash #" << debugHashCounter 
                       << " | Nonce: 0x" << std::hex << std::setw(8) << std::setfill('0') << nonce32 << "\n";
                    
                    ss << "  Hash (LE):   ";
                    for (int i = 0; i < 32; i++) {
                        ss << std::hex << std::setw(2) << std::setfill('0') << (int)hashResult[i];
                        if (i == 7 || i == 15 || i == 23) ss << " ";
                    }
                    
                    ss << "\n  Target (LE): ";
                    for (int i = 0; i < 32; i++) {
                        ss << std::hex << std::setw(2) << std::setfill('0') << (int)jobCopy.targetBytes[i];
                        if (i == 7 || i == 15 || i == 23) ss << " ";
                    }
                    
                    // Detailed byte-by-byte comparison (first 8 bytes)
                    ss << "\n  Byte-by-byte comparison (LE order):";
                    bool hashStillValid = true;
                    for (int i = 0; i < 8; i++) {
                        ss << "\n    Byte[" << i << "]: Hash=0x" 
                           << std::hex << std::setw(2) << std::setfill('0') << (int)hashResult[i]
                           << " vs Target=0x" 
                           << std::hex << std::setw(2) << std::setfill('0') << (int)jobCopy.targetBytes[i];
                        
                        if (hashStillValid) {
                            if (hashResult[i] < jobCopy.targetBytes[i]) {
                                ss << " [PASS - hash byte is lower]";
                                hashStillValid = false; // Rest doesn't matter
                            } else if (hashResult[i] > jobCopy.targetBytes[i]) {
                                ss << " [FAIL - hash byte is higher]";
                                hashStillValid = false;
                            } else {
                                ss << " [EQUAL - continue to next byte]";
                            }
                        }
                    }
                    
                    ss << "\n  Result: " << (hashOk ? "VALID SHARE" : "Does not meet target");
                    ss << "\n  Expected shares so far: " << std::fixed << std::setprecision(3) 
                       << (static_cast<double>(hashesTotal) / static_cast<double>(jobCopy.difficulty));
                    
                    bool isAllZeros = std::all_of(hashResult.begin(), hashResult.end(), [](uint8_t b){ return b == 0; });
                    if (isAllZeros) {
                        ss << "\n  [WARNING: Hash is all zeros - VM calculation error!]";
                    }
                    
                    Utils::threadSafePrint(ss.str(), true);
                }

                // Check for valid share
                bool isAllZeros = std::all_of(hashResult.begin(), hashResult.end(), [](uint8_t b){ return b == 0; });
                if (hashOk && !isAllZeros) {
                    // CRITICAL FIX: Format nonce as 8-character hex (little-endian bytes)
                    std::stringstream nonceStream;
                    nonceStream << std::hex << std::setw(2) << std::setfill('0') << (int)workingBlob[offset + 0];
                    nonceStream << std::hex << std::setw(2) << std::setfill('0') << (int)workingBlob[offset + 1];
                    nonceStream << std::hex << std::setw(2) << std::setfill('0') << (int)workingBlob[offset + 2];
                    nonceStream << std::hex << std::setw(2) << std::setfill('0') << (int)workingBlob[offset + 3];
                    std::string nonceHex = nonceStream.str();
                    
                    // CRITICAL FIX: Send hash in LITTLE-ENDIAN byte order (as calculated by RandomX)
                    std::stringstream hashStream;
                    for (int i = 0; i < 32; i++) {
                        hashStream << std::hex << std::setw(2) << std::setfill('0') << (int)hashResult[i];
                    }
                    std::string hashHex = hashStream.str();

                    Utils::threadSafePrint("\n*** VALID SHARE FOUND! ***", true);
                    Utils::threadSafePrint("  Job: " + currentJobId, true);
                    Utils::threadSafePrint("  Nonce: " + nonceHex, true);
                    Utils::threadSafePrint("  Hash: " + hashHex, true);
                    Utils::threadSafePrint("  After " + std::to_string(hashesTotal) + " hashes", true);
                    
                    if (config.debugMode) {
                        Utils::threadSafePrint("  Blob with nonce (first 50 bytes): ", true);
                        for (size_t i = 0; i < 50 && i < workingBlob.size(); i++) {
                            std::cout << std::hex << std::setw(2) << std::setfill('0') << (int)workingBlob[i];
                        }
                        std::cout << std::dec << std::endl;
                    }

                    // Submit share - let PoolClient handle the actual submission format
                    if (PoolClient::submitShare(currentJobId, nonceHex, hashHex, "rx/0")) {
                        data->incrementAccepted();
                        Utils::threadSafePrint("*** SHARE ACCEPTED BY POOL! *** Total: " + std::to_string(acceptedShares.load()), true);
                    } else {
                        data->incrementRejected();
                        Utils::threadSafePrint("Share rejected by pool. Total: " + std::to_string(rejectedShares.load()), true);
                    }
                }

                hashesInPeriod++;
                hashesTotal++;

                localNonce++;
                auto now = std::chrono::steady_clock::now();
                auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - lastHashrateUpdate).count();
                if (elapsed >= 5 && hashesInPeriod > 0) {
                    double hashrate = static_cast<double>(hashesInPeriod) / static_cast<double>(elapsed);
                    data->setHashrate(hashrate);
                    
                    if (config.debugMode) {
                        Utils::threadSafePrint("[T" + std::to_string(data->getThreadId()) + "] Hashrate: " + 
                            std::to_string(static_cast<int>(hashrate)) + " H/s | Total: " + std::to_string(hashesTotal), true);
                    }
                    
                    lastHashrateUpdate = now;
                    hashesInPeriod = 0;
                }
                
                if ((localNonce & 0xFF) == 0) {
                    std::this_thread::yield();
                }
            }
            catch (const std::exception& e) {
                Utils::threadSafePrint("Thread error: " + std::string(e.what()), true);
                std::this_thread::sleep_for(std::chrono::seconds(1));
            }
        }
    }
    catch (const std::exception& e) {
        Utils::threadSafePrint("Fatal thread error: " + std::string(e.what()), true);
    }
}

void processNewJob(const picojson::object& jobObj) {
    try {
        // Extract job details
        std::string jobId = jobObj.at("job_id").get<std::string>();
        std::string blob = jobObj.at("blob").get<std::string>();
        std::string target = jobObj.at("target").get<std::string>();
        uint64_t height = static_cast<uint64_t>(jobObj.at("height").get<double>());
        std::string seedHash = jobObj.at("seed_hash").get<std::string>();

        Job job(blob, jobId, target, height, seedHash);

        // Set target in RandomXManager (for legacy compatibility)
        if (!RandomXManager::setTargetAndDifficulty(target)) {
            Utils::threadSafePrint("Failed to set target", true);
            return;
        }

        // DON'T overwrite job.difficulty - it's already calculated correctly in Job constructor!
        // job.difficulty is already set from the target in Job::Job()

        // FIX C4244 WARNING - Explicit intermediate variable
        uint32_t jobIdNum = 0;
        try {
            size_t pos = 0;
            uint32_t temp = static_cast<uint32_t>(std::stoul(jobId, &pos, 10));
            jobIdNum = temp;
        } catch (const std::exception&) {
            jobIdNum = static_cast<uint32_t>(std::hash<std::string>{}(jobId));
        }
        
        if (jobIdNum != activeJobId.load()) {
            activeJobId.store(jobIdNum);
            notifiedJobId.store(jobIdNum);

            if (!RandomXManager::initialize(seedHash)) {
                Utils::threadSafePrint("Failed to initialize RandomX with seed hash: " + seedHash, true);
                return;
            }

            {
                std::lock_guard<std::mutex> lock(PoolClient::jobMutex);
                while (!PoolClient::jobQueue.empty()) {
                    PoolClient::jobQueue.pop();
                }
                PoolClient::jobQueue.push(job);
            }

            if (!config.debugMode) {
                Utils::threadSafePrint("New job: " + jobId + " | Height: " + std::to_string(height), false);
            } else {
                Utils::threadSafePrint("New job details:", true);
                Utils::threadSafePrint("  Height: " + std::to_string(height), true);
                Utils::threadSafePrint("  Job ID: " + jobId, true);
                Utils::threadSafePrint("  Target: 0x" + target, true);
                Utils::threadSafePrint("  Difficulty: " + std::to_string(job.difficulty), true);
            }

            PoolClient::jobAvailable.notify_all();
            PoolClient::jobQueueCondition.notify_all();
        }
    }
    catch (const std::exception& e) {
        Utils::threadSafePrint("Error processing job: " + std::string(e.what()), true);
    }
    catch (...) {
        Utils::threadSafePrint("Unknown error processing job", true);
    }
}

bool submitShare(const std::string& jobId, const std::string& nonce, const std::string& hash, const std::string& algo) {
    if (PoolClient::sessionId.empty()) {
        Utils::threadSafePrint("Cannot submit share: Not logged in", true);
        return false;
    }

    std::string payload = createSubmitPayload(PoolClient::sessionId, jobId, nonce, hash, algo);
    std::string response = PoolClient::sendAndReceive(payload);

    bool accepted = false;
    handleShareResponse(response, accepted);
    return accepted;
}

void handleShareResponse(const std::string& response, bool& accepted) {
    picojson::value v;
    std::string err = picojson::parse(v, response);
    if (!err.empty()) {
        Utils::threadSafePrint("Failed to parse share response: " + err, true);
        accepted = false;
        return;
    }

    if (!v.is<picojson::object>()) {
        Utils::threadSafePrint("Invalid share response format", true);
        accepted = false;
        return;
    }

    const picojson::object& obj = v.get<picojson::object>();
    if (obj.find("result") != obj.end()) {
        const picojson::value& result = obj.at("result");
        if (result.is<picojson::object>()) {
            const picojson::object& resultObj = result.get<picojson::object>();
            if (resultObj.find("status") != resultObj.end()) {
                const std::string& status = resultObj.at("status").get<std::string>();
                accepted = (status == "OK");
                if (accepted) {
                    acceptedShares++;
                    Utils::threadSafePrint("Share accepted!", true);
                } else {
                    rejectedShares++;
                    Utils::threadSafePrint("Share rejected: " + status, true);
                }
            }
        }
    } else if (obj.find("error") != obj.end()) {
        const picojson::value& error = obj.at("error");
        if (error.is<picojson::object>()) {
            const picojson::object& errorObj = error.get<picojson::object>();
            if (errorObj.find("message") != errorObj.end()) {
                Utils::threadSafePrint("Share submission error: " + errorObj.at("message").get<std::string>(), true);
            }
        }
        accepted = false;
        rejectedShares++;
    }
}

std::string sendAndReceive(SOCKET sock, const std::string& payload) {
    // Add newline to payload
    std::string fullPayload = payload + "\n";

    // Send the payload
    int bytesSent = send(sock, fullPayload.c_str(), static_cast<int>(fullPayload.length()), 0);
    if (bytesSent == SOCKET_ERROR) {
        int error = WSAGetLastError();
        Utils::threadSafePrint("Failed to send data: " + std::to_string(error), true);
        return "";
    }

    if (config.debugMode) {
        Utils::threadSafePrint("[POOL] Sent: " + payload, true);
    }

    // Set up select for timeout
    fd_set readSet;
    FD_ZERO(&readSet);
    FD_SET(sock, &readSet);

    struct timeval timeout;
    timeout.tv_sec = 10;  // 10 second timeout
    timeout.tv_usec = 0;

    // Wait for data with timeout
    int result = select(0, &readSet, nullptr, nullptr, &timeout);
    if (result == 0) {
        Utils::threadSafePrint("Timeout waiting for response", true);
        return "";
    }
    if (result == SOCKET_ERROR) {
        Utils::threadSafePrint("Select error: " + std::to_string(WSAGetLastError()), true);
        return "";
    }

    // Receive response
    std::string response;
    char buffer[4096];
    int totalBytes = 0;
    
    while (true) {
        int bytesReceived = recv(sock, buffer, sizeof(buffer) - 1, 0);
        if (bytesReceived == SOCKET_ERROR) {
            int error = WSAGetLastError();
            Utils::threadSafePrint("Failed to receive data: " + std::to_string(error), true);
            return "";
        }
        if (bytesReceived == 0) {
            break;  // Connection closed
        }
        
        buffer[bytesReceived] = '\0';
        response += buffer;
        totalBytes += bytesReceived;

        // Check if we have a complete JSON response
        try {
            picojson::value v;
            std::string err = picojson::parse(v, response);
            if (err.empty()) {
                break;  // Valid JSON received
            }
        } catch (...) {
            // Continue receiving if JSON is incomplete
        }
    }

    // Clean up response
    while (!response.empty() && (response.back() == '\n' || response.back() == '\r')) {
        response.pop_back();
    }

    if (response.empty()) {
        Utils::threadSafePrint("Received empty response", true);
    }

    if (config.debugMode) {
        Utils::threadSafePrint("[POOL] Received: " + response, true);
    }

    return response;
}

std::string createSubmitPayload(const std::string& sessionId, const std::string& jobId,
                              const std::string& nonceHex, const std::string& hashHex,
                              const std::string& algo) {
    picojson::object submitObj;
    uint32_t id = jsonRpcId.fetch_add(1);
    submitObj["id"] = picojson::value(static_cast<double>(id));
    submitObj["method"] = picojson::value("submit");
    
    picojson::array params;
    params.push_back(picojson::value(sessionId));
    params.push_back(picojson::value(jobId));
    params.push_back(picojson::value(nonceHex));
    params.push_back(picojson::value(hashHex));
    params.push_back(picojson::value(algo));
    
    submitObj["params"] = picojson::value(params);
    
    return picojson::value(submitObj).serialize();
}

void handleLoginResponse(const std::string& response) {
    try {
        picojson::value v;
        std::string err = picojson::parse(v, response);
        if (!err.empty()) {
            Utils::threadSafePrint("JSON parse error: " + err, true);
            return;
        }

        if (!v.is<picojson::object>()) {
            Utils::threadSafePrint("Invalid JSON response format", true);
            return;
        }

        const picojson::object& obj = v.get<picojson::object>();
        if (obj.find("result") == obj.end()) {
            Utils::threadSafePrint("No result in response", true);
            return;
        }

        const picojson::object& result = obj.at("result").get<picojson::object>();
        if (result.find("id") == result.end()) {
            Utils::threadSafePrint("No session ID in response", true);
            return;
        }

        sessionId = result.at("id").get<std::string>();
        Utils::threadSafePrint("Session ID: " + sessionId, true);

        if (result.find("job") != result.end()) {
            const picojson::object& jobObj = result.at("job").get<picojson::object>();
            processNewJob(jobObj);
        } else {
            Utils::threadSafePrint("No job in login response", true);
        }
    }
    catch (const std::exception& e) {
        Utils::threadSafePrint("Error processing login response: " + std::string(e.what()), true);
    }
    catch (...) {
        Utils::threadSafePrint("Unknown error processing login response", true);
    }
}

bool loadConfig() {
    try {
        std::ifstream file("config.json");
        if (file.is_open()) {
            picojson::value v;
            std::string err = picojson::parse(v, file);
            if (err.empty() && v.is<picojson::object>()) {
                const picojson::object& obj = v.get<picojson::object>();
                
                if (obj.find("poolAddress") != obj.end()) {
                    config.poolAddress = obj.at("poolAddress").get<std::string>();
                }
                if (obj.find("poolPort") != obj.end()) {
                    config.poolPort = static_cast<int>(obj.at("poolPort").get<double>());
                }
                if (obj.find("walletAddress") != obj.end()) {
                    config.walletAddress = obj.at("walletAddress").get<std::string>();
                }
                if (obj.find("workerName") != obj.end()) {
                    config.workerName = obj.at("workerName").get<std::string>();
                }
                if (obj.find("password") != obj.end()) {
                    config.password = obj.at("password").get<std::string>();
                }
                if (obj.find("userAgent") != obj.end()) {
                    config.userAgent = obj.at("userAgent").get<std::string>();
                }
                if (obj.find("numThreads") != obj.end()) {
                    config.numThreads = static_cast<int>(obj.at("numThreads").get<double>());
                }
                if (obj.find("debugMode") != obj.end()) {
                    config.debugMode = obj.at("debugMode").get<bool>();
                }
                if (obj.find("useLogFile") != obj.end()) {
                    config.useLogFile = obj.at("useLogFile").get<bool>();
                }
                if (obj.find("logFileName") != obj.end()) {
                    config.logFileName = obj.at("logFileName").get<std::string>();
                }
                
                file.close();
                return true;
            }
            file.close();
        }
        return true;
    }
    catch (const std::exception& e) {
        Utils::threadSafePrint("Error loading config: " + std::string(e.what()), true);
        return false;
    }
}

bool startMining() {
    // Initialize network first
    if (!PoolClient::initialize()) {
        return false;
    }
    
    if (!PoolClient::connect()) {
        return false;
    }
    
    if (!PoolClient::login(config.walletAddress, config.password, 
                          config.workerName, config.userAgent)) {
        return false;
    }
    
    // Initialize RandomX before creating threads
    Job* currentJob = nullptr;
    {
        std::lock_guard<std::mutex> lock(PoolClient::jobMutex);
        if (!PoolClient::jobQueue.empty()) {
            currentJob = &PoolClient::jobQueue.front();
        }
    }

    if (!currentJob) {
        Utils::threadSafePrint("No job available for RandomX initialization", true);
        return false;
    }

    if (!RandomXManager::initialize(currentJob->seedHash)) {
        Utils::threadSafePrint("Failed to initialize RandomX", true);
        return false;
    }

    // Initialize thread data - FIX: use explicit cast to avoid warning
    threadData.resize(config.numThreads);
    for (size_t i = 0; i < static_cast<size_t>(config.numThreads); i++) {
        threadData[i] = new MiningThreadData(static_cast<int>(i));
        if (!threadData[i]->initializeVM()) {
            Utils::threadSafePrint("Failed to initialize VM for thread " + std::to_string(i), true);
            return false;
        }
        if (config.debugMode && i < 4) {
            Utils::threadSafePrint("VM ready for thread " + std::to_string(i), true);
        }
    }
    if (!config.debugMode) {
        Utils::threadSafePrint("Initialized " + std::to_string(config.numThreads) + " mining threads", true);
    }
    
    // Start job listener thread
    jobListenerThread = std::thread(PoolClient::jobListener);
    
    // Wait for first job
    {
        std::unique_lock<std::mutex> lock(PoolClient::jobMutex);
        PoolClient::jobAvailable.wait(lock, [] { return !PoolClient::jobQueue.empty() || shouldStop; });
    }

    if (shouldStop) {
        return false;
    }

    // Start mining threads - FIX: use int instead of size_t
    for (size_t i = 0; i < static_cast<size_t>(config.numThreads); i++) {
        miningThreads.emplace_back(miningThread, threadData[i]);
        if (config.debugMode) {
            Utils::threadSafePrint("Started mining thread " + std::to_string(i), true);
        }
    }
    
    if (!config.debugMode) {
        Utils::threadSafePrint("Mining started - Press Ctrl+C to stop", true);
    } else {
        Utils::threadSafePrint("=== MINING STARTED WITH " + std::to_string(config.numThreads) + " THREADS ===", true);
        Utils::threadSafePrint("Press Ctrl+C to stop mining", true);
    }
    
    return true;
}

int main(int argc, char* argv[]) {
    // Check for --help first
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            printHelp();
            return 0;
        }
    }

    // Get system info early
    SYSTEM_INFO sysInfo;
    GetSystemInfo(&sysInfo);

    // Initialize Winsock
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        std::cerr << "Failed to initialize Winsock" << std::endl;
        return 1;
    }

    // Load configuration FIRST
    if (!loadConfig()) {
        std::cerr << "Failed to load configuration" << std::endl;
        WSACleanup();
        return 1;
    }

    // THEN parse command line arguments (so they override config file)
    if (!config.parseCommandLine(argc, argv)) {
        WSACleanup();
        return 0;  // --help was shown
    }

    // Print current configuration
    config.printConfig();
    
    // Show system info with XMRig-style output
    if (config.debugMode) {
        Utils::threadSafePrint("=== MoneroMiner v1.0.0 ===", true);
        
        // CPU detection
        char cpuBrand[0x40];
        int cpuInfo[4] = {0};
        __cpuid(cpuInfo, 0x80000002);
        memcpy(cpuBrand, cpuInfo, sizeof(cpuInfo));
        __cpuid(cpuInfo, 0x80000003);
        memcpy(cpuBrand + 16, cpuInfo, sizeof(cpuInfo));
        __cpuid(cpuInfo, 0x80000004);
        memcpy(cpuBrand + 32, cpuInfo, sizeof(cpuInfo));
        
        Utils::threadSafePrint(std::string("CPU: ") + cpuBrand, true);
        Utils::threadSafePrint("Logical cores: " + std::to_string(sysInfo.dwNumberOfProcessors), true);
        
        // Memory info
        MEMORYSTATUSEX memInfo;
        memInfo.dwLength = sizeof(MEMORYSTATUSEX);
        GlobalMemoryStatusEx(&memInfo);
        Utils::threadSafePrint("Memory: " + std::to_string(memInfo.ullAvailPhys / (1024*1024*1024)) + "/" + 
                              std::to_string(memInfo.ullTotalPhys / (1024*1024*1024)) + " GB", true);
        
        Utils::threadSafePrint("Pool: " + config.poolAddress + ":" + std::to_string(config.poolPort), true);
        Utils::threadSafePrint("Threads: " + std::to_string(config.numThreads), true);
        Utils::threadSafePrint("Worker: " + config.workerName, true);
        Utils::threadSafePrint("Algorithm: RandomX (rx/0)", true);
    } else {
        Utils::threadSafePrint("MoneroMiner v1.0.0 | CPU:" + std::to_string(sysInfo.dwNumberOfProcessors) + 
            " cores | Pool:" + config.poolAddress + " | Threads:" + std::to_string(config.numThreads), true);
    }
    
    // Start mining
    if (!startMining()) {
        std::cerr << "Failed to start mining" << std::endl;
        WSACleanup();
        return 1;
    }

    // Start statistics monitoring thread
    std::thread statsThread(MiningStatsUtil::globalStatsMonitor);
    
    // Keep the miner running
    Utils::threadSafePrint("=== MINER IS NOW RUNNING ===", !config.debugMode);
    Utils::threadSafePrint("Press Ctrl+C to stop mining", !config.debugMode);
    
    // Main loop - keep program alive while mining threads work
    auto lastStatsTime = std::chrono::steady_clock::now();
    int secondsCounter = 0;
    
    while (!shouldStop) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
        secondsCounter++;
        
        // Display stats every 10 seconds in non-debug mode
        if (!config.debugMode && secondsCounter >= 10) {
            auto now = std::chrono::steady_clock::now();
            auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - lastStatsTime).count();
            
            if (elapsed >= 10) {
                // Calculate total hashrate
                double totalHashrate = 0.0;
                for (size_t i = 0; i < threadData.size(); i++) {
                    if (threadData[i] != nullptr) {
                        totalHashrate += threadData[i]->getHashrate();
                    }
                }
                
                // Get current difficulty
                uint64_t currentDiff = 0;
                {
                    std::lock_guard<std::mutex> lock(PoolClient::jobMutex);
                    if (!PoolClient::jobQueue.empty()) {
                        currentDiff = PoolClient::jobQueue.front().difficulty;
                    }
                }
                
                std::stringstream ss;
                ss << "[" << secondsCounter << "s] ";
                ss << "Hashrate: " << std::fixed << std::setprecision(1) << totalHashrate << " H/s";
                ss << " | Difficulty: " << currentDiff;
                ss << " | Accepted: " << acceptedShares.load();
                ss << " | Rejected: " << rejectedShares.load();
                
                Utils::threadSafePrint(ss.str(), false);
                lastStatsTime = now;
                secondsCounter = 0;
            }
        }
    }

    // Cleanup when stopped
    Utils::threadSafePrint("Shutting down miner...", true);
    
    // Wait for mining threads
    for (auto& thread : miningThreads) {
        if (thread.joinable()) {
            thread.join();
        }
    }
    
    // Wait for job listener thread
    if (jobListenerThread.joinable()) {
        jobListenerThread.join();
    }
    
    // Wait for stats thread
    if (statsThread.joinable()) {
        statsThread.join();
    }

    // Cleanup thread data
    for (auto* data : threadData) {
        delete data;
    }
    threadData.clear();
    
    // Cleanup RandomX and pool
    RandomXManager::cleanup();
    PoolClient::cleanup();
    
    // Cleanup Winsock
    WSACleanup();
    
    Utils::threadSafePrint("Miner stopped cleanly", true);
    return 0;
}