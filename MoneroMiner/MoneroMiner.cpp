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

// Global variable declarations (not definitions)
extern std::atomic<uint32_t> activeJobId;
extern std::atomic<uint32_t> notifiedJobId;
extern std::atomic<bool> newJobAvailable;
extern std::atomic<uint64_t> acceptedShares;
extern std::atomic<uint64_t> rejectedShares;
extern std::atomic<uint64_t> jsonRpcId;
extern std::string sessionId;
extern std::vector<MiningThreadData*> threadData;

// Forward declarations
void printHelp();
bool validateConfig();
bool parseCommandLine(int argc, char* argv[]);
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
void miningThread(int threadId);

void printHelp() {
    std::cout << "MoneroMiner v1.0.0 - Lightweight High Performance Monero CPU Miner\n\n"
              << "Usage: MoneroMiner.exe [OPTIONS]\n\n"
              << "Options:\n"
              << "  --help               Show this help message\n"
              << "  --debug              Enable debug logging\n"
              << "  --logfile [FILE]     Enable logging to file\n"
              << "  --threads N          Number of mining threads\n"
              << "  --pool-address URL   Pool address\n"
              << "  --pool-port PORT     Pool port\n"
              << "  --wallet ADDRESS     Wallet address\n"
              << "  --worker-name NAME   Worker name\n"
              << "  --password PASS      Pool password\n"
              << "  --user-agent AGENT   User agent string\n\n"
              << "Example:\n"
              << "  MoneroMiner.exe --debug --logfile debug.log --threads 4\n"
              << std::endl;
}

bool validateConfig() {
    if (config.walletAddress.empty()) {
        threadSafePrint("Error: Wallet address is required\n", false);
        return false;
    }
    if (config.numThreads <= 0) {
        config.numThreads = std::thread::hardware_concurrency();
        if (config.numThreads == 0) {
            config.numThreads = 4;
        }
        threadSafePrint("Using " + std::to_string(config.numThreads) + " threads", false);
    }
    return true;
}

bool parseCommandLine(int argc, char* argv[]) {
    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        if (arg == "--help" || arg == "-h") {
            printHelp();
            return false;
        }
        else if (arg == "--debug") {
            config.debugMode = true;
        }
        else if (arg == "--logfile") {
            config.useLogFile = true;
            if (i + 1 < argc && argv[i + 1][0] != '-') {
                config.logFileName = argv[++i];
            }
        }
        else if (arg == "--threads" && i + 1 < argc) {
            int threads = std::stoi(argv[++i]);
            if (threads <= 0) {
                config.numThreads = std::thread::hardware_concurrency();
                threadSafePrint("Invalid thread count, using " + std::to_string(config.numThreads) + " threads", true);
            } else {
                config.numThreads = threads;
                threadSafePrint("Using " + std::to_string(config.numThreads) + " mining threads", true);
            }
        }
        else if (arg == "--pool" && i + 1 < argc) {
            std::string poolAddress = argv[++i];
            size_t colonPos = poolAddress.find(':');
            if (colonPos != std::string::npos) {
                config.poolAddress = poolAddress.substr(0, colonPos);
                config.poolPort = poolAddress.substr(colonPos + 1);
            }
        }
        else if (arg == "--wallet" && i + 1 < argc) {
            config.walletAddress = argv[++i];
        }
        else if (arg == "--worker" && i + 1 < argc) {
            config.workerName = argv[++i];
        }
        else if (arg == "--password" && i + 1 < argc) {
            config.password = argv[++i];
        }
        else if (arg == "--useragent" && i + 1 < argc) {
            config.userAgent = argv[++i];
        }
    }

    return true;
}

