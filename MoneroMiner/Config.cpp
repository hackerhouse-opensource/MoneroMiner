#include "Config.h"
#include "Globals.h"
#include <iostream>
#include <string>
#include <thread>
#include <sstream>

Config config;

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
            std::string poolArg = argv[++i];
            size_t colonPos = poolArg.find(':');
            if (colonPos != std::string::npos) {
                poolAddress = poolArg.substr(0, colonPos);
                poolPort = std::stoi(poolArg.substr(colonPos + 1));
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