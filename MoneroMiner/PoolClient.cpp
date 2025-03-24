#include "PoolClient.h"
#include "Config.h"
#include "Utils.h"
#include "Constants.h"
#include "RandomXManager.h"
#include "MiningThreadData.h"
#include "HashValidation.h"
#include <sstream>
#include <cstring>
#include <ws2tcpip.h>
#include "picojson.h"
#pragma comment(lib, "ws2_32.lib")

using namespace picojson;

namespace PoolClient {
    // Static member definitions
    SOCKET poolSocket = INVALID_SOCKET;
    std::atomic<bool> shouldStop(false);
    std::queue<Job> jobQueue;
    std::mutex jobMutex;
    std::condition_variable jobAvailable;
    std::string currentSeedHash;
    std::string sessionId;
    std::string currentTargetHex;
    std::vector<std::shared_ptr<MiningThreadData>> threadData;

    // Forward declarations
    bool sendRequest(const std::string& request);
    void processNewJob(const picojson::object& jobObj);

    std::string receiveData(SOCKET socket) {
        if (socket == INVALID_SOCKET) {
            threadSafePrint("Invalid socket");
            return "";
        }

        char buffer[4096];
        std::string messageBuffer;
        
        // Wait for data with timeout
        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(socket, &readfds);
        struct timeval tv = { 1, 0 }; // 1 second timeout
        
        int status = select(0, &readfds, nullptr, nullptr, &tv);
        if (status <= 0) {
            if (status == 0) {
                // Timeout - no data available
                return "";
            }
            if (WSAGetLastError() == WSAEWOULDBLOCK) {
                // No data available in non-blocking mode
                return "";
            }
            threadSafePrint("select failed: " + std::to_string(WSAGetLastError()));
            return "";
        }

        int bytesReceived = recv(socket, buffer, sizeof(buffer) - 1, 0);
        if (bytesReceived <= 0) {
            if (bytesReceived == 0) {
                threadSafePrint("Connection closed by pool");
            } else if (WSAGetLastError() == WSAEWOULDBLOCK) {
                // No data available in non-blocking mode
                return "";
            } else {
                threadSafePrint("Error receiving data from pool: " + std::to_string(WSAGetLastError()));
            }
            return "";
        }
        buffer[bytesReceived] = '\0';
        
        // Add received data to message buffer
        messageBuffer += buffer;

        // Process complete messages (separated by newlines)
        size_t pos = 0;
        while ((pos = messageBuffer.find('\n')) != std::string::npos) {
            std::string message = messageBuffer.substr(0, pos);
            messageBuffer = messageBuffer.substr(pos + 1);

            // Skip empty messages
            if (message.empty()) {
                continue;
            }

            // Remove any trailing carriage return
            if (!message.empty() && message.back() == '\r') {
                message.pop_back();
            }

            return message;
        }
        
        return "";
    }

    bool initialize() {
        WSADATA wsaData;
        return WSAStartup(MAKEWORD(2, 2), &wsaData) == 0;
    }

    bool connect(const std::string& address, const std::string& port) {
        struct addrinfo hints = {}, *result = nullptr;
        hints.ai_family = AF_UNSPEC;
        hints.ai_socktype = SOCK_STREAM;
        hints.ai_protocol = IPPROTO_TCP;

        int status = getaddrinfo(address.c_str(), port.c_str(), &hints, &result);
        if (status != 0) {
            char errorMsg[256];
            strcpy_s(errorMsg, gai_strerrorA(status));
            threadSafePrint("getaddrinfo failed: " + std::string(errorMsg));
            return false;
        }

        // Try each address until we connect
        for (struct addrinfo* ptr = result; ptr != nullptr; ptr = ptr->ai_next) {
            poolSocket = socket(ptr->ai_family, ptr->ai_socktype, ptr->ai_protocol);
            if (poolSocket == INVALID_SOCKET) {
                threadSafePrint("socket failed: " + std::to_string(WSAGetLastError()));
                continue;
            }

            // Set socket to non-blocking mode
            u_long mode = 1;
            if (ioctlsocket(poolSocket, FIONBIO, &mode) == SOCKET_ERROR) {
                threadSafePrint("Failed to set non-blocking mode: " + std::to_string(WSAGetLastError()));
                closesocket(poolSocket);
                poolSocket = INVALID_SOCKET;
                continue;
            }

            status = ::connect(poolSocket, ptr->ai_addr, static_cast<int>(ptr->ai_addrlen));
            if (status == SOCKET_ERROR) {
                if (WSAGetLastError() == WSAEWOULDBLOCK) {
                    // Wait for connection to complete
                    fd_set writefds;
                    FD_ZERO(&writefds);
                    FD_SET(poolSocket, &writefds);
                    struct timeval tv = { 5, 0 }; // 5 second timeout
                    
                    status = select(0, nullptr, &writefds, nullptr, &tv);
                    if (status > 0) {
                        // Connection successful
                        freeaddrinfo(result);
                        return true;
                    }
                }
                threadSafePrint("connect failed: " + std::to_string(WSAGetLastError()));
                closesocket(poolSocket);
                poolSocket = INVALID_SOCKET;
                continue;
            }

            // Connection successful
            freeaddrinfo(result);
            return true;
        }

        freeaddrinfo(result);
        return false;
    }

