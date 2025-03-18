#include "PoolClient.h"
#include "Utils.h"
#include "Constants.h"
#include "RandomXManager.h"
#include <sstream>
#include <cstring>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")

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

    // Forward declarations
    bool sendRequest(const std::string& request);
    void processNewJob(const char* response);

    bool initialize() {
        WSADATA wsaData;
        int result = WSAStartup(MAKEWORD(2, 2), &wsaData);
        if (result != 0) {
            threadSafePrint("WSAStartup failed: " + std::to_string(result));
            return false;
        }
        return true;
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

        poolSocket = socket(result->ai_family, result->ai_socktype, result->ai_protocol);
        if (poolSocket == INVALID_SOCKET) {
            threadSafePrint("socket failed: " + std::to_string(WSAGetLastError()));
            freeaddrinfo(result);
            return false;
        }

        status = ::connect(poolSocket, result->ai_addr, static_cast<int>(result->ai_addrlen));
        if (status == SOCKET_ERROR) {
            threadSafePrint("connect failed: " + std::to_string(WSAGetLastError()));
            closesocket(poolSocket);
            poolSocket = INVALID_SOCKET;
            freeaddrinfo(result);
            return false;
        }

        freeaddrinfo(result);
        return true;
    }

    bool login(const std::string& walletAddress, const std::string& password, 
               const std::string& workerName, const std::string& userAgent) {
        std::string loginPayload = "{\"id\":1,\"jsonrpc\":\"2.0\",\"method\":\"login\",\"params\":{\"agent\":\"" + 
                                  userAgent + "\",\"login\":\"" + walletAddress + 
                                  "\",\"pass\":\"" + password + "\",\"worker\":\"" + workerName + "\"}}";
        
        if (!sendRequest(loginPayload)) {
            threadSafePrint("Failed to send login request");
            return false;
        }

        // Wait for login response
        char response[4096];
        int bytesReceived = recv(poolSocket, response, sizeof(response) - 1, 0);
        if (bytesReceived <= 0) {
            threadSafePrint("Failed to receive login response");
            return false;
        }
        response[bytesReceived] = '\0';

        // Clean up response
        std::string cleanResponse = response;
        while (!cleanResponse.empty() && (cleanResponse.back() == '\n' || cleanResponse.back() == '\r')) {
            cleanResponse.pop_back();
        }
        
        threadSafePrint("Received response: " + cleanResponse);

        // Parse login response
        picojson::value v;
        std::string err = picojson::parse(v, cleanResponse);
        if (!err.empty()) {
            threadSafePrint("JSON parse error: " + err);
            return false;
        }

        if (!v.is<picojson::object>()) {
            threadSafePrint("Invalid JSON response: not an object");
            return false;
        }

        const picojson::object& obj = v.get<picojson::object>();
        if (obj.count("result")) {
            const picojson::object& result = obj.at("result").get<picojson::object>();
            if (result.count("id")) {
                sessionId = result.at("id").get<std::string>();
                threadSafePrint("Session ID: " + sessionId);
            }
            if (result.count("job")) {
                const picojson::object& job = result.at("job").get<picojson::object>();
                if (job.count("blob") && job.count("job_id") && job.count("target") && 
                    job.count("height") && job.count("seed_hash")) {
                    std::string blob = job.at("blob").get<std::string>();
                    std::string jobId = job.at("job_id").get<std::string>();
                    std::string target = job.at("target").get<std::string>();
                    uint64_t height = static_cast<uint64_t>(job.at("height").get<double>());
                    std::string seedHash = job.at("seed_hash").get<std::string>();
                    
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

        threadSafePrint("Login failed: " + (obj.count("error") ? obj.at("error").get<std::string>() : "Unknown error"));
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
        
        // Flush the socket
        if (shutdown(poolSocket, SD_SEND) == SOCKET_ERROR) {
            threadSafePrint("shutdown failed: " + std::to_string(WSAGetLastError()));
            return false;
        }
        
        return true;
    }

    void listenForNewJobs(SOCKET socket) {
        char buffer[4096];
        std::string messageBuffer;
        
        while (!shouldStop) {
            int bytesReceived = recv(socket, buffer, sizeof(buffer) - 1, 0);
            if (bytesReceived <= 0) {
                if (bytesReceived == 0) {
                    threadSafePrint("Connection closed by pool");
                } else {
                    threadSafePrint("Error receiving data from pool");
                }
                break;
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

                threadSafePrint("Received from pool: " + message);
                processNewJob(message.c_str());
            }
        }
        threadSafePrint("Job listener thread stopped");
    }

    void processNewJob(const char* response) {
        // Clean up the response by removing trailing newlines and carriage returns
        std::string cleanResponse = response;
        while (!cleanResponse.empty() && (cleanResponse.back() == '\n' || cleanResponse.back() == '\r')) {
            cleanResponse.pop_back();
        }
        
        if (cleanResponse.empty()) {
            threadSafePrint("Received empty response from pool", true);
            return;
        }
        
        picojson::value v;
        std::string err = picojson::parse(v, cleanResponse);
        if (!err.empty()) {
            threadSafePrint("Failed to parse JSON: " + err);
            return;
        }
        
        if (!v.is<picojson::object>()) {
            threadSafePrint("Invalid JSON response: not an object");
            return;
        }
        
        // Handle login response
        if (v.contains("result") && v.get("result").is<picojson::object>()) {
            const auto& result = v.get("result");
            if (result.contains("job") && result.get("job").is<picojson::object>()) {
                const auto& job = result.get("job");
                if (!job.contains("job_id") || !job.contains("blob") || !job.contains("target") || 
                    !job.contains("height") || !job.contains("seed_hash")) {
                    threadSafePrint("Invalid job format in login response");
                    return;
                }
                
                std::string jobId = job.get("job_id").to_str();
                std::string blob = job.get("blob").to_str();
                std::string target = job.get("target").to_str();
                uint64_t height = static_cast<uint64_t>(job.get("height").get<double>());
                std::string seedHash = job.get("seed_hash").to_str();
                
                // Format target to ensure it's 8 hex characters
                if (target.length() > 2 && target.substr(0, 2) == "0x") {
                    target = target.substr(2);
                }
                if (target.length() != 8) {
                    threadSafePrint("Invalid target format: " + target, true);
                    return;
                }
                
                // Queue the job
                {
                    std::lock_guard<std::mutex> lock(jobMutex);
                    Job newJob;
                    newJob.setId(jobId);
                    newJob.setBlob(hexToBytes(blob));
                    newJob.setTarget(target);
                    newJob.setHeight(height);
                    newJob.setSeedHash(seedHash);
                    jobQueue.push(newJob);
                }
                jobAvailable.notify_one();
                
                // Handle seed hash change
                handleSeedHashChange(seedHash);
            }
        }
        // Handle new job notification
        else if (v.contains("method") && v.get("method").to_str() == "job" && 
                 v.contains("params") && v.get("params").is<picojson::object>()) {
            const auto& params = v.get("params");
            if (params.contains("job") && params.get("job").is<picojson::object>()) {
                const auto& job = params.get("job");
                if (!job.contains("job_id") || !job.contains("blob") || !job.contains("target") || 
                    !job.contains("height") || !job.contains("seed_hash")) {
                    threadSafePrint("Invalid job format in notification");
                    return;
                }
                
                std::string jobId = job.get("job_id").to_str();
                std::string blob = job.get("blob").to_str();
                std::string target = job.get("target").to_str();
                uint64_t height = static_cast<uint64_t>(job.get("height").get<double>());
                std::string seedHash = job.get("seed_hash").to_str();
                
                // Format target to ensure it's 8 hex characters
                if (target.length() > 2 && target.substr(0, 2) == "0x") {
                    target = target.substr(2);
                }
                if (target.length() != 8) {
                    threadSafePrint("Invalid target format: " + target, true);
                    return;
                }
                
                // Queue the job
                {
                    std::lock_guard<std::mutex> lock(jobMutex);
                    Job newJob;
                    newJob.setId(jobId);
                    newJob.setBlob(hexToBytes(blob));
                    newJob.setTarget(target);
                    newJob.setHeight(height);
                    newJob.setSeedHash(seedHash);
                    jobQueue.push(newJob);
                }
                jobAvailable.notify_one();
                
                // Handle seed hash change
                handleSeedHashChange(seedHash);
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
} 