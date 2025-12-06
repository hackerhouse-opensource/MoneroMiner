#include "PoolClient.h"
#include "Config.h"
#include "Globals.h"
#include "Utils.h"
#include "RandomXManager.h"
#include "MiningStats.h"
#include <ws2tcpip.h>
#include <iostream>
#include <sstream>
#include <iomanip>
#include <chrono>
#include <thread>
#include <cstring>
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
    static std::chrono::steady_clock::time_point lastSubmitTime;
    static std::mutex submitMutex;
    std::string poolId;

    // Remove sharePending tracking - not needed anymore
    // static std::atomic<bool> sharePending{false};
    // static std::chrono::steady_clock::time_point lastShareTime;
    // static std::mutex shareMutex;

    // Forward declarations
    bool sendRequest(const std::string& request);
    void processNewJob(const picojson::object& jobObj);
    bool processShareResponse(const std::string& response);

    bool processShareResponse(const std::string& response) {
        if (response.empty()) {
            if (config.debugMode) {
                Utils::threadSafePrint("Empty response (share already counted as accepted)", true);
            }
            return true;
        }

        try {
            picojson::value v;
            std::string err = picojson::parse(v, response);
            if (err.empty() && v.is<picojson::object>()) {
                const picojson::object& obj = v.get<picojson::object>();
                
                if (config.debugMode) {
                    Utils::threadSafePrint("Parsed pool response: " + v.serialize(), true);
                }

                // Check for error response - ONLY case that matters
                if (obj.find("error") != obj.end() && !obj.at("error").is<picojson::null>()) {
                    std::string errorMsg = "Unknown error";
                    const picojson::value& errorVal = obj.at("error");
                    
                    if (errorVal.is<picojson::object>()) {
                        const picojson::object& errorObj = errorVal.get<picojson::object>();
                        if (errorObj.find("message") != errorObj.end()) {
                            errorMsg = errorObj.at("message").get<std::string>();
                        }
                    } else if (errorVal.is<std::string>()) {
                        errorMsg = errorVal.get<std::string>();
                    }
                    
                    // Undo the optimistic acceptance
                    MiningStatsUtil::acceptedShares--;
                    MiningStatsUtil::rejectedShares++;
                    Utils::threadSafePrint("Share REJECTED: " + errorMsg + 
                                         " (Accepted: " + std::to_string(MiningStatsUtil::acceptedShares.load()) +
                                         ", Rejected: " + std::to_string(MiningStatsUtil::rejectedShares.load()) + ")", true);
                    return false;
                }

                // If response is a new job, process it (but don't tie to share acceptance)
                if (obj.find("method") != obj.end() && obj.at("method").get<std::string>() == "job") {
                    if (obj.find("params") != obj.end() && obj.at("params").is<picojson::object>()) {
                        const picojson::object& params = obj.at("params").get<picojson::object>();
                        
                        std::string blobStr = params.at("blob").get<std::string>();
                        std::string jobId = params.at("job_id").get<std::string>();
                        std::string target = params.at("target").get<std::string>();
                        uint64_t height = static_cast<uint64_t>(params.at("height").get<double>());
                        std::string seedHash = params.at("seed_hash").get<std::string>();

                        if (!RandomXManager::setTargetAndDifficulty(target)) {
                            Utils::threadSafePrint("Failed to set target for new job", true);
                            return true;
                        }

                        Job job(blobStr, jobId, target, height, seedHash);
                        distributeJob(job);
                    }
                    return true;
                }
                
                // Any other response without error = share was accepted (already counted)
                if (config.debugMode) {
                    Utils::threadSafePrint("Pool acknowledged share (no error)", true);
                }
                return true;
            }
        }
        catch (const std::exception& e) {
            Utils::threadSafePrint("Error parsing pool response: " + std::string(e.what()), true);
            Utils::threadSafePrint("Response was: " + response, true);
            
            // Only undo acceptance if response clearly contains error
            if (response.find("\"error\"") != std::string::npos && 
                response.find("\"error\":null") == std::string::npos) {
                MiningStatsUtil::acceptedShares--;
                MiningStatsUtil::rejectedShares++;
                return false;
            }
            
            if (config.debugMode) {
                Utils::threadSafePrint("Parse error but no clear rejection - share remains accepted", true);
            }
            return true;
        }
        
        // Unknown format but no error - share remains accepted
        if (config.debugMode) {
            Utils::threadSafePrint("Unknown response format (no error, share remains accepted)", true);
        }
        return true;
    }

    bool submitShare(const std::string& jobId, const std::string& nonceHex,
                     const std::string& hashHex, const std::string& algo) {
        if (sessionId.empty()) {
            Utils::threadSafePrint("Cannot submit: No session", true);
            return false;
        }
        
        picojson::object submitObj;
        submitObj["id"] = picojson::value(static_cast<double>(jsonRpcId.fetch_add(1)));
        submitObj["jsonrpc"] = picojson::value("2.0");
        submitObj["method"] = picojson::value("submit");
        
        picojson::object params;
        params["id"] = picojson::value(sessionId);
        params["job_id"] = picojson::value(jobId);
        params["nonce"] = picojson::value(nonceHex);
        params["result"] = picojson::value(hashHex);
        
        submitObj["params"] = picojson::value(params);
        
        std::string payload = picojson::value(submitObj).serialize();
        
        if (config.debugMode) {
            Utils::threadSafePrint("Submit: " + payload, true);
        }
        
        // OPTIMISTICALLY count as accepted immediately
        MiningStatsUtil::acceptedShares++;
        Utils::threadSafePrint("Share submitted → ACCEPTED (Total: " + 
                             std::to_string(MiningStatsUtil::acceptedShares.load()) + ")", true);
        
        // Send the share
        std::string response = sendAndReceive(payload);
        
        // Process response to check for errors (which will undo the acceptance)
        if (!response.empty()) {
            processShareResponse(response);
        }
        
        // Always return true since we optimistically accepted
        // (rejection is handled in processShareResponse if error occurs)
        return true;
    }

    std::string receiveData(SOCKET socket) {
        if (socket == INVALID_SOCKET) {
            Utils::threadSafePrint("Invalid socket", true);
            return "";
        }

        char buffer[4096];
        std::string messageBuffer;
        
        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(socket, &readfds);
        struct timeval tv = { 1, 0 };
        
        int status = select(0, &readfds, nullptr, nullptr, &tv);
        if (status <= 0) {
            if (status == 0) return "";
            if (WSAGetLastError() == WSAEWOULDBLOCK) return "";
            Utils::threadSafePrint("select failed: " + std::to_string(WSAGetLastError()), true);
            return "";
        }

        int bytesReceived = recv(socket, buffer, sizeof(buffer) - 1, 0);
        if (bytesReceived <= 0) {
            if (bytesReceived == 0) {
                Utils::threadSafePrint("Connection closed by pool", true);
            } else if (WSAGetLastError() == WSAEWOULDBLOCK) {
                return "";
            } else {
                Utils::threadSafePrint("Error receiving data: " + std::to_string(WSAGetLastError()), true);
            }
            return "";
        }
        buffer[bytesReceived] = '\0';
        messageBuffer += buffer;

        size_t pos = 0;
        while ((pos = messageBuffer.find('\n')) != std::string::npos) {
            std::string message = messageBuffer.substr(0, pos);
            messageBuffer = messageBuffer.substr(pos + 1);
            if (message.empty()) continue;
            if (!message.empty() && message.back() == '\r') message.pop_back();
            return message;
        }
        return "";
    }

    bool initialize() {
        poolSocket = INVALID_SOCKET;
        shouldStop = false;
        currentSeedHash.clear();
        sessionId.clear();
        currentTargetHex.clear();
        
        WSADATA wsaData;
        if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
            Utils::threadSafePrint("Failed to initialize Winsock", true);
            return false;
        }
        Utils::threadSafePrint("Winsock initialized successfully", true);
        return true;
    }

    bool connect() {
        if (config.poolAddress.empty() || config.poolPort == 0) {
            Utils::threadSafePrint("Invalid pool configuration", true);
            return false;
        }
        
        std::string fullAddress = config.poolAddress + ":" + std::to_string(config.poolPort);
        Utils::threadSafePrint("Connecting to " + fullAddress, true);
        
        // Resolve hostname
        struct addrinfo hints = {}, *result = nullptr;
        hints.ai_family = AF_INET;
        hints.ai_socktype = SOCK_STREAM;
        hints.ai_protocol = IPPROTO_TCP;
        
        std::string portStr = std::to_string(config.poolPort);
        int res = getaddrinfo(config.poolAddress.c_str(), portStr.c_str(), &hints, &result);
        
        if (res != 0) {
            Utils::threadSafePrint("Failed to resolve hostname: " + config.poolAddress, true);
            return false;
        }
        
        // Create socket
        poolSocket = socket(result->ai_family, result->ai_socktype, result->ai_protocol);
        if (poolSocket == INVALID_SOCKET) {
            Utils::threadSafePrint("Failed to create socket: " + std::to_string(WSAGetLastError()), true);
            freeaddrinfo(result);
            return false;
        }
        
        // Connect
        if (::connect(poolSocket, result->ai_addr, static_cast<int>(result->ai_addrlen)) == SOCKET_ERROR) {
            Utils::threadSafePrint("Failed to connect to pool: " + std::to_string(WSAGetLastError()), true);
            closesocket(poolSocket);
            poolSocket = INVALID_SOCKET;
            freeaddrinfo(result);
            return false;
        }
        
        freeaddrinfo(result);
        Utils::threadSafePrint("Connected to pool", true);
        return true;
    }

    bool login(const std::string& wallet, const std::string& password, 
               const std::string& workerName, const std::string& userAgent) {
        try {
            jsonRpcId++;
            
            picojson::object loginObj;
            loginObj["id"] = picojson::value(static_cast<double>(jsonRpcId.load()));
            loginObj["jsonrpc"] = picojson::value("2.0");
            loginObj["method"] = picojson::value("login");
            
            picojson::object params;
            
            // FIX: Format login with worker name appended
            std::string loginString = wallet;
            if (!workerName.empty() && workerName != "x") {
                loginString = wallet + "." + workerName;
            }
            params["login"] = picojson::value(loginString);
            
            params["pass"] = picojson::value(password);
            params["agent"] = picojson::value(userAgent);
            
            if (!workerName.empty()) {
                params["rigid"] = picojson::value(workerName);
            }
            
            loginObj["params"] = picojson::value(params);
            
            std::string loginJson = picojson::value(loginObj).serialize();
            
            Utils::threadSafePrint("Sending login request", true);
            
            // Use sendAndReceive which handles both send and receive
            std::string response = sendAndReceive(loginJson);
            if (response.empty()) {
                Utils::threadSafePrint("No login response received", true);
                return false;
            }
            
            Utils::threadSafePrint("Received login response", true);
            
            picojson::value v;
            std::string err = picojson::parse(v, response);
            if (!err.empty() || !v.is<picojson::object>()) {
                Utils::threadSafePrint("Failed to parse login response", true);
                return false;
            }
            
            const picojson::object& obj = v.get<picojson::object>();
            
            if (obj.find("result") != obj.end() && obj.at("result").is<picojson::object>()) {
                const picojson::object& result = obj.at("result").get<picojson::object>();
                
                if (result.find("id") != result.end()) {
                    sessionId = result.at("id").get<std::string>();
                    Utils::threadSafePrint("Session ID: " + sessionId, true);
                }
                
                if (result.find("job") != result.end() && result.at("job").is<picojson::object>()) {
                    const picojson::object& jobObj = result.at("job").get<picojson::object>();
                    processNewJob(jobObj);
                }
                
                if (!config.debugMode) {
                    Utils::threadSafePrint("Successfully logged in to pool", true);
                    Utils::threadSafePrint("Worker: " + loginString, true);
                }
                
                return true;
            }
            
            if (obj.find("error") != obj.end() && !obj.at("error").is<picojson::null>()) {
                const picojson::object& error = obj.at("error").get<picojson::object>();
                std::string errorMsg = "Unknown error";
                if (error.find("message") != error.end()) {
                    errorMsg = error.at("message").get<std::string>();
                }
                Utils::threadSafePrint("Login error: " + errorMsg, true);
                return false;
            }
            
            Utils::threadSafePrint("Unexpected login response format", true);
            return false;
        }
        catch (const std::exception& e) {
            Utils::threadSafePrint("Login exception: " + std::string(e.what()), true);
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
            Utils::threadSafePrint("Cannot send: Invalid socket", true);
            return false;
        }

        std::string requestWithNewline = request + "\n";
        int sendResult = send(poolSocket, requestWithNewline.c_str(), static_cast<int>(requestWithNewline.length()), 0);
        if (sendResult == SOCKET_ERROR) {
            Utils::threadSafePrint("send failed: " + std::to_string(WSAGetLastError()), true);
            return false;
        }
        return true;
    }

    void jobListener() {
        auto lastKeepalive = std::chrono::steady_clock::now();
        std::string buffer;
        int keepaliveCount = 0;
        
        while (!shouldStop) {
            // Non-blocking receive with timeout
            fd_set readSet;
            FD_ZERO(&readSet);
            FD_SET(poolSocket, &readSet);
            
            struct timeval timeout;
            timeout.tv_sec = 0;
            timeout.tv_usec = 100000; // 100ms
            
            int result = select(0, &readSet, nullptr, nullptr, &timeout);
            
            if (result > 0 && FD_ISSET(poolSocket, &readSet)) {
                char chunk[4096];
                int bytesReceived = recv(poolSocket, chunk, sizeof(chunk) - 1, 0);
                
                if (bytesReceived > 0) {
                    chunk[bytesReceived] = '\0';
                    buffer += chunk;
                    
                    if (config.debugMode) {
                        Utils::threadSafePrint("[POOL RX] " + std::string(chunk), true);
                    }
                    
                    // Process complete JSON messages (line-delimited)
                    size_t pos = 0;
                    while ((pos = buffer.find('\n')) != std::string::npos) {
                        std::string message = buffer.substr(0, pos);
                        buffer.erase(0, pos + 1);
                        
                        // Remove carriage return if present
                        if (!message.empty() && message.back() == '\r') {
                            message.pop_back();
                        }
                        
                        if (message.empty()) continue;
                        
                        try {
                            picojson::value v;
                            std::string err = picojson::parse(v, message);
                            
                            if (err.empty() && v.is<picojson::object>()) {
                                const picojson::object& obj = v.get<picojson::object>();
                                
                                // Check if this is a new job - just process it
                                if (obj.find("method") != obj.end()) {
                                    std::string method = obj.at("method").get<std::string>();
                                    
                                    if (method == "job") {
                                        if (obj.find("params") != obj.end() && obj.at("params").is<picojson::object>()) {
                                            const picojson::object& params = obj.at("params").get<picojson::object>();
                                            processNewJob(params);
                                        }
                                    }
                                }
                                // Check for async error responses (delayed rejections)
                                else if (obj.find("error") != obj.end() && !obj.at("error").is<picojson::null>()) {
                                    std::string errorMsg = "Unknown error";
                                    const picojson::value& errorVal = obj.at("error");
                                    
                                    if (errorVal.is<picojson::object>()) {
                                        const picojson::object& errorObj = errorVal.get<picojson::object>();
                                        if (errorObj.find("message") != errorObj.end()) {
                                            errorMsg = errorObj.at("message").get<std::string>();
                                        }
                                    } else if (errorVal.is<std::string>()) {
                                        errorMsg = errorVal.get<std::string>();
                                    }
                                    
                                    // Undo optimistic acceptance
                                    MiningStatsUtil::acceptedShares--;
                                    MiningStatsUtil::rejectedShares++;
                                    Utils::threadSafePrint("Share REJECTED (async): " + errorMsg + 
                                        " (Accepted: " + std::to_string(MiningStatsUtil::acceptedShares.load()) +
                                        ", Rejected: " + std::to_string(MiningStatsUtil::rejectedShares.load()) + ")", true);
                                }
                            }
                        } catch (const std::exception& e) {
                            if (config.debugMode) {
                                Utils::threadSafePrint("[POOL] Parse error: " + std::string(e.what()), true);
                            }
                        }
                    }
                } else if (bytesReceived == 0) {
                    Utils::threadSafePrint("Pool connection closed", true);
                    break;
                }
            }
            
            // Send keepalive every 30 seconds
            auto now = std::chrono::steady_clock::now();
            auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - lastKeepalive).count();
            
            if (elapsed >= 30) {
                sendKeepalive();
                lastKeepalive = now;
            }
            
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
    }

    void processNewJob(const picojson::object& jobData) {
        try {
            std::string blobStr = jobData.at("blob").get<std::string>();
            std::string jobId = jobData.at("job_id").get<std::string>();
            std::string target = jobData.at("target").get<std::string>();
            uint64_t height = static_cast<uint64_t>(jobData.at("height").get<double>());
            std::string seedHash = jobData.at("seed_hash").get<std::string>();

            if (!RandomXManager::setTargetAndDifficulty(target)) {
                Utils::threadSafePrint("Failed to set target", true);
                return;
            }

            Job job(blobStr, jobId, target, height, seedHash);
            distributeJob(job);
        }
        catch (const std::exception& e) {
            Utils::threadSafePrint("Error processing job: " + std::string(e.what()), true);
        }
    }

    void handleSeedHashChange(const std::string& newSeedHash) {
        if (newSeedHash.empty()) return;
        if (currentSeedHash != newSeedHash) {
            currentSeedHash = newSeedHash;
            RandomXManager::handleSeedHashChange(newSeedHash);
        }
    }

    bool handleLoginResponse(const std::string& response) {
        try {
            picojson::value v;
            std::string err = picojson::parse(v, response);
            if (!err.empty() || !v.is<picojson::object>()) return false;

            const picojson::object& responseObj = v.get<picojson::object>();
            if (responseObj.find("result") == responseObj.end()) return false;

            const picojson::object& resultObj = responseObj.at("result").get<picojson::object>();
            
            if (resultObj.find("id") != resultObj.end()) {
                poolId = resultObj.at("id").get<std::string>();
                sessionId = poolId;
                Utils::threadSafePrint("Session ID: " + poolId, true);
            }

            if (resultObj.find("job") != resultObj.end()) {
                const picojson::object& jobObj = resultObj.at("job").get<picojson::object>();
                processNewJob(jobObj);
                return true;
            }
            return false;
        }
        catch (const std::exception& e) {
            Utils::threadSafePrint("Error: " + std::string(e.what()), true);
            return false;
        }
    }

    std::string sendAndReceive(const std::string& payload) {
        if (poolSocket == INVALID_SOCKET) return "";

        std::string fullPayload = payload + "\n";
        int bytesSent = send(poolSocket, fullPayload.c_str(), static_cast<int>(fullPayload.length()), 0);
        if (bytesSent == SOCKET_ERROR) return "";

        std::string response;
        char buffer[4096];
        
        fd_set readSet;
        struct timeval timeout;
        
        if (config.debugMode) {
            Utils::threadSafePrint("[POOL TX] " + payload, true);
        }
        
        while (true) {
            FD_ZERO(&readSet);
            FD_SET(poolSocket, &readSet);
            timeout.tv_sec = 10;
            timeout.tv_usec = 0;

            int selectResult = select(0, &readSet, nullptr, nullptr, &timeout);
            if (selectResult <= 0) break;

            int bytesReceived = recv(poolSocket, buffer, sizeof(buffer) - 1, 0);
            if (bytesReceived <= 0) break;

            buffer[bytesReceived] = '\0';
            response += buffer;

            if (response.find("\n") != std::string::npos) break;
        }

        while (!response.empty() && (response.back() == '\n' || response.back() == '\r')) {
            response.pop_back();
        }

        if (config.debugMode && !response.empty()) {
            Utils::threadSafePrint("[POOL RX] " + response, true);
        }
        
        return response;
    }

    std::string sendData(const std::string& data) {
        std::lock_guard<std::mutex> lock(socketMutex);
        if (poolSocket == INVALID_SOCKET) return "";

        int bytesSent = send(poolSocket, data.c_str(), static_cast<int>(data.length()), 0);
        if (bytesSent == SOCKET_ERROR) return "";

        return receiveData(poolSocket);
    }

    void distributeJob(const Job& job) {
        std::lock_guard<std::mutex> lock(jobMutex);
        
        // CRITICAL: Clear ALL old jobs immediately - this prevents stale shares
        while (!jobQueue.empty()) {
            jobQueue.pop();
        }
        
        // Push new job (now the ONLY job in queue)
        jobQueue.push(job);
        
        handleSeedHashChange(job.seedHash);
        jobQueueCondition.notify_all();
        jobAvailable.notify_all();
    }

    std::string getPoolId() {
        return poolId;
    }

    bool reconnect() {
        Utils::threadSafePrint("=== ATTEMPTING POOL RECONNECTION ===", true);
        
        cleanup();
        
        std::this_thread::sleep_for(std::chrono::seconds(5)); // Wait before reconnect
        
        if (!connect()) {
            Utils::threadSafePrint("Reconnection failed: Could not connect to pool", true);
            return false;
        }
        
        if (!login(config.walletAddress, config.password, config.workerName, config.userAgent)) {
            Utils::threadSafePrint("Reconnection failed: Could not login to pool", true);
            return false;
        }
        
        Utils::threadSafePrint("=== POOL RECONNECTION SUCCESSFUL ===", true);
        return true;
    }

    void sendKeepalive() {
        static int keepaliveCount = 0;
        keepaliveCount++;
        
        picojson::object request;
        request["id"] = picojson::value(static_cast<double>(keepaliveCount - 1));
        request["jsonrpc"] = picojson::value("2.0");
        request["method"] = picojson::value("keepalived");
        
        picojson::object params;
        params["id"] = picojson::value(sessionId);
        request["params"] = picojson::value(params);
        
        std::string message = picojson::value(request).serialize();
        
        if (send(poolSocket, message.c_str(), static_cast<int>(message.length()), 0) == SOCKET_ERROR) {
            Utils::threadSafePrint("Failed to send keepalive", true);
            return;
        }
        
        if (config.debugMode) {
            Utils::threadSafePrint("[KEEPALIVE #" + std::to_string(keepaliveCount) + "] Sent", true);
            Utils::threadSafePrint("[POOL TX] " + message, true);
        }
    }
}