    bool login(const std::string& walletAddress, const std::string& password, 
               const std::string& workerName, const std::string& userAgent) {
        if (poolSocket == INVALID_SOCKET) {
            threadSafePrint("Not connected to pool");
            return false;
        }

        std::string loginPayload = "{\"id\":1,\"jsonrpc\":\"2.0\",\"method\":\"login\",\"params\":{\"agent\":\"" + 
                                  userAgent + "\",\"login\":\"" + walletAddress + 
                                  "\",\"pass\":\"" + password + "\",\"worker\":\"" + workerName + "\"}}";
        
        threadSafePrint("Sending login request: " + loginPayload);
        
        if (!sendRequest(loginPayload)) {
            threadSafePrint("Failed to send login request");
            return false;
        }

        // Wait for login response with timeout
        std::string response;
        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(poolSocket, &readfds);
        struct timeval tv = { 10, 0 }; // 10 second timeout
        
        int status = select(0, &readfds, nullptr, nullptr, &tv);
        if (status <= 0) {
            threadSafePrint("Timeout waiting for login response");
            return false;
        }

        char buffer[4096];
        int bytesReceived = recv(poolSocket, buffer, sizeof(buffer) - 1, 0);
        if (bytesReceived <= 0) {
            threadSafePrint("Failed to receive login response: " + std::to_string(WSAGetLastError()));
            return false;
        }
        buffer[bytesReceived] = '\0';

        // Clean up response
        std::string cleanResponse = buffer;
        while (!cleanResponse.empty() && (cleanResponse.back() == '\n' || cleanResponse.back() == '\r')) {
            cleanResponse.pop_back();
        }
        
        threadSafePrint("Received response: " + cleanResponse);

        // Parse login response
        value v;
        std::string err = parse(v, cleanResponse);
        if (!err.empty()) {
            threadSafePrint("JSON parse error: " + err);
            return false;
        }

        if (!v.is<object>()) {
            threadSafePrint("Invalid JSON response: not an object");
            return false;
        }

        const object& obj = v.get<object>();
        if (obj.count("result")) {
            const object& result = obj.at("result").get<object>();
            if (result.count("id")) {
                sessionId = result.at("id").to_str();
                threadSafePrint("Session ID: " + sessionId);
            }
            if (result.count("job")) {
                const object& job = result.at("job").get<object>();
                if (job.count("blob") && job.count("job_id") && job.count("target") && 
                    job.count("height") && job.count("seed_hash")) {
                    std::string blob = job.at("blob").to_str();
                    std::string jobId = job.at("job_id").to_str();
                    std::string target = job.at("target").to_str();
                    uint64_t height = static_cast<uint64_t>(job.at("height").get<double>());
                    std::string seedHash = job.at("seed_hash").to_str();
                    
                    threadSafePrint("New job details:");
                    threadSafePrint("  Height: " + std::to_string(height));
                    threadSafePrint("  Job ID: " + jobId);
                    threadSafePrint("  Target: 0x" + target);
                    threadSafePrint("  Blob: " + blob);
                    threadSafePrint("  Seed Hash: " + seedHash);
                    threadSafePrint("  Difficulty: " + std::to_string(static_cast<double>(0xFFFFFFFFFFFFFFFF) / std::stoull(target, nullptr, 16)));

                    // Add job to queue
                    Job newJob;
                    newJob.setId(jobId);
                    newJob.setBlob(hexToBytes(blob));
                    newJob.setTarget(target);
                    newJob.setHeight(height);
                    newJob.setSeedHash(seedHash);
                    {
                        std::lock_guard<std::mutex> lock(jobMutex);
                        jobQueue.push(newJob);
                        jobAvailable.notify_one();
                    }
                }
            }
            return true;
        }

        threadSafePrint("Login failed: " + (obj.count("error") ? obj.at("error").to_str() : "Unknown error"));
        return false;
    }

