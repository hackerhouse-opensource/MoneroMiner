/**
 * MoneroMiner.cpp - Lightweight High Performance Monero (XMR) CPU Miner
 * 
 * Implementation file containing the core mining functionality.
 * 
 * Author: Hacker Fantastic (https://hacker.house)
 * License: Attribution-NonCommercial-NoDerivatives 4.0 International
 * https://creativecommons.org/licenses/by-nc-nd/4.0/
 */
 
#include "MoneroMiner.h"
#include "RandomXManager.h"
#include "Utils.h"
#include "Constants.h"
#include "PoolClient.h"
#include "HashValidation.h"
#include "MiningStats.h"
#include <csignal>
#include <chrono>
#include <iomanip>
#include <sstream>

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

void miningThread(MiningThreadData* data) {
    if (!data) {
        threadSafePrint("Error: Null thread data", true);
        return;
    }
    
    threadSafePrint("Thread " + std::to_string(data->getThreadId()) + " starting...", true);
    
    while (!shouldStop) {
        // Wait for a job
        threadSafePrint("Thread " + std::to_string(data->getThreadId()) + " waiting for job...", true);
        
        std::unique_lock<std::mutex> lock(jobMutex);
        jobQueueCV.wait(lock, []() { 
            bool hasJob = !jobQueue.empty();
            if (hasJob) {
                threadSafePrint("Job queue is not empty, waking up thread", true);
            }
            return hasJob || shouldStop; 
        });
        
        if (shouldStop) {
            threadSafePrint("Thread " + std::to_string(data->getThreadId()) + " stopping...", true);
            break;
        }
        
        // Get current job without removing it from queue
        Job currentJob = jobQueue.front();
        uint32_t currentJobId = std::stoul(currentJob.getJobId());
        lock.unlock();
        
        threadSafePrint("Thread " + std::to_string(data->getThreadId()) + 
                       " processing job ID: " + currentJob.getJobId(), true);
        
        // Initialize VM with the job's seed hash
        threadSafePrint("Thread " + std::to_string(data->getThreadId()) + " initializing VM...", true);
        if (!data->initializeVM()) {
            threadSafePrint("Failed to initialize VM for thread " + std::to_string(data->getThreadId()), true);
            continue;
        }
        threadSafePrint("Thread " + std::to_string(data->getThreadId()) + " VM initialized successfully", true);
        
        // Process the job
        uint64_t hashCount = 0;
        auto startTime = std::chrono::steady_clock::now();
        
        while (!shouldStop && currentJobId == activeJobId.load(std::memory_order_acquire)) {
            try {
                // Calculate hash for current nonce
                uint8_t hash[32];
                if (!data->calculateHash(currentJob.getBlob(), currentJob.getNonce(), hash)) {
                    threadSafePrint("Hash calculation failed for thread " + std::to_string(data->getThreadId()), true);
                    break;
                }
                
                hashCount++;
                if (hashCount % 1000 == 0) {
                    auto now = std::chrono::steady_clock::now();
                    auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - startTime).count();
                    if (elapsed > 0) {
                        double hashrate = static_cast<double>(hashCount) / elapsed;
                        threadSafePrint("Thread " + std::to_string(data->getThreadId()) + 
                                      " hashrate: " + std::to_string(hashrate) + " H/s", true);
                    }
                }
                
                // Convert hash to hex string
                std::string hashHex = HashValidation::hashToHex(hash, 32);
                
                // Check if hash meets target
                if (HashValidation::validateHash(hashHex, currentJob.getTarget())) {
                    threadSafePrint("Thread " + std::to_string(data->getThreadId()) + " found valid share!" +
                                  "\n  Job ID: " + currentJob.getJobId() +
                                  "\n  Nonce: 0x" + std::to_string(currentJob.getNonce()) +
                                  "\n  Hash: " + hashHex, true);
                    
                    // Submit share
                    bool accepted = false;
                    if (submitShare(currentJob.getJobId(), std::to_string(currentJob.getNonce()), hashHex, "rx/0")) {
                        handleShareResponse("", accepted);
                        if (accepted) {
                            acceptedShares++;
                            threadSafePrint("Thread " + std::to_string(data->getThreadId()) + " share accepted!", true);
                        } else {
                            rejectedShares++;
                            threadSafePrint("Thread " + std::to_string(data->getThreadId()) + " share rejected", true);
                        }
                    }
                }
                
                // Update nonce and hash count
                currentJob.incrementNonce();
                totalHashes++;
            } catch (const std::exception& e) {
                threadSafePrint("Exception in mining loop: " + std::string(e.what()), true);
                break;
            } catch (...) {
                threadSafePrint("Unknown exception in mining loop", true);
                break;
            }
        }
    }
}

