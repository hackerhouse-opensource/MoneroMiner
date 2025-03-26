#include "PoolClient.h"
#include "Globals.h"
#include "Config.h"
#include "Utils.h"
#include "Constants.h"
#include "RandomXManager.h"
#include "MiningThreadData.h"
#include "HashValidation.h"
#include "Job.h"
#include <iostream>
#include <sstream>
#include <iomanip>
#include <chrono>
#include <thread>
#include <cstring>
#include <ws2tcpip.h>
#include "picojson.h"
#pragma comment(lib, "ws2_32.lib")

using namespace picojson;

namespace PoolClient {
    // Static member definitions
    SOCKET poolSocket = INVALID_SOCKET;
    std::mutex jobMutex;
    std::queue<Job> jobQueue;
    std::condition_variable jobAvailable;
    std::condition_variable jobQueueCondition;
    std::atomic<bool> shouldStop(false);
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
        // Reset state
        poolSocket = INVALID_SOCKET;
        shouldStop = false;
        currentSeedHash.clear();
        sessionId.clear();
        currentTargetHex.clear();
        
        // Initialize Winsock
        WSADATA wsaData;
        if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
            threadSafePrint("Failed to initialize Winsock");
            return false;
        }
        threadSafePrint("Winsock initialized successfully", true);
        return true;
    }

    bool connect(const std::string& address, const std::string& port) {
        threadSafePrint("Attempting to connect to " + address + ":" + port, true);
        
        // Close existing socket if any
        if (poolSocket != INVALID_SOCKET) {
            closesocket(poolSocket);
            poolSocket = INVALID_SOCKET;
        }

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

        bool connected = false;
        // Try each address until we connect
        for (struct addrinfo* ptr = result; ptr != nullptr; ptr = ptr->ai_next) {
            // Create socket
            poolSocket = socket(ptr->ai_family, ptr->ai_socktype, ptr->ai_protocol);
            if (poolSocket == INVALID_SOCKET) {
                threadSafePrint("socket failed: " + std::to_string(WSAGetLastError()));
                continue;
            }

            // Set socket options
            int optval = 1;
            if (setsockopt(poolSocket, SOL_SOCKET, SO_KEEPALIVE, (char*)&optval, sizeof(optval)) == SOCKET_ERROR) {
                threadSafePrint("setsockopt SO_KEEPALIVE failed: " + std::to_string(WSAGetLastError()));
                closesocket(poolSocket);
                poolSocket = INVALID_SOCKET;
                continue;
            }

            // Set TCP_NODELAY to disable Nagle's algorithm
            if (setsockopt(poolSocket, IPPROTO_TCP, TCP_NODELAY, (char*)&optval, sizeof(optval)) == SOCKET_ERROR) {
                threadSafePrint("setsockopt TCP_NODELAY failed: " + std::to_string(WSAGetLastError()));
                closesocket(poolSocket);
                poolSocket = INVALID_SOCKET;
                continue;
            }

            // Connect to server
            status = ::connect(poolSocket, ptr->ai_addr, static_cast<int>(ptr->ai_addrlen));
            if (status == SOCKET_ERROR) {
                int error = WSAGetLastError();
                threadSafePrint("connect failed: " + std::to_string(error));
                closesocket(poolSocket);
                poolSocket = INVALID_SOCKET;
                continue;
            }

            // Verify socket is valid after connection
            if (poolSocket == INVALID_SOCKET) {
                threadSafePrint("Socket became invalid after connection");
                continue;
            }

            // Connection successful
            connected = true;
            threadSafePrint("Successfully connected to pool", true);
            break;
        }

        freeaddrinfo(result);
        
        if (!connected) {
            threadSafePrint("Failed to connect to any pool address", true);
            return false;
        }
        
        return true;
    }

    bool login(const std::string& wallet, const std::string& password, 
               const std::string& worker, const std::string& userAgent) {
        // Verify socket is valid
        if (poolSocket == INVALID_SOCKET) {
            threadSafePrint("Cannot login: Invalid socket", true);
            return false;
        }

        try {
            // Create login request
            picojson::object loginObj;
            loginObj["id"] = picojson::value(1.0);
            loginObj["jsonrpc"] = picojson::value("2.0");
            loginObj["method"] = picojson::value("login");
            
            picojson::object params;
            params["agent"] = picojson::value(userAgent);
            params["login"] = picojson::value(wallet);
            params["pass"] = picojson::value(password);
            params["worker"] = picojson::value(worker);
            
            loginObj["params"] = picojson::value(params);
            
            std::string request = picojson::value(loginObj).serialize();
            threadSafePrint("Sending login request: " + request, true);
            
            // Send request
            std::string fullRequest = request + "\n";
            int result = send(poolSocket, fullRequest.c_str(), static_cast<int>(fullRequest.length()), 0);
            if (result == SOCKET_ERROR) {
                int error = WSAGetLastError();
                threadSafePrint("Failed to send login request: " + std::to_string(error), true);
                return false;
            }

            // Receive response
            char buffer[4096];
            std::string response;
            bool complete = false;
            
            while (!complete) {
                int bytesReceived = recv(poolSocket, buffer, sizeof(buffer) - 1, 0);
                if (bytesReceived == SOCKET_ERROR) {
                    int error = WSAGetLastError();
                    threadSafePrint("Failed to receive login response: " + std::to_string(error), true);
                    return false;
                }
                if (bytesReceived == 0) {
                    threadSafePrint("Connection closed by server during login", true);
                    return false;
                }

                buffer[bytesReceived] = '\0';
                response += buffer;

                // Check if we have a complete JSON response
                try {
                    picojson::value v;
                    std::string err = picojson::parse(v, response);
                    if (err.empty()) {
                        complete = true;
                    }
                } catch (...) {
                    // Continue receiving if JSON is incomplete
                    continue;
                }
            }

            // Clean up response
            while (!response.empty() && (response.back() == '\n' || response.back() == '\r')) {
                response.pop_back();
            }

            if (response.empty()) {
                threadSafePrint("Empty login response received", true);
                return false;
            }

            threadSafePrint("Received login response: " + response, true);
            
            // Process login response
            if (!handleLoginResponse(response)) {
                return false;
            }
            
            // Verify we received a job
            {
                std::lock_guard<std::mutex> lock(jobMutex);
                if (jobQueue.empty()) {
                    threadSafePrint("No job received from login response", true);
                    return false;
                }
            }
            
            return true;
        }
        catch (const std::exception& e) {
            threadSafePrint("Login error: " + std::string(e.what()), true);
            return false;
        }
        catch (...) {
            threadSafePrint("Unknown error during login", true);
            return false;
        }
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
            threadSafePrint("Cannot send request: Invalid socket", true);
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

    void jobListener() {
        while (!shouldStop) {
            try {
                // Wait for data with timeout
                fd_set readSet;
                FD_ZERO(&readSet);
                FD_SET(poolSocket, &readSet);

                struct timeval timeout;
                timeout.tv_sec = 1;  // 1 second timeout
                timeout.tv_usec = 0;

                int result = select(0, &readSet, nullptr, nullptr, &timeout);
                if (result == 0) {
                    continue;  // Timeout, check shouldStop and continue
                }
                if (result == SOCKET_ERROR) {
                    threadSafePrint("Select error in job listener: " + std::to_string(WSAGetLastError()), true);
                    break;
                }

                // Receive response
                std::string response;
                char buffer[4096];
                int totalBytes = 0;
                
                while (true) {
                    int bytesReceived = recv(poolSocket, buffer, sizeof(buffer) - 1, 0);
                    if (bytesReceived == SOCKET_ERROR) {
                        int error = WSAGetLastError();
                        threadSafePrint("Failed to receive data in job listener: " + std::to_string(error), true);
                        break;
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

                if (response.empty()) {
                    continue;
                }

                // Parse response
                picojson::value v;
                std::string err = picojson::parse(v, response);
                if (!err.empty()) {
                    threadSafePrint("Failed to parse job listener response: " + err, true);
                    continue;
                }

                if (!v.is<picojson::object>()) {
                    threadSafePrint("Invalid response format in job listener", true);
                    continue;
                }

                const picojson::object& obj = v.get<picojson::object>();
                
                // Handle job update
                if (obj.find("method") != obj.end() && obj.at("method").get<std::string>() == "job") {
                    if (obj.find("params") == obj.end()) {
                        threadSafePrint("No params in job update", true);
                        continue;
                    }

                    const picojson::value& params = obj.at("params");
                    if (!params.is<picojson::object>()) {
                        threadSafePrint("Invalid params format in job update", true);
                        continue;
                    }

                    const picojson::object& jobObj = params.get<picojson::object>();
                    processNewJob(jobObj);
                }
                // Handle login response
                else if (obj.find("result") != obj.end()) {
                    const picojson::value& result = obj.at("result");
                    if (result.is<picojson::object>()) {
                        const picojson::object& resultObj = result.get<picojson::object>();
                        if (resultObj.find("job") != resultObj.end()) {
                            const picojson::object& jobObj = resultObj.at("job").get<picojson::object>();
                            processNewJob(jobObj);
                        }
                    }
                }
            }
            catch (const std::exception& e) {
                threadSafePrint("Error in job listener: " + std::string(e.what()), true);
            }
        }
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
        uint32_t height = static_cast<uint32_t>(jobObj.at("height").get<double>());
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

    bool handleLoginResponse(const std::string& response) {
        try {
            picojson::value v;
            std::string err = picojson::parse(v, response);
            if (!err.empty()) {
                threadSafePrint("JSON parse error: " + err, true);
                return false;
            }

            if (!v.is<picojson::object>()) {
                threadSafePrint("Invalid response format", true);
                return false;
            }

            auto& result = v.get("result");
            if (!result.is<picojson::object>()) {
                threadSafePrint("Invalid result format", true);
                return false;
            }

            // Get session ID
            auto& id = result.get("id");
            if (!id.is<std::string>()) {
                threadSafePrint("Invalid session ID format", true);
                return false;
            }
            sessionId = id.get<std::string>();

            // Get job
            auto& job = result.get("job");
            if (!job.is<picojson::object>()) {
                threadSafePrint("Invalid job format", true);
                return false;
            }

            // Process the job
            processNewJob(job.get<picojson::object>());
            return true;
        }
        catch (const std::exception& e) {
            threadSafePrint("Error processing login response: " + std::string(e.what()), true);
            return false;
        }
    }

    std::string sendAndReceive(const std::string& payload) {
        if (poolSocket == INVALID_SOCKET) {
            threadSafePrint("Cannot send/receive: Invalid socket", true);
            return "";
        }

        // Add newline to payload
        std::string fullPayload = payload + "\n";

        // Send the payload
        int bytesSent = send(poolSocket, fullPayload.c_str(), static_cast<int>(fullPayload.length()), 0);
        if (bytesSent == SOCKET_ERROR) {
            threadSafePrint("Failed to send data: " + std::to_string(WSAGetLastError()), true);
            return "";
        }

        // Receive response
        char buffer[4096];
        std::string response;
        bool complete = false;
        
        while (!complete) {
            int bytesReceived = recv(poolSocket, buffer, sizeof(buffer) - 1, 0);
            if (bytesReceived == SOCKET_ERROR) {
                threadSafePrint("Failed to receive data: " + std::to_string(WSAGetLastError()), true);
                return "";
            }
            if (bytesReceived == 0) {
                threadSafePrint("Connection closed by server", true);
                return "";
            }

            buffer[bytesReceived] = '\0';
            response += buffer;

            // Check if we have a complete JSON response
            try {
                picojson::value v;
                std::string err = picojson::parse(v, response);
                if (err.empty()) {
                    complete = true;
                }
            } catch (...) {
                // Continue receiving if JSON is incomplete
                continue;
            }
        }

        // Clean up response
        while (!response.empty() && (response.back() == '\n' || response.back() == '\r')) {
            response.pop_back();
        }

        if (response.empty()) {
            threadSafePrint("Empty response received", true);
            return "";
        }

        return response;
    }
} 