void signalHandler(int signum) {
    threadSafePrint("Received signal " + std::to_string(signum) + ", shutting down...", false);
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

void miningThread(int threadId) {
    try {
        threadSafePrint("Starting mining thread " + std::to_string(threadId), true);
        
        // Get thread data
        auto& data = threadData[threadId];
        if (!data) {
            threadSafePrint("Failed to get thread data for thread " + std::to_string(threadId), true);
            return;
        }

        // Initialize VM
        if (!data->initializeVM()) {
            threadSafePrint("Failed to initialize VM for thread " + std::to_string(threadId), true);
            return;
        }

        // Main mining loop
        while (!shouldStop) {
            try {
                // Wait for a job if we don't have one
                if (!data->hasJob()) {
                    std::unique_lock<std::mutex> lock(PoolClient::jobMutex);
                    PoolClient::jobQueueCondition.wait(lock, [&]() {
                        return !PoolClient::jobQueue.empty() || shouldStop;
                    });
                    
                    if (shouldStop) break;
                    
                    if (PoolClient::jobQueue.empty()) {
                        threadSafePrint("Thread " + std::to_string(threadId) + 
                            " woke up but no job available", true);
                        continue;
                    }
                    
                    // Get new job
                    Job newJob = PoolClient::jobQueue.front();
                    PoolClient::jobQueue.pop();
                    data->updateJob(newJob);
                    
                    if (debugMode) {
                        threadSafePrint("Thread " + std::to_string(threadId) + 
                            " received new job: " + newJob.getJobId(), true);
                    }
                }

                // Process current job
                Job* currentJob = data->getCurrentJob();
                if (!currentJob) {
                    threadSafePrint("Thread " + std::to_string(threadId) + 
                        " no current job available", true);
                    continue;
                }

                uint64_t nonce = data->getNonce();
                uint64_t hashCount = 0;
                
                // Set target in RandomXManager
                RandomXManager::setTarget(currentJob->getTarget());
                
                // Process hashes in batches
                const int BATCH_SIZE = 1000;
                while (!shouldStop && data->hasJob()) {
                    // Get current job again to check if it changed
                    Job* currentJobCheck = data->getCurrentJob();
                    if (!currentJobCheck || currentJobCheck->getJobId() != currentJob->getJobId()) {
                        break;
                    }
                    
                    // Calculate batch of hashes
                    for (int i = 0; i < BATCH_SIZE && !shouldStop; i++) {
                        std::vector<uint8_t> input = currentJob->getBlob();
                        uint64_t currentNonce = nonce + i;
                        
                        // Insert nonce into input
                        input[72] = (currentNonce >> 24) & 0xFF;
                        input[73] = (currentNonce >> 16) & 0xFF;
                        input[74] = (currentNonce >> 8) & 0xFF;
                        input[75] = currentNonce & 0xFF;
                        
                        // Calculate hash
                        uint8_t output[32];
                        if (data->calculateHash(input, currentNonce, output)) {
                            if (debugMode) {
                                threadSafePrint("Thread " + std::to_string(threadId) + 
                                    " found valid hash at nonce " + std::to_string(currentNonce), true);
                            }
                            
                            data->submitShare(output);
                        }
                        
                        hashCount++;
                    }
                    
                    // Update nonce
                    nonce += BATCH_SIZE;
                    data->setNonce(nonce);
                    
                    // Print hash rate every 1000 hashes
                    if (hashCount % 1000 == 0) {
                        threadSafePrint("Thread " + std::to_string(threadId) + 
                            " processed " + std::to_string(hashCount) + " hashes", true);
                    }
                }
            }
            catch (const std::exception& e) {
                threadSafePrint("Error in mining thread " + std::to_string(threadId) + 
                    ": " + std::string(e.what()), true);
                std::this_thread::sleep_for(std::chrono::seconds(1));
            }
        }
    }
    catch (const std::exception& e) {
        threadSafePrint("Fatal error in mining thread " + std::to_string(threadId) + 
            ": " + std::string(e.what()), true);
    }
    catch (...) {
        threadSafePrint("Unknown fatal error in mining thread " + std::to_string(threadId), true);
    }
    
    threadSafePrint("Mining thread " + std::to_string(threadId) + " stopped", true);
}

void processNewJob(const picojson::object& jobObj) {
    try {
        // Extract job details
        std::string jobId = jobObj.at("job_id").get<std::string>();
        std::string blob = jobObj.at("blob").get<std::string>();
        std::string target = jobObj.at("target").get<std::string>();
        uint64_t height = static_cast<uint64_t>(jobObj.at("height").get<double>());
        std::string seedHash = jobObj.at("seed_hash").get<std::string>();

        // Create new job with explicit cast to uint32_t
        Job newJob(jobId, blob, target, static_cast<uint32_t>(height), seedHash);

        // Update active job ID atomically
        uint32_t jobIdNum = static_cast<uint32_t>(std::stoul(jobId));
        if (jobIdNum != activeJobId.load()) {
            activeJobId.store(jobIdNum);
            notifiedJobId.store(jobIdNum);

            // Initialize RandomX with new seed hash if needed
            if (!RandomXManager::initialize(seedHash)) {
                threadSafePrint("Failed to initialize RandomX with seed hash: " + seedHash, true);
                return;
            }

            // Clear existing job queue and add new job
            {
                std::lock_guard<std::mutex> lock(PoolClient::jobMutex);
                // Clear the queue
                std::queue<Job> empty;
                std::swap(PoolClient::jobQueue, empty);
                // Add the new job
                PoolClient::jobQueue.push(newJob);
                
                if (debugMode) {
                    threadSafePrint("Job queue updated with new job: " + jobId, true);
                    threadSafePrint("Queue size: " + std::to_string(PoolClient::jobQueue.size()), true);
                }
            }

            // Print job details
            threadSafePrint("New job details:", true);
            threadSafePrint("  Height: " + std::to_string(height), true);
            threadSafePrint("  Job ID: " + jobId, true);
            threadSafePrint("  Target: 0x" + target, true);
            threadSafePrint("  Blob: " + blob, true);
            threadSafePrint("  Seed Hash: " + seedHash, true);
            
            // Calculate and print difficulty
            uint64_t targetValue = std::stoull(target, nullptr, 16);
            uint32_t exponent = (targetValue >> 24) & 0xFF;
            uint32_t mantissa = targetValue & 0xFFFFFF;
            
            // Calculate expanded target
            uint64_t expandedTarget = 0;
            if (exponent <= 3) {
                expandedTarget = mantissa >> (8 * (3 - exponent));
            } else {
                expandedTarget = static_cast<uint64_t>(mantissa) << (8 * (exponent - 3));
            }
            
            // Calculate difficulty
            double difficulty = 0xFFFFFFFFFFFFFFFFULL / static_cast<double>(expandedTarget);
            threadSafePrint("  Difficulty: " + std::to_string(difficulty), true);

            // Update thread data with new job
            for (auto* data : MiningStats::threadData) {
                if (data) {
                    data->updateJob(newJob);
                    if (debugMode) {
                        threadSafePrint("Updated thread " + std::to_string(data->getThreadId()) + 
                            " with new job: " + jobId, true);
                    }
                }
            }

            // Notify all mining threads about the new job
            PoolClient::jobQueueCondition.notify_all();
            if (debugMode) {
                threadSafePrint("Notified all mining threads about new job", true);
            }

            threadSafePrint("Job processed and distributed to all threads", true);
        } else {
            if (debugMode) {
                threadSafePrint("Skipping duplicate job: " + jobId, true);
            }
        }
    }
    catch (const std::exception& e) {
        threadSafePrint("Error processing job: " + std::string(e.what()), true);
    }
    catch (...) {
        threadSafePrint("Unknown error processing job", true);
    }
}

bool submitShare(const std::string& jobId, const std::string& nonce, const std::string& hash, const std::string& algo) {
    if (PoolClient::sessionId.empty()) {
        threadSafePrint("Cannot submit share: Not logged in", true);
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
        threadSafePrint("Failed to parse share response: " + err, true);
        accepted = false;
        return;
    }

    if (!v.is<picojson::object>()) {
        threadSafePrint("Invalid share response format", true);
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
                    threadSafePrint("Share accepted!", true);
                } else {
                    rejectedShares++;
                    threadSafePrint("Share rejected: " + status, true);
                }
            }
        }
    } else if (obj.find("error") != obj.end()) {
        const picojson::value& error = obj.at("error");
        if (error.is<picojson::object>()) {
            const picojson::object& errorObj = error.get<picojson::object>();
            if (errorObj.find("message") != errorObj.end()) {
                threadSafePrint("Share submission error: " + errorObj.at("message").get<std::string>(), true);
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
        threadSafePrint("Failed to send data: " + std::to_string(error), true);
        return "";
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
        threadSafePrint("Timeout waiting for response", true);
        return "";
    }
    if (result == SOCKET_ERROR) {
        threadSafePrint("Select error: " + std::to_string(WSAGetLastError()), true);
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
            threadSafePrint("Failed to receive data: " + std::to_string(error), true);
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
        threadSafePrint("Received empty response", true);
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
            threadSafePrint("JSON parse error: " + err, true);
            return;
        }

        if (!v.is<picojson::object>()) {
            threadSafePrint("Invalid JSON response format", true);
            return;
        }

        const picojson::object& obj = v.get<picojson::object>();
        if (obj.find("result") == obj.end()) {
            threadSafePrint("No result in response", true);
            return;
        }

        const picojson::object& result = obj.at("result").get<picojson::object>();
        if (result.find("id") == result.end()) {
            threadSafePrint("No session ID in response", true);
            return;
        }

        sessionId = result.at("id").get<std::string>();
        threadSafePrint("Session ID: " + sessionId, true);

        // Process the job from login response
        if (result.find("job") != result.end()) {
            const picojson::object& jobObj = result.at("job").get<picojson::object>();
            processNewJob(jobObj);
        } else {
            threadSafePrint("No job in login response", true);
        }
    }
    catch (const std::exception& e) {
        threadSafePrint("Error processing login response: " + std::string(e.what()), true);
    }
    catch (...) {
        threadSafePrint("Unknown error processing login response", true);
    }
}