void processNewJob(const picojson::object& jobObj) {
    try {
        std::string jobId = jobObj.at("job_id").get<std::string>();
        std::string blob = jobObj.at("blob").get<std::string>();
        std::string target = jobObj.at("target").get<std::string>();
        uint64_t height = static_cast<uint64_t>(jobObj.at("height").get<double>());
        std::string seedHash = jobObj.at("seed_hash").get<std::string>();

        threadSafePrint("Processing new job with seed hash: " + seedHash, true);

        // Create new job
        Job newJob;
        newJob.jobId = jobId;
        newJob.blobHex = blob;
        newJob.target = target;
        newJob.height = height;
        newJob.seedHash = seedHash;

        // Update active job ID and notify threads
        uint32_t jobIdNum = static_cast<uint32_t>(std::stoul(jobId));
        activeJobId.store(jobIdNum, std::memory_order_release);
        notifiedJobId.store(jobIdNum, std::memory_order_release);

        threadSafePrint("Updated active job ID to: " + std::to_string(jobIdNum), true);

        // Initialize RandomX with the new seed hash
        if (!RandomXManager::initialize(seedHash)) {
            threadSafePrint("Failed to initialize RandomX with seed hash: " + seedHash, true);
            return;
        }

        threadSafePrint("RandomX initialized successfully with seed hash: " + seedHash, true);

        // Clear existing jobs and add the new one
        {
            std::lock_guard<std::mutex> lock(jobMutex);
            std::queue<Job> empty;
            std::swap(jobQueue, empty);
            jobQueue.push(newJob);
            threadSafePrint("Added new job to queue, notifying threads...", true);
            jobQueueCV.notify_all();
        }

        // Print job details
        std::stringstream ss;
        ss << "\nNew job details:" << std::endl;
        ss << "  Height: " << height << std::endl;
        ss << "  Job ID: " << jobId << std::endl;
        ss << "  Target: 0x" << target << std::endl;
        ss << "  Blob: " << blob << std::endl;
        ss << "  Seed Hash: " << seedHash << std::endl;
        ss << "  Difficulty: " << HashValidation::getTargetDifficulty(target) << std::endl;
        threadSafePrint(ss.str(), true);

        // Notify all mining threads about the new job
        for (auto* data : threadData) {
            if (data) {
                data->updateJob(newJob);
                threadSafePrint("Updated job for thread " + std::to_string(data->getThreadId()), true);
            }
        }
    } catch (const std::exception& e) {
        threadSafePrint("Error processing new job: " + std::string(e.what()), true);
    } catch (...) {
        threadSafePrint("Unknown error processing new job", true);
    }
}

bool submitShare(const std::string& jobId, const std::string& nonce, const std::string& hash, const std::string& algo) {
    if (sessionId.empty()) {
        threadSafePrint("Cannot submit share: Not logged in", true);
        return false;
    }

    std::string payload = createSubmitPayload(sessionId, jobId, nonce, hash, algo);
    std::string response = sendAndReceive(globalSocket, payload);

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
    // Send the payload
    if (send(sock, payload.c_str(), static_cast<int>(payload.length()), 0) == SOCKET_ERROR) {
        threadSafePrint("Failed to send data: " + std::to_string(WSAGetLastError()), true);
        return "";
    }

    // Receive response
    char buffer[4096];
    int bytesReceived = recv(sock, buffer, sizeof(buffer) - 1, 0);
    if (bytesReceived == SOCKET_ERROR) {
        threadSafePrint("Failed to receive data: " + std::to_string(WSAGetLastError()), true);
        return "";
    }

    buffer[bytesReceived] = '\0';
    return std::string(buffer);
}

