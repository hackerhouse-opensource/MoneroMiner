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
    if (config.poolAddress.empty()) {
        threadSafePrint("Error: Pool address is required", false);
        return false;
    }
    if (config.poolPort.empty()) {
        threadSafePrint("Error: Pool port is required", false);
        return false;
    }
    if (config.walletAddress.empty()) {
        threadSafePrint("Error: Wallet address is required", false);
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
        if (arg == "--help") {
            printHelp();
            return false;
        } else if (arg == "--debug") {
            config.debugMode = true;
        } else if (arg == "--logfile") {
            config.useLogFile = true;
            if (i + 1 < argc && argv[i + 1][0] != '-') {
                config.logFile = argv[++i];
            }
        } else if (arg == "--threads" && i + 1 < argc) {
            config.numThreads = std::atoi(argv[++i]);
            if (config.numThreads <= 0) {
                config.numThreads = std::thread::hardware_concurrency();
            }
        } else if (arg == "--pool-address" && i + 1 < argc) {
            config.poolAddress = argv[++i];
        } else if (arg == "--pool-port" && i + 1 < argc) {
            config.poolPort = argv[++i];
        } else if (arg == "--wallet" && i + 1 < argc) {
            config.walletAddress = argv[++i];
        } else if (arg == "--worker-name" && i + 1 < argc) {
            config.workerName = argv[++i];
        } else if (arg == "--password" && i + 1 < argc) {
            config.password = argv[++i];
        } else if (arg == "--user-agent" && i + 1 < argc) {
            config.userAgent = argv[++i];
        }
    }
    return true;
}

void signalHandler(int signum) {
    threadSafePrint("Received signal " + std::to_string(signum) + ", shutting down...", false);
    shouldStop = true;
}

int main(int argc, char* argv[]) {
    if (!parseCommandLine(argc, argv)) {
        return 1;
    }

    if (!validateConfig()) {
        printHelp();
        return 1;
    }

    if (config.useLogFile) {
        logFile.open(config.logFile, std::ios::app);
        if (!logFile.is_open()) {
            threadSafePrint("Failed to open log file: " + config.logFile, false);
            return 1;
        }
    }

    signal(SIGINT, signalHandler);
    signal(SIGTERM, signalHandler);

    threadSafePrint("Initializing with " + std::to_string(config.numThreads) + " mining threads...", false);
    threadSafePrint("Waiting for dataset initialization...", false);

    if (!PoolClient::initialize()) {
        threadSafePrint("Failed to initialize network", false);
        return 1;
    }

    threadSafePrint("Resolving " + config.poolAddress + ":" + config.poolPort + "...", false);
    if (!PoolClient::connect(config.poolAddress, config.poolPort)) {
        threadSafePrint("Failed to connect to pool", false);
        PoolClient::cleanup();
        return 1;
    }

    threadSafePrint("Connected to pool.", false);

    std::string loginPayload = "{\"id\":1,\"jsonrpc\":\"2.0\",\"method\":\"login\",\"params\":{\"agent\":\"" + 
                              config.userAgent + "\",\"login\":\"" + config.walletAddress + 
                              "\",\"pass\":\"" + config.password + "\",\"worker\":\"" + config.workerName + "\"}}";
    
    threadSafePrint("Sending login payload: " + loginPayload, false);
    threadSafePrint("Pool send: " + loginPayload, false);

    if (!PoolClient::login(config.walletAddress, config.password, 
                          config.workerName, config.userAgent)) {
        threadSafePrint("Failed to send login request", false);
        PoolClient::cleanup();
        return 1;
    }

    threadSafePrint("Starting job listener thread...", false);
    threadSafePrint("Starting mining threads...", false);

    std::thread jobListener(PoolClient::listenForNewJobs, PoolClient::poolSocket);
    std::thread statsMonitor(MiningStats::globalStatsMonitor);
    
    threadData.resize(config.numThreads);
    std::vector<std::thread> miningThreads;
    
    for (int i = 0; i < config.numThreads; i++) {
        threadData[i] = new MiningThreadData(i);
        miningThreads.emplace_back(miningThread, threadData[i]);
    }
    
    for (auto& thread : miningThreads) {
        thread.join();
    }
    
    jobListener.join();
    statsMonitor.join();
    
    for (auto data : threadData) {
        delete data;
    }
    
    PoolClient::cleanup();
    return 0;
} 