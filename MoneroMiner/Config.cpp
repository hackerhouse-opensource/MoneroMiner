#include "Config.h"
#include "Globals.h"
#include "Platform.h"  // Replace windows.h with Platform.h
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
    // Current pool (high difficulty):
    poolAddress = "xmr-us-east1.nanopool.org";
    poolPort = 10300;
    walletAddress = "488XamY1RKhUVpHPirdcXwb9ePGjGrcNoi8FA2MHDWCrYEfpz1ansYR4gUuhkjDVWR2rmgitM9LtZUXx4SrBSvPB9smskV8"; // Default test wallet
    workerName = "worker1";
    password = "x";  // Some pools require non-empty password
    userAgent = "MoneroMiner/1.0.0";
    numThreads = 1;
    debugMode = false;  // This should be overridden by --debug flag
    useLogFile = false;
    logFileName = "monerominer.log";
    headlessMode = false; // Initialize headless mode flag
}

bool Config::parseCommandLine(int argc, char* argv[]) {
    bool threadCountSpecified = false; // Track if user specified threads

    // Parse all arguments first
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
            threadCountSpecified = true; // Track if user specified threads
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
        else if (arg == "--headless") {
            headlessMode = true;
            useLogFile = true; // Force log file in headless mode
        }
    }
    
    // ONLY auto-detect if user did NOT specify --threads
    if (!threadCountSpecified && numThreads <= 1) {
        unsigned int logicalProcessors = Platform::getLogicalProcessors();
        
        numThreads = logicalProcessors;
        
        // Leave 2-4 threads for system based on total count
        if (logicalProcessors >= 24) {
            numThreads = logicalProcessors - 4; // e.g., 24 -> 20 threads
        } else if (logicalProcessors >= 16) {
            numThreads = logicalProcessors - 2; // e.g., 16 -> 14 threads
        } else if (logicalProcessors >= 8) {
            numThreads = logicalProcessors - 1; // e.g., 8 -> 7 threads
        }
        // else use all threads for < 8 cores
        
        std::cout << "Auto-detected " << logicalProcessors 
                  << " logical processors, using " << numThreads << " mining threads" << std::endl;
    }
    
    // Set default worker name based on machine name if not specified
    if (workerName.empty() || workerName == "worker1") {
        std::string computerName = Platform::getComputerName();
        workerName = computerName;
        
        // Sanitize: lowercase and remove invalid chars
        for (char& c : workerName) {
            if (!std::isalnum(c)) c = '_';
            else c = std::tolower(c);
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
    std::cout << "  --headless             Enable headless mode (no GUI)" << std::endl;
    std::cout << "\nExample:" << std::endl;
    std::cout << "  MoneroMiner.exe --wallet YOUR_WALLET --threads 4" << std::endl;
}