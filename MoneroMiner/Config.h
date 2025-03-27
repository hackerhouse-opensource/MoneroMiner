#pragma once

#include <string>
#include <fstream>
#include <iostream>
#include <thread>

struct Config {
    std::string poolAddress;
    std::string poolPort;
    std::string walletAddress;
    std::string workerName;
    std::string password;
    std::string userAgent;
    int numThreads;
    bool debugMode;
    bool useLogFile;
    std::string logFileName;

    Config() : numThreads(0), debugMode(false), useLogFile(false) {
        // Set default values
        poolAddress = "xmr-eu1.nanopool.org";
        poolPort = "14444";
        walletAddress = "8BghJxGWaE2Ekh8KrrEEqhGMLVnB17cCATNscfEyH8qq9uvrG3WwYPXbvqfx1HqY96ZaF3yVYtcQ2X1KUMNt2Pr29M41jHf";
        workerName = "worker1";
        userAgent = "MoneroMiner/1.0.0";
        password = "x";
        logFileName = "miner.log";
    }

    bool parseCommandLine(int argc, char* argv[]) {
        for (int i = 1; i < argc; i++) {
            std::string arg = argv[i];
            if (arg == "--help" || arg == "-h") {
                return false;
            }
            else if (arg == "--debug") {
                debugMode = true;
            }
            else if (arg == "--logfile") {
                useLogFile = true;
                if (i + 1 < argc && argv[i + 1][0] != '-') {
                    logFileName = argv[++i];
                }
            }
            else if (arg == "--threads" && i + 1 < argc) {
                numThreads = std::stoi(argv[++i]);
            }
            else if (arg == "--pool" && i + 1 < argc) {
                std::string poolArg = argv[++i];
                size_t colonPos = poolArg.find(':');
                if (colonPos != std::string::npos) {
                    poolAddress = poolArg.substr(0, colonPos);
                    poolPort = poolArg.substr(colonPos + 1);
                } else {
                    poolAddress = poolArg;
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

        // Validate and set thread count
        if (numThreads <= 0) {
            numThreads = std::thread::hardware_concurrency();
            if (numThreads == 0) {
                numThreads = 4;
            }
        }

        // Validate required fields
        if (walletAddress.empty()) {
            std::cerr << "Error: Wallet address is required" << std::endl;
            return false;
        }

        return true;
    }

    void printConfig() {
        std::cout << "Current configuration:" << std::endl;
        std::cout << "Pool address: " << poolAddress << ":" << poolPort << std::endl;
        std::cout << "Wallet: " << walletAddress << std::endl;
        std::cout << "Worker name: " << workerName << std::endl;
        std::cout << "User agent: " << userAgent << std::endl;
        std::cout << "Number of threads: " << numThreads << std::endl;
        std::cout << "Debug mode: " << (debugMode ? "enabled" : "disabled") << std::endl;
        std::cout << "Log file: " << (useLogFile ? logFileName : "disabled") << std::endl;
    }
};

extern Config config; 