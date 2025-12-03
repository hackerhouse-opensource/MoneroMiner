#include "Config.h"
#include "Globals.h"
#include <iostream>
#include <string>
#include <thread>
#include <sstream>
#include <fstream>
#include "Utils.h"

Config::Config() {
    setDefaults();
}

void Config::setDefaults() {
    poolAddress = "xmr-eu1.nanopool.org:14444";
    walletAddress = "48edfHu7V9Z84YzzMa6fUueoELZ9ZRXq9VetWzYGzKt52XU5xvqgzYnDK9URnRoJMk1j8nLwEVsaSWJ4fhdUyZijBGUicoD"; // Default test wallet
    workerName = "worker1";
    userAgent = "MoneroMiner/1.0.0";
    numThreads = std::thread::hardware_concurrency();
    debugMode = false;
    useLogFile = false;
    logFileName = "monerominer.log";
}

bool Config::parseCommandLine(int argc, char* argv[]) {
    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        if (arg == "--help" || arg == "-h") {
            return false;  // Let the main program handle help
        }
        else if (arg == "--debug") {
            debugMode = true;
        }
        else if (arg == "--logfile") {
            useLogFile = true;
            logFileName = "miner.log";
        }
        else if (arg == "--threads" && i + 1 < argc) {
            int threads = std::stoi(argv[++i]);
            if (threads > 0) {
                numThreads = threads;
            }
        }
        else if (arg == "--pool" && i + 1 < argc) {
            poolAddress = argv[++i];
            // Remove any :0 suffix if present
            size_t pos = poolAddress.find(":0");
            if (pos != std::string::npos) {
                poolAddress = poolAddress.substr(0, pos);
            }
        }
        else if (arg == "--wallet" && i + 1 < argc) {
            walletAddress = argv[++i];
        }
        else if (arg == "--worker" && i + 1 < argc) {
            workerName = argv[++i];
        }
        else if (arg == "--password" && i + 1 < argc) {
            password = argv[++i];
        }
        else if (arg == "--useragent" && i + 1 < argc) {
            userAgent = argv[++i];
        }
    }
    return true;
}

bool validateConfig(const Config& config) {
    if (config.walletAddress.empty()) {
        std::cerr << "Error: Wallet address is required" << std::endl;
        return false;
    }

    if (config.numThreads <= 0) {
        std::cerr << "Error: Invalid thread count" << std::endl;
        return false;
    }

    if (config.poolPort <= 0) {
        std::cerr << "Error: Invalid pool port" << std::endl;
        return false;
    }

    return true;
}

void Config::printConfig() const {
    std::cout << "Current configuration:" << std::endl;
    std::cout << "Pool address: " << poolAddress << std::endl;
    std::cout << "Wallet: " << walletAddress << std::endl;
    std::cout << "Worker name: " << workerName << std::endl;
    std::cout << "User agent: " << userAgent << std::endl;
    std::cout << "Number of threads: " << numThreads << std::endl;
    std::cout << "Debug mode: " << (debugMode ? "enabled" : "disabled") << std::endl;
    std::cout << "Log file: " << logFileName << std::endl;
} 