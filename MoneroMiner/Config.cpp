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
    poolAddress = "xmr-us-east1.nanopool.org";
    poolPort = 10300;
    walletAddress = "48edfHu7V9Z84YzzMa6fUueoELZ9ZRXq9VetWzYGzKt52XU5xvqgzYnDK9URnRoJMk1j8nLwEVsaSWJ4fhdUyZijBGUicoD"; // Default test wallet
    workerName = "worker1";
    password = "x";  // FIX: SupportXMR requires non-empty password
    userAgent = "MoneroMiner/1.0.0";
    numThreads = 1;
    debugMode = false;  // This should be overridden by --debug flag
    useLogFile = false;
    logFileName = "monerominer.log";
}

bool Config::parseCommandLine(int argc, char* argv[]) {
    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        if (arg == "--help" || arg == "-h") {
            printUsage();
            return false;  // Let the main program handle help
        }
        else if (arg == "--debug") {
            debugMode = true;
        }
        else if (arg == "--logfile") {
            useLogFile = true;
        }
        else if (arg == "--threads" && i + 1 < argc) {
            numThreads = std::stoi(argv[++i]);
        }
        else if (arg == "--pool" && i + 1 < argc) {
            std::string poolStr = argv[++i];
            size_t colonPos = poolStr.find(':');
            if (colonPos != std::string::npos) {
                poolAddress = poolStr.substr(0, colonPos);
                poolPort = std::stoi(poolStr.substr(colonPos + 1));
            } else {
                poolAddress = poolStr;
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
    std::cout << "Pool address: " << poolAddress << ":" << poolPort << std::endl;
    std::cout << "Wallet: " << walletAddress << std::endl;
    std::cout << "Worker name: " << workerName << std::endl;
    std::cout << "User agent: " << userAgent << std::endl;
    std::cout << "Number of threads: " << numThreads << std::endl;
    std::cout << "Debug mode: " << (debugMode ? "enabled" : "disabled") << std::endl;
    std::cout << "Log file: " << logFileName << std::endl;
    
    if (debugMode) {
        std::cout << "\nNote: At pool difficulty ~480,000:" << std::endl;
        std::cout << "  - 1 thread (~400 H/s):  expect share every ~20 minutes" << std::endl;
        std::cout << "  - 24 threads (~9600 H/s): expect share every ~50 seconds" << std::endl;
        std::cout << "  - 48 threads (~19200 H/s): expect share every ~25 seconds" << std::endl;
    }
    std::cout << std::endl;
}

void Config::printUsage() const {
    std::cout << "MoneroMiner - Monero CPU Miner" << std::endl;
    std::cout << "\nUsage: MoneroMiner [options]" << std::endl;
    std::cout << "\nOptions:" << std::endl;
    std::cout << "  --help                 Show this help message" << std::endl;
    std::cout << "  --debug                Enable debug output" << std::endl;
    std::cout << "  --logfile              Enable logging to file" << std::endl;
    std::cout << "  --threads N            Number of mining threads" << std::endl;
    std::cout << "  --pool ADDRESS:PORT    Pool address and port" << std::endl;
    std::cout << "  --wallet ADDRESS       Your Monero wallet address" << std::endl;
    std::cout << "  --worker NAME          Worker name" << std::endl;
    std::cout << "  --password PASS        Pool password (default: x)" << std::endl;
    std::cout << "\nExample:" << std::endl;
    std::cout << "  MoneroMiner.exe --wallet YOUR_WALLET --threads 4" << std::endl;
}