#include "PoolClient.h"
#include "Globals.h"
#include "Config.h"
#include "Utils.h"
#include "Constants.h"
#include "RandomXManager.h"
#include "MiningThreadData.h"
#include "Job.h"
#include "MiningStats.h"
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
    bool processShareResponse(const std::string& response);

    class ShareSubmitter {
    private:
        static std::mutex submitMutex;
        
    public:
        static bool submitShare(const std::string& jobId, uint32_t nonce, 
                              const std::vector<uint8_t>& hash) {
            std::lock_guard<std::mutex> lock(submitMutex);
            
            std::string nonceHex = Utils::formatHex(nonce, 8);
            std::string hashHex = Utils::bytesToHex(hash);
            
            picojson::object submitObj;
            submitObj["id"] = picojson::value(1.0);
            submitObj["jsonrpc"] = picojson::value("2.0");
            submitObj["method"] = picojson::value("submit");
            
            picojson::object params;
            params["id"] = picojson::value(poolId);
            params["job_id"] = picojson::value(jobId);
            params["nonce"] = picojson::value(nonceHex);
            params["result"] = picojson::value(hashHex);
            
            submitObj["params"] = picojson::value(params);
            
            std::string request = picojson::value(submitObj).serialize() + "\n";
            std::string response = sendData(request);
            
            return processShareResponse(response);
        }
    };

    bool processShareResponse(const std::string& response) {
        if (response.empty()) {
            Utils::threadSafePrint("ERROR: Empty response from pool", true);
            return false;
        }

        try {
            picojson::value v;
            std::string err = picojson::parse(v, response);
            if (err.empty() && v.is<picojson::object>()) {
                const picojson::object& obj = v.get<picojson::object>();
                
                if (config.debugMode) {
                    Utils::threadSafePrint("Parsed pool response: " + v.serialize(), true);
                }

                if (obj.find("error") != obj.end() && !obj.at("error").is<picojson::null>()) {
                    Utils::threadSafePrint("Share rejected - Error: " + obj.at("error").serialize(), true);
                    MiningStats::rejectedShares++;
                    return false;
                }
                else if (obj.find("result") != obj.end()) {
                    const picojson::value& result = obj.at("result");
                    bool accepted = false;
                    
                    if (result.is<picojson::object>()) {
                        const picojson::object& resultObj = result.get<picojson::object>();
                        if (resultObj.find("status") != resultObj.end()) {
                            accepted = (resultObj.at("status").get<std::string>() == "OK");
                        }
                    } else if (result.is<bool>()) {
                        accepted = result.get<bool>();
                    }
                    
                    if (accepted) {
                        MiningStats::acceptedShares++;
                        Utils::threadSafePrint("SHARE ACCEPTED BY POOL!", true);
                    } else {
                        MiningStats::rejectedShares++;
                        Utils::threadSafePrint("Share rejected by pool", true);
                    }
                    return accepted;
                }
            }
        }
        catch (const std::exception& e) {
            Utils::threadSafePrint("Error processing pool response: " + std::string(e.what()), true);
            return false;
        }
        return false;
    }

    bool submitShare(const std::string& jobId, 
                    const std::string& nonce, 
                    const std::string& result, 
                    const std::string& id) {
        std::string submitRequest = "{\"id\":1,\"jsonrpc\":\"2.0\",\"method\":\"submit\","
            "\"params\":{\"id\":\"" + id + "\","
            "\"job_id\":\"" + jobId + "\","
            "\"nonce\":\"" + nonce + "\","
            "\"result\":\"" + result + "\"}}";

        if (config.debugMode) {
            Utils::threadSafePrint("\n=== Share Submission ===", true);
            Utils::threadSafePrint("Job ID: " + jobId, true);
            Utils::threadSafePrint("Nonce: " + nonce, true);
            Utils::threadSafePrint("Result: " + result, true);
        }

        std::string response = sendData(submitRequest);
        return processShareResponse(response);
    }

    bool submitShare(const std::string& jobId, 
                    uint32_t nonce, 
                    const std::vector<uint8_t>& hash) {
        std::string nonceHex = Utils::formatHex(nonce, 8);
        std::string hashHex = Utils::bytesToHex(hash);
        
        return submitShare(jobId, nonceHex, hashHex, poolId);
    }

    bool submitShare(const std::string& jobId, uint64_t nonce, const std::vector<uint8_t>& hash) {
        if (poolId.empty()) {
            Utils::threadSafePrint("Cannot submit share: No pool session ID", true);
            return false;
        }

        try {
            std::lock_guard<std::mutex> lock(submitMutex);
            
            // Convert nonce to hex (little-endian)
            std::stringstream nonceHex;
            nonceHex << std::hex << std::setw(8) << std::setfill('0');
            for (int i = 0; i < 4; i++) {
                nonceHex << std::setw(2) << static_cast<int>((nonce >> (i * 8)) & 0xFF);
            }
            
            // Convert hash to hex
            std::string hashHex = Utils::bytesToHex(hash);
            
            // Create submit request
            picojson::object submitObj;
            submitObj["id"] = picojson::value(static_cast<double>(jsonRpcId.fetch_add(1)));
            submitObj["jsonrpc"] = picojson::value("2.0");
            submitObj["method"] = picojson::value("submit");
            
            picojson::object params;
            params["id"] = picojson::value(poolId);
            params["job_id"] = picojson::value(jobId);
            params["nonce"] = picojson::value(nonceHex.str());
            params["result"] = picojson::value(hashHex);
            
            submitObj["params"] = picojson::value(params);
            
            std::string request = picojson::value(submitObj).serialize() + "\n";
            
            if (config.debugMode) {
                Utils::threadSafePrint("\n=== Share Submission ===", true);
                Utils::threadSafePrint("Job ID: " + jobId, true);
                Utils::threadSafePrint("Nonce: " + nonceHex.str(), true);
                Utils::threadSafePrint("Result: " + hashHex, true);
            }
            
            std::string response = sendData(request);
            return processShareResponse(response);
        }
        catch (const std::exception& e) {
            Utils::threadSafePrint("Error submitting share: " + std::string(e.what()), true);
            return false;
        }
    }

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

    bool connect() {
        // Parse pool address and port
        size_t colonPos = config.poolAddress.find(':');
        if (colonPos == std::string::npos) {
            Utils::threadSafePrint("Invalid pool address format. Expected format: hostname:port", true);
            return false;
        }

        std::string host = config.poolAddress.substr(0, colonPos);
        std::string portStr = config.poolAddress.substr(colonPos + 1);
        
        // Remove any trailing :0 or other invalid suffixes
        size_t invalidPos = portStr.find(':');
        if (invalidPos != std::string::npos) {
            portStr = portStr.substr(0, invalidPos);
        }

        if (config.debugMode) {
            Utils::threadSafePrint("Attempting to connect to " + host + ":" + portStr, true);
        }

        // Initialize Winsock
        WSADATA wsaData;
        if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
            Utils::threadSafePrint("WSAStartup failed", true);
            return false;
        }

        // Get address info
        struct addrinfo hints = {}, *result = nullptr;
        hints.ai_family = AF_INET;
        hints.ai_socktype = SOCK_STREAM;
        hints.ai_protocol = IPPROTO_TCP;

        if (getaddrinfo(host.c_str(), portStr.c_str(), &hints, &result) != 0) {
            Utils::threadSafePrint("Failed to resolve pool address: " + host, true);
            WSACleanup();
            return false;
        }

        // Create socket and connect
        poolSocket = socket(result->ai_family, result->ai_socktype, result->ai_protocol);
        if (poolSocket == INVALID_SOCKET) {
            Utils::threadSafePrint("Failed to create socket", true);
            freeaddrinfo(result);
            WSACleanup();
            return false;
        }

        // Set socket timeout
        DWORD timeout = 10000; // 10 seconds
        setsockopt(poolSocket, SOL_SOCKET, SO_RCVTIMEO, (char*)&timeout, sizeof(timeout));
        setsockopt(poolSocket, SOL_SOCKET, SO_SNDTIMEO, (char*)&timeout, sizeof(timeout));

        if (connect(poolSocket, result->ai_addr, (int)result->ai_addrlen) == SOCKET_ERROR) {
            Utils::threadSafePrint("Failed to connect to pool: " + host + ":" + portStr, true);
            closesocket(poolSocket);
            freeaddrinfo(result);
            WSACleanup();
            return false;
        }

        freeaddrinfo(result);
        Utils::threadSafePrint("Successfully connected to pool", true);
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
                if (!connect()) {
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

    void processNewJob(const picojson::object& jobData) {
        try {
            // Extract job data
            std::string blob = jobData.at("blob").get<std::string>();
            std::string jobId = jobData.at("job_id").get<std::string>();
            std::string target = jobData.at("target").get<std::string>();
            uint64_t height = static_cast<uint64_t>(jobData.at("height").get<double>());
            std::string seedHash = jobData.at("seed_hash").get<std::string>();

            // Set target and get difficulty from RandomXManager
            if (!RandomXManager::setTargetAndDifficulty(target)) {
                Utils::threadSafePrint("Failed to set target", true);
                return;
            }

            // Create new job with 5 parameters (matching Job constructor)
            Job job;
            job.blob = blob;
            job.jobId = jobId;
            job.target = target;
            job.height = height;
            job.seedHash = seedHash;
            job.difficulty = RandomXManager::getDifficulty();

            // Distribute job to mining threads
            distributeJob(job);
        }
        catch (const std::exception& e) {
            Utils::threadSafePrint("Error processing new job: " + std::string(e.what()), true);
        }
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

    std::string sendData(const std::string& data) {
        if (config.debugMode) {
            Utils::threadSafePrint("\nSending to pool: " + data, true);
        }

        std::lock_guard<std::mutex> lock(socketMutex);
        if (poolSocket == INVALID_SOCKET) {
            Utils::threadSafePrint("Cannot send data: Not connected to pool", true);
            return "";
        }

        int bytesSent = send(poolSocket, data.c_str(), static_cast<int>(data.length()), 0);
        if (bytesSent == SOCKET_ERROR) {
            Utils::threadSafePrint("Failed to send data: " + std::to_string(WSAGetLastError()), true);
            return "";
        }

        std::string response = receiveData(poolSocket);
        
        if (config.debugMode) {
            Utils::threadSafePrint("Received from pool: " + response, true);
        }

        return response;
    }

    void distributeJob(const Job& job) {
        std::lock_guard<std::mutex> lock(jobMutex);
        
        // Clear existing jobs from queue
        std::queue<Job> empty;
        std::swap(jobQueue, empty);
        
        // Add new job to queue
        jobQueue.push(job);
        
        if (config.debugMode) {
            Utils::threadSafePrint("Job queue updated with new job: " + job.getJobId(), true);
            Utils::threadSafePrint("Queue size: " + std::to_string(jobQueue.size()), true);
            Utils::threadSafePrint("New job details:", true);
            Utils::threadSafePrint("  Height: " + std::to_string(job.height), true);
            Utils::threadSafePrint("  Job ID: " + job.getJobId(), true);
            Utils::threadSafePrint("  Target: 0x" + job.target, true);
            Utils::threadSafePrint("  Blob: " + job.blob, true);
            Utils::threadSafePrint("  Seed Hash: " + job.seedHash, true);
            Utils::threadSafePrint("  Difficulty: " + std::to_string(job.difficulty), true);
        }

        // Handle seed hash change if needed
        handleSeedHashChange(job.seedHash);

        // Notify all waiting threads
        jobQueueCondition.notify_all();
        jobAvailable.notify_all();
    }

    std::string getPoolId() {
        return poolId;
    }

    bool reconnect() {
        cleanup();
        if (!connect()) {
            return false;
        }
        return login(config.walletAddress, config.password, config.workerName, config.userAgent);
    }
}