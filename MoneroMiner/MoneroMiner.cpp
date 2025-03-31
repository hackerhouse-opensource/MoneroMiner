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
bool loadConfig();

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
                // Get current job
                Job* currentJob = nullptr;
                {
                    std::lock_guard<std::mutex> lock(PoolClient::jobMutex);
                    if (!PoolClient::jobQueue.empty()) {
                        currentJob = &PoolClient::jobQueue.front();
                    }
                }

                if (!currentJob) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(100));
                    continue;
                }

                // Convert hex blob to bytes
                std::vector<uint8_t> input = currentJob->getBlobBytes();

                // Calculate hash
                if (data->calculateHash(input, currentJob->getNonce())) {
                    // Share was found and submitted
                    data->submitShare(RandomXManager::getLastHash());
                }

                // Increment nonce
                currentJob->incrementNonce();
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

bool loadConfig() {
    try {
        // Try to load from config.json if it exists
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
        // If no config file exists or it's invalid, use default values
        // The default values are already set in the Config constructor
        return true;
    }
    catch (const std::exception& e) {
        threadSafePrint("Error loading config: " + std::string(e.what()), true);
        return false;
    }
}

int main(int argc, char* argv[]) {
    // Check for --help first, regardless of position
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            printHelp();
            return 0;
        }
    }

    // Initialize Winsock
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        std::cerr << "Failed to initialize Winsock" << std::endl;
        return 1;
    }

    // Load configuration
    if (!loadConfig()) {
        std::cerr << "Failed to load configuration" << std::endl;
        WSACleanup();
        return 1;
    }

    // Parse command line arguments
    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        if (arg == "--debug") {
            config.debugMode = true;
        }
        else if (arg == "--threads" && i + 1 < argc) {
            config.numThreads = std::stoi(argv[++i]);
        }
        else if (arg == "--pool" && i + 1 < argc) {
            config.poolAddress = argv[++i];
        }
        else if (arg == "--wallet" && i + 1 < argc) {
            config.walletAddress = argv[++i];
        }
        else if (arg == "--worker" && i + 1 < argc) {
            config.workerName = argv[++i];
        }
        else if (arg == "--user-agent" && i + 1 < argc) {
            config.userAgent = argv[++i];
        }
        else if (arg == "--log-file" && i + 1 < argc) {
            config.logFileName = argv[++i];
            config.useLogFile = true;
        }
        else if (arg != "--help" && arg != "-h") {
            std::cerr << "Unknown argument: " << arg << std::endl;
            printHelp();
            WSACleanup();
            return 1;
        }
    }

    // Print current configuration
    config.printConfig();

    // Connect to pool
    if (!PoolClient::connect(config.poolAddress, std::to_string(config.poolPort))) {
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