int main(int argc, char* argv[]) {
    try {
        // Parse command line arguments and initialize config
        if (!config.parseCommandLine(argc, argv)) {
            return 1;
        }

        // Print current configuration
        config.printConfig();

        // Initialize Winsock
        if (!PoolClient::initialize()) {
            std::cerr << "Failed to initialize Winsock" << std::endl;
            return 1;
        }

        // Connect to pool
        if (!PoolClient::connect(config.poolAddress, config.poolPort)) {
            std::cerr << "Failed to connect to pool" << std::endl;
            PoolClient::cleanup();
            return 1;
        }

        // Login to pool
        if (!PoolClient::login(config.walletAddress, config.password, config.workerName, config.userAgent)) {
            std::cerr << "Failed to login to pool" << std::endl;
            PoolClient::cleanup();
            return 1;
        }

        // Initialize thread data
        threadData.resize(config.numThreads);
        for (int i = 0; i < config.numThreads; i++) {
            threadData[i] = new MiningThreadData(i);
            if (!threadData[i]) {
                threadSafePrint("Failed to create thread data for thread " + std::to_string(i), true);
                // Cleanup
                for (int j = 0; j < i; j++) {
                    delete threadData[j];
                }
                threadData.clear();
                PoolClient::cleanup();
                return 1;
            }
        }

        // Start job listener thread
        std::thread jobListenerThread(PoolClient::jobListener);

        // Start mining threads
        std::vector<std::thread> miningThreads;
        for (int i = 0; i < config.numThreads; i++) {
            miningThreads.emplace_back(miningThread, i);
        }

        // Wait for mining threads to complete
        for (auto& thread : miningThreads) {
            thread.join();
        }
        
        // Wait for job listener thread
        jobListenerThread.join();
    
        // Cleanup
        for (auto* data : threadData) {
            delete data;
        }
        threadData.clear();
        PoolClient::cleanup();
        return 0;
    }
    catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        // Cleanup
        for (auto* data : threadData) {
            delete data;
        }
        threadData.clear();
        PoolClient::cleanup();
        return 1;
    }
} 