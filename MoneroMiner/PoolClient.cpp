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
    std::mutex submitMutex;
    std::string poolId;

    // Forward declarations
    bool sendRequest(const std::string& request);
    void processNewJob(const picojson::object& jobObj);
    bool processShareResponse(const std::string& response);

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
                    MiningStatsUtil::rejectedShares++;
                    return false;
                }
                else if (obj.find("result") != obj.end()) {
                    const picojson::value& resultVal = obj.at("result");
                    bool accepted = false;
                    
                    if (resultVal.is<picojson::object>()) {
                        const picojson::object& resultObj = resultVal.get<picojson::object>();
                        if (resultObj.find("status") != resultObj.end()) {
                            accepted = (resultObj.at("status").get<std::string>() == "OK");
                        }
                    } else if (resultVal.is<bool>()) {
                        accepted = resultVal.get<bool>();
                    }
                    
                    if (accepted) {
                        MiningStatsUtil::acceptedShares++;  // FIX: Use util namespace
                        Utils::threadSafePrint("SHARE ACCEPTED BY POOL!", true);
                    } else {
                        MiningStatsUtil::rejectedShares++;  // FIX: Use util namespace
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
        
        std::string response = sendAndReceive(payload);
        return processShareResponse(response);
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
        size_t colonPos = config.poolAddress.find(':');
        if (colonPos == std::string::npos) {
            Utils::threadSafePrint("Invalid pool address format", true);
            return false;
        }

        std::string host = config.poolAddress.substr(0, colonPos);
        std::string portStr = config.poolAddress.substr(colonPos + 1);
        
        size_t invalidPos = portStr.find(':');
        if (invalidPos != std::string::npos) {
            portStr = portStr.substr(0, invalidPos);
        }

        if (config.debugMode) {
            Utils::threadSafePrint("Connecting to " + host + ":" + portStr, true);
        }

        WSADATA wsaData;
        if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
            Utils::threadSafePrint("WSAStartup failed", true);
            return false;
        }

        struct addrinfo hints = {}, *result = nullptr;
        hints.ai_family = AF_INET;
        hints.ai_socktype = SOCK_STREAM;
        hints.ai_protocol = IPPROTO_TCP;

        if (getaddrinfo(host.c_str(), portStr.c_str(), &hints, &result) != 0) {
            Utils::threadSafePrint("Failed to resolve pool address: " + host, true);
            WSACleanup();
            return false;
        }

        poolSocket = socket(result->ai_family, result->ai_socktype, result->ai_protocol);
        if (poolSocket == INVALID_SOCKET) {
            Utils::threadSafePrint("Failed to create socket", true);
            freeaddrinfo(result);
            WSACleanup();
            return false;
        }

        DWORD timeout = 10000;
        setsockopt(poolSocket, SOL_SOCKET, SO_RCVTIMEO, (char*)&timeout, sizeof(timeout));
        setsockopt(poolSocket, SOL_SOCKET, SO_SNDTIMEO, (char*)&timeout, sizeof(timeout));

        if (::connect(poolSocket, result->ai_addr, (int)result->ai_addrlen) == SOCKET_ERROR) {
            Utils::threadSafePrint("Failed to connect to pool", true);
            closesocket(poolSocket);
            freeaddrinfo(result);
            WSACleanup();
            return false;
        }

        freeaddrinfo(result);
        Utils::threadSafePrint("Connected to pool", true);
        return true;
    }

    bool login(const std::string& wallet, const std::string& password, 
               const std::string& worker, const std::string& userAgent) {
        if (poolSocket == INVALID_SOCKET) {
            Utils::threadSafePrint("Cannot login: Invalid socket", true);
            return false;
        }

        try {
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
            Utils::threadSafePrint("Sending login request", true);
            
            std::string fullRequest = request + "\n";
            int sendResult = send(poolSocket, fullRequest.c_str(), static_cast<int>(fullRequest.length()), 0);
            if (sendResult == SOCKET_ERROR) {
                Utils::threadSafePrint("Failed to send login: " + std::to_string(WSAGetLastError()), true);
                return false;
            }

            char buffer[4096];
            std::string response;
            bool complete = false;
            
            while (!complete) {
                int bytesReceived = recv(poolSocket, buffer, sizeof(buffer) - 1, 0);
                if (bytesReceived == SOCKET_ERROR || bytesReceived == 0) {
                    Utils::threadSafePrint("Failed to receive login response", true);
                    return false;
                }

                buffer[bytesReceived] = '\0';
                response += buffer;

                try {
                    picojson::value v;
                    std::string err = picojson::parse(v, response);
                    if (err.empty()) complete = true;
                } catch (...) {
                    continue;
                }
            }

            while (!response.empty() && (response.back() == '\n' || response.back() == '\r')) {
                response.pop_back();
            }

            if (response.empty()) {
                Utils::threadSafePrint("Empty login response", true);
                return false;
            }

            Utils::threadSafePrint("Received login response", true);
            
            if (!handleLoginResponse(response)) {
                return false;
            }
            
            {
                std::lock_guard<std::mutex> lock(jobMutex);
                if (jobQueue.empty()) {
                    Utils::threadSafePrint("No job in login response", true);
                    return false;
                }
            }
            
            return true;
        }
        catch (const std::exception& e) {
            Utils::threadSafePrint("Login error: " + std::string(e.what()), true);
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
        while (!shouldStop) {
            if (poolSocket == INVALID_SOCKET) {
                Utils::threadSafePrint("Pool connection lost", true);
                std::this_thread::sleep_for(std::chrono::seconds(5));
                continue;
            }

            std::string response = receiveData(poolSocket);
            if (response.empty()) {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                continue;
            }

            try {
                picojson::value v;
                std::string err = picojson::parse(v, response);
                if (!err.empty()) continue;
                if (!v.is<picojson::object>()) continue;

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
                Utils::threadSafePrint("Error processing response: " + std::string(e.what()), true);
            }
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
            job.difficulty = static_cast<uint64_t>(RandomXManager::getDifficulty());

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
        std::queue<Job> empty;
        std::swap(jobQueue, empty);
        jobQueue.push(job);
        
        if (config.debugMode) {
            Utils::threadSafePrint("[JOB] ID=" + job.jobId + " Height=" + 
                std::to_string(job.height), true);
        }
        
        handleSeedHashChange(job.seedHash);
        jobQueueCondition.notify_all();
        jobAvailable.notify_all();
    }

    std::string getPoolId() {
        return poolId;
    }

    bool reconnect() {
        cleanup();
        if (!connect()) return false;
        return login(config.walletAddress, config.password, config.workerName, config.userAgent);
    }
}