    void cleanup() {
        if (poolSocket != INVALID_SOCKET) {
            closesocket(poolSocket);
            poolSocket = INVALID_SOCKET;
        }
        WSACleanup();
    }

    bool sendRequest(const std::string& request) {
        if (poolSocket == INVALID_SOCKET) {
            return false;
        }

        // Add newline character to the request
        std::string requestWithNewline = request + "\n";
        
        int result = send(poolSocket, requestWithNewline.c_str(), static_cast<int>(requestWithNewline.length()), 0);
        if (result == SOCKET_ERROR) {
            threadSafePrint("send failed: " + std::to_string(WSAGetLastError()));
            return false;
        }
        
        return true;
    }

    void listenForNewJobs(SOCKET sock) {
        char buffer[4096];
        std::string response;

        while (!shouldStop) {
            // Set up select for non-blocking socket read
            fd_set readfds;
            FD_ZERO(&readfds);
            FD_SET(sock, &readfds);
            struct timeval tv = { 1, 0 }; // 1 second timeout

            int status = select(0, &readfds, nullptr, nullptr, &tv);
            if (status == SOCKET_ERROR) {
                threadSafePrint("Select error in job listener: " + std::to_string(WSAGetLastError()), true);
                break;
            }
            if (status == 0) {
                // Timeout - no data available, continue waiting
                continue;
            }

            int bytesReceived = recv(sock, buffer, sizeof(buffer) - 1, 0);
            if (bytesReceived == SOCKET_ERROR) {
                if (WSAGetLastError() == WSAEWOULDBLOCK) {
                    // No data available in non-blocking mode, continue waiting
                    continue;
                }
                threadSafePrint("Socket error in job listener: " + std::to_string(WSAGetLastError()), true);
                break;
            }
            if (bytesReceived == 0) {
                threadSafePrint("Connection closed by server", true);
                break;
            }

            buffer[bytesReceived] = '\0';
            response = buffer;

            threadSafePrint("Received response: " + response, true);

            picojson::value v;
            std::string err = picojson::parse(v, response);
            if (!err.empty()) {
                threadSafePrint("Failed to parse response: " + err, true);
                continue;
            }

            if (!v.is<picojson::object>()) {
                threadSafePrint("Invalid response format", true);
                continue;
            }

            const picojson::object& obj = v.get<picojson::object>();
            
            // Handle login response with job
            if (obj.find("result") != obj.end() && obj.at("result").is<picojson::object>()) {
                const picojson::object& result = obj.at("result").get<picojson::object>();
                if (result.find("job") != result.end() && result.at("job").is<picojson::object>()) {
                    processNewJob(result.at("job").get<picojson::object>());
                }
            }
            // Handle new job notification
            else if (obj.find("method") != obj.end() && obj.at("method").get<std::string>() == "job") {
                const picojson::value& params = obj.at("params");
                if (params.is<picojson::object>()) {
                    const picojson::object& jobObj = params.get<picojson::object>();
                    processNewJob(jobObj);
                }
            }
        }

        threadSafePrint("Job listener thread stopping...", true);
    }

