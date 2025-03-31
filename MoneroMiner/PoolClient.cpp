#include "PoolClient.h"
#include "Globals.h"
#include "Config.h"
#include "Utils.h"
#include "Constants.h"
#include "RandomXManager.h"
#include "MiningThreadData.h"
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
    std::mutex socketMutex;
    std::mutex submitMutex;
    std::string poolId;

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
            // Verify socket is valid
            if (poolSocket == INVALID_SOCKET) {
                threadSafePrint("Pool connection lost, attempting to reconnect...", true);
                if (!connect(config.poolAddress, std::to_string(config.poolPort))) {
                    threadSafePrint("Failed to reconnect to pool", true);
                    std::this_thread::sleep_for(std::chrono::seconds(5));
                    continue;
                }
                // Re-login after reconnection
                if (!login(config.walletAddress, config.password, config.workerName, config.userAgent)) {
                    threadSafePrint("Failed to re-login to pool", true);
                    std::this_thread::sleep_for(std::chrono::seconds(5));
                    continue;
                }
            }

            std::string response = receiveData(poolSocket);
            if (response.empty()) {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                continue;
            }

            try {
                picojson::value v;
                std::string err = picojson::parse(v, response);
                if (!err.empty()) {
                    threadSafePrint("JSON parse error: " + err, true);
                    continue;
                }

                if (!v.is<picojson::object>()) {
                    threadSafePrint("Invalid JSON response format", true);
                    continue;
                }

                const picojson::object& obj = v.get<picojson::object>();
                if (obj.find("method") != obj.end()) {
                    const std::string& method = obj.at("method").get<std::string>();
                    if (method == "job") {
                        const picojson::object& jobObj = obj.at("params").get<picojson::object>();
                        processNewJob(jobObj);
                    }
                }
            }
            catch (const std::exception& e) {
                threadSafePrint("Error processing response: " + std::string(e.what()), true);
            }
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

            // Create new job
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
                    std::lock_guard<std::mutex> lock(jobMutex);
                    // Clear the queue
                    std::queue<Job> empty;
                    std::swap(jobQueue, empty);
                    // Add the new job
                    jobQueue.push(newJob);
                    
                    if (debugMode) {
                        threadSafePrint("Job queue updated with new job: " + jobId, true);
                        threadSafePrint("Queue size: " + std::to_string(jobQueue.size()), true);
                    }
                }

                // Set job information in RandomXManager
                RandomXManager::setJobInfo(height, jobId);
                
                // Set target for hash comparison
                RandomXManager::setTarget(target);
                
                // Print job details
                threadSafePrint("New job details:", true);
                threadSafePrint("  Height: " + std::to_string(height), true);
                threadSafePrint("  Job ID: " + jobId, true);
                threadSafePrint("  Target: 0x" + target, true);
                threadSafePrint("  Blob: " + blob, true);
                threadSafePrint("  Seed Hash: " + seedHash, true);
                
                // Calculate and print difficulty
                double difficulty = newJob.calculateDifficulty();
                threadSafePrint("  Difficulty: " + std::to_string(difficulty), true);

                // Update thread data with new job
                for (auto& data : threadData) {
                    if (data) {
                        data->updateJob(newJob);
                        if (debugMode) {
                            threadSafePrint("Updated thread " + std::to_string(data->getThreadId()) + 
                                " with new job: " + jobId, true);
                        }
                    }
                }

                // Notify all mining threads about the new job
                jobQueueCondition.notify_all();
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

    bool submitShare(const std::string& jobId, const std::string& nonce, 
                    const std::string& result, const std::string& algorithm) {
        std::lock_guard<std::mutex> lock(submitMutex);
        std::lock_guard<std::mutex> sockLock(socketMutex);

        if (poolSocket == INVALID_SOCKET) {
            threadSafePrint("Cannot submit share: Not connected to pool", true);
            return false;
        }

        // Construct JSON request
        std::stringstream ss;
        ss << "{\"id\":1,\"jsonrpc\":\"2.0\",\"method\":\"submit\",\"params\":{"
           << "\"id\":\"" << poolId << "\","
           << "\"job_id\":\"" << jobId << "\","
           << "\"nonce\":\"" << nonce << "\","
           << "\"result\":\"" << result << "\","
           << "\"algo\":\"" << algorithm << "\"}}";
        
        std::string request = ss.str();

        if (config.debugMode) {
            threadSafePrint("\nSubmitting share to pool:", true);
            threadSafePrint("  Pool ID: " + poolId, true);
            threadSafePrint("  Job ID: " + jobId, true);
            threadSafePrint("  Nonce: " + nonce, true);
            threadSafePrint("  Result: " + result, true);
            threadSafePrint("  Request: " + request, true);
        }

        // Send request with newline
        request += "\n";
        int bytesSent = send(poolSocket, request.c_str(), static_cast<int>(request.length()), 0);
        if (bytesSent == SOCKET_ERROR) {
            threadSafePrint("Failed to send share: " + std::to_string(WSAGetLastError()), true);
            return false;
        }

        // Receive response with timeout
        fd_set readSet;
        struct timeval timeout;
        FD_ZERO(&readSet);
        FD_SET(poolSocket, &readSet);
        timeout.tv_sec = 10;
        timeout.tv_usec = 0;

        int selectResult = select(0, &readSet, nullptr, nullptr, &timeout);
        if (selectResult <= 0) {
            threadSafePrint("Timeout or error waiting for pool response", true);
            return false;
        }

        char buffer[4096];
        int bytesReceived = recv(poolSocket, buffer, sizeof(buffer) - 1, 0);
        if (bytesReceived <= 0) {
            threadSafePrint("Failed to receive pool response", true);
            return false;
        }

        buffer[bytesReceived] = '\0';
        std::string response(buffer);

        if (config.debugMode) {
            threadSafePrint("Pool response: " + response, true);
        }

        // Parse response
        try {
            picojson::value v;
            std::string err = picojson::parse(v, response);
            if (err.empty() && v.is<picojson::object>()) {
                const picojson::object& obj = v.get<picojson::object>();
                if (obj.find("result") != obj.end() && 
                    obj.at("result").is<picojson::object>()) {
                    const picojson::object& result = obj.at("result").get<picojson::object>();
                    if (result.find("status") != result.end()) {
                        std::string status = result.at("status").get<std::string>();
                        bool accepted = (status == "OK");
                        
                        if (config.debugMode) {
                            threadSafePrint("Share " + std::string(accepted ? "accepted" : "rejected") + 
                                          " by pool (status: " + status + ")", true);
                        }
                        
                        return accepted;
                    }
                }
            }
            threadSafePrint("Invalid response format from pool", true);
        }
        catch (const std::exception& e) {
            threadSafePrint("Error parsing share response: " + std::string(e.what()), true);
        }

        return false;
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

            const picojson::object& responseObj = v.get<picojson::object>();
            if (responseObj.find("result") == responseObj.end() || 
                !responseObj.at("result").is<picojson::object>()) {
                threadSafePrint("Invalid result format", true);
                return false;
            }

            const picojson::object& result = responseObj.at("result").get<picojson::object>();
            
            // Get and store pool ID
            if (result.find("id") != result.end() && result.at("id").is<std::string>()) {
                poolId = result.at("id").get<std::string>();
                threadSafePrint("Pool session ID: " + poolId, true);
            } else {
                threadSafePrint("Warning: No pool ID in login response", true);
                poolId = "1"; // Fallback ID
            }

            // Get job
            if (result.find("job") != result.end() && 
                result.at("job").is<picojson::object>()) {
                const picojson::object& jobObj = result.at("job").get<picojson::object>();
                processNewJob(jobObj);
                return true;
            } else {
                threadSafePrint("No job in login response", true);
                return false;
            }
        }
        catch (const std::exception& e) {
            threadSafePrint("Error processing login response: " + std::string(e.what()), true);
            return false;
        }
    }

    std::string sendAndReceive(const std::string& payload) {
        if (poolSocket == INVALID_SOCKET) {
            threadSafePrint("Cannot send share: Invalid socket", true);
            return "";
        }

        // Add newline to payload
        std::string fullPayload = payload + "\n";

        // Debug output for sending
        threadSafePrint("\nSending to pool:", true);
        threadSafePrint("  Payload: " + fullPayload, true);

        // Send the payload
        int bytesSent = send(poolSocket, fullPayload.c_str(), static_cast<int>(fullPayload.length()), 0);
        if (bytesSent == SOCKET_ERROR) {
            threadSafePrint("Failed to send data: " + std::to_string(WSAGetLastError()), true);
            return "";
        }

        // Receive response with timeout
        std::string response;
        char buffer[4096];
        int totalBytes = 0;
        
        // Set up select for timeout
        fd_set readSet;
        struct timeval timeout;
        
        while (true) {
            FD_ZERO(&readSet);
            FD_SET(poolSocket, &readSet);
            timeout.tv_sec = 10;  // 10 second timeout
            timeout.tv_usec = 0;

            int result = select(0, &readSet, nullptr, nullptr, &timeout);
            if (result == 0) {
                threadSafePrint("Timeout waiting for response", true);
                break;
            }
            if (result == SOCKET_ERROR) {
                threadSafePrint("Select error: " + std::to_string(WSAGetLastError()), true);
                break;
            }

            int bytesReceived = recv(poolSocket, buffer, sizeof(buffer) - 1, 0);
            if (bytesReceived == SOCKET_ERROR) {
                threadSafePrint("Failed to receive data: " + std::to_string(WSAGetLastError()), true);
                break;
            }
            if (bytesReceived == 0) {
                threadSafePrint("Connection closed by pool", true);
                break;
            }

            buffer[bytesReceived] = '\0';
            response += buffer;
            totalBytes += bytesReceived;

            // Check if we have a complete response
            if (response.find("\n") != std::string::npos) {
                break;
            }
        }

        // Clean up response
        while (!response.empty() && (response.back() == '\n' || response.back() == '\r')) {
            response.pop_back();
        }

        // Debug output for receiving
        threadSafePrint("\nReceived from pool:", true);
        threadSafePrint("  Response: " + response, true);
        threadSafePrint("  Total bytes: " + std::to_string(totalBytes), true);

        return response;
    }

    bool sendData(const std::string& data) {
        if (poolSocket == INVALID_SOCKET) {
            threadSafePrint("Cannot send data: Invalid socket", true);
            return false;
        }

        std::string dataWithNewline = data + "\n";
        int result = send(poolSocket, dataWithNewline.c_str(), static_cast<int>(dataWithNewline.length()), 0);
        if (result == SOCKET_ERROR) {
            threadSafePrint("Failed to send data: " + std::to_string(WSAGetLastError()), true);
            return false;
        }
        return true;
    }
} 