std::string createSubmitPayload(const std::string& sessionId, const std::string& jobId,
                              const std::string& nonceHex, const std::string& hashHex,
                              const std::string& algo) {
    picojson::object submitObj;
    submitObj["id"] = picojson::value(static_cast<double>(++jsonRpcId));
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

int main(int argc, char* argv[]) {
    // Set up signal handler
    signal(SIGINT, signalHandler);
    signal(SIGTERM, signalHandler);

    // Parse command line arguments
    if (!parseCommandLine(argc, argv)) {
        return 1;
    }

    // Open log file if enabled
    if (config.useLogFile) {
        logFile.open(config.logFileName, std::ios::app);
        if (!logFile.is_open()) {
            std::cerr << "Failed to open log file: " << config.logFileName << std::endl;
            return 1;
        }
    }

    printConfig();

    // Initialize Winsock
    if (!PoolClient::initialize()) {
        std::cerr << "Failed to initialize Winsock" << std::endl;
        return 1;
    }

    // Connect to pool first
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

    // Start job listener thread
    std::thread listenerThread(PoolClient::listenForNewJobs, PoolClient::getSocket());

    // Wait for first job to be received
    {
        std::unique_lock<std::mutex> jobLock(jobMutex);
        threadSafePrint("Waiting for first job...", true);
        
        // Wait for job with timeout
        auto startTime = std::chrono::steady_clock::now();
        while (jobQueue.empty() && !shouldStop) {
            if (jobQueueCV.wait_for(jobLock, std::chrono::seconds(1), []() { return !jobQueue.empty(); })) {
                break;
            }
            
            // Check if we've been waiting too long
            auto now = std::chrono::steady_clock::now();
            if (std::chrono::duration_cast<std::chrono::seconds>(now - startTime).count() > 30) {
                threadSafePrint("Timeout waiting for first job", true);
                shouldStop = true;
                break;
            }
        }
        
        if (shouldStop) {
            threadSafePrint("Shutting down while waiting for first job", true);
            listenerThread.join();
            return 1;
        }
        
        threadSafePrint("First job received, proceeding with initialization", true);
    }

    // Get the first job and initialize RandomX
    Job firstJob;
    {
        std::lock_guard<std::mutex> jobLock(jobMutex);
        firstJob = jobQueue.front();
        threadSafePrint("Retrieved first job from queue", true);
    }

    threadSafePrint("Received first job with seed hash: " + firstJob.getSeedHash(), true);

    // Initialize RandomX with the first job's seed hash
    if (!RandomXManager::initialize(firstJob.getSeedHash())) {
        std::cerr << "Failed to initialize RandomX with seed hash: " << firstJob.getSeedHash() << std::endl;
        PoolClient::cleanup();
        return 1;
    }

    threadSafePrint("RandomX initialized successfully with seed hash: " + firstJob.getSeedHash(), true);

    // Start mining threads after we have the first job and RandomX is initialized
    std::vector<std::shared_ptr<MiningThreadData>> threadDataVector;
    
    // Print system information before starting threads
    threadSafePrint("=== System Information ===", true);
    threadSafePrint("Hardware Concurrency: " + std::to_string(std::thread::hardware_concurrency()), true);
    threadSafePrint("Number of Threads: " + std::to_string(config.numThreads), true);
    threadSafePrint("Pool Address: " + config.poolAddress + ":" + config.poolPort, true);
    threadSafePrint("=========================", true);

    // Create and start mining threads
    for (int i = 0; i < config.numThreads; i++) {
        threadSafePrint("Creating mining thread " + std::to_string(i) + "...", true);
        auto data = std::make_shared<MiningThreadData>(i);
        threadDataVector.push_back(data);
        threadData.push_back(data.get());  // Update global threadData vector
        
        // Update thread with first job
        threadSafePrint("Updating thread " + std::to_string(i) + " with first job...", true);
        data->updateJob(firstJob);
        
        // Start the mining thread
        data->start();
        threadSafePrint("Started mining thread " + std::to_string(i), true);
    }

    // Start stats monitor thread
    std::thread statsThread(MiningStats::globalStatsMonitor);

    // Wait for all threads to complete
    for (auto& data : threadDataVector) {
        data->stop();
    }
    listenerThread.join();
    statsThread.join();

    // Cleanup
    PoolClient::cleanup();
    if (logFile.is_open()) {
        logFile.close();
    }

    return 0;
} 