    void processNewJob(const picojson::object& jobObj) {
        if (!jobObj.count("job_id") || !jobObj.count("blob") || !jobObj.count("target") || 
            !jobObj.count("height") || !jobObj.count("seed_hash")) {
            threadSafePrint("Invalid job format");
            return;
        }
        
        std::string jobId = jobObj.at("job_id").to_str();
        std::string blob = jobObj.at("blob").to_str();
        std::string target = jobObj.at("target").to_str();
        uint64_t height = static_cast<uint64_t>(jobObj.at("height").get<double>());
        std::string seedHash = jobObj.at("seed_hash").to_str();
        
        // Format target to ensure it's 8 hex characters
        if (target.length() > 2 && target.substr(0, 2) == "0x") {
            target = target.substr(2);
        }
        if (target.length() != 8) {
            threadSafePrint("Invalid target format: " + target, true);
            return;
        }
        
        if (debugMode) {
            threadSafePrint("Processing new job with seed hash: " + seedHash, true);
        }

        // Initialize RandomX with the new seed hash
        if (!RandomXManager::initialize(seedHash)) {
            threadSafePrint("Failed to initialize RandomX with seed hash: " + seedHash, true);
            return;
        }

        if (debugMode) {
            threadSafePrint("RandomX initialized successfully with seed hash: " + seedHash, true);
        }
        
        // Queue the job
        {
            std::lock_guard<std::mutex> lock(jobMutex);
            std::queue<Job> empty;
            std::swap(jobQueue, empty);
            Job newJob;
            newJob.setId(jobId);
            newJob.setBlob(hexToBytes(blob));
            newJob.setTarget(target);
            newJob.setHeight(height);
            newJob.setSeedHash(seedHash);
            jobQueue.push(newJob);
            jobAvailable.notify_all();
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
        for (auto& threadData : threadData) {
            if (threadData) {
                threadData->updateJob(jobQueue.front());
                if (debugMode) {
                    threadSafePrint("Updated job for thread " + std::to_string(threadData->getThreadId()), true);
                }
            }
        }
    }

    bool submitShare(const std::string& jobId, const std::string& nonce,
                    const std::string& hash, const std::string& algo) {
        std::stringstream ss;
        ss << "{\"method\":\"submit\",\"params\":{\"id\":\"" << sessionId
           << "\",\"job_id\":\"" << jobId << "\",\"nonce\":\"" << nonce
           << "\",\"result\":\"" << hash << "\"},\"id\":2,\"algo\":\"" << algo << "\"}";

        std::string request = ss.str();
        threadSafePrint("Submitting validated share:");
        threadSafePrint("  Job ID: " + jobId);
        threadSafePrint("  Nonce: 0x" + nonce);
        threadSafePrint("  Hash: " + hash);
        threadSafePrint("  Target: " + currentTargetHex);
        threadSafePrint("  Algorithm: " + algo);
        threadSafePrint("Pool send: " + request);

        return sendRequest(request);
    }

    void handleSeedHashChange(const std::string& newSeedHash) {
        if (newSeedHash.empty()) {
            threadSafePrint("Warning: Received empty seed hash", true);
            return;
        }
        
        if (currentSeedHash != newSeedHash) {
            threadSafePrint("Seed hash changed from " + (currentSeedHash.empty() ? "none" : currentSeedHash) + " to " + newSeedHash, true);
            currentSeedHash = newSeedHash;
            RandomXManager::handleSeedHashChange(newSeedHash);
        }
    }

    void handleLoginResponse(const std::string& response) {
        picojson::value v;
        std::string err = picojson::parse(v, response);
        if (!err.empty()) {
            threadSafePrint("Failed to parse login response: " + err, true);
            return;
        }

        if (!v.is<picojson::object>()) {
            threadSafePrint("Invalid login response format", true);
            return;
        }

        const picojson::object& obj = v.get<picojson::object>();
        if (obj.find("result") != obj.end()) {
            const picojson::object& result = obj.at("result").get<picojson::object>();
            if (result.find("id") != result.end()) {
                sessionId = result.at("id").get<std::string>();
                threadSafePrint("Login successful. Session ID: " + sessionId, true);
                
                // Get seed hash from pool response
                if (result.find("seed_hash") != result.end()) {
                    std::string seedHash = result.at("seed_hash").get<std::string>();
                    threadSafePrint("Received seed hash from pool: " + seedHash, true);
                    
                    // Initialize RandomX with pool's seed hash
                    if (!RandomXManager::initialize(seedHash)) {
                        threadSafePrint("Failed to initialize RandomX with pool seed hash", true);
                        return;
                    }
                    threadSafePrint("RandomX initialized with pool seed hash", true);
                } else {
                    threadSafePrint("No seed hash received from pool", true);
                }
            }
        } else if (obj.find("error") != obj.end()) {
            const picojson::object& error = obj.at("error").get<picojson::object>();
            threadSafePrint("Login failed: " + error.at("message").get<std::string>(), true);
        }
    }
} 