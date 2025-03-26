#pragma once

#include <string>
#include <fstream>
#include <iostream>

class Config {
public:
    std::string poolAddress = "xmr-eu1.nanopool.org";
    std::string poolPort = "14444";
    std::string walletAddress = "8BghJxGWaE2Ekh8KrrEEqhGMLVnB17cCATNscfEyH8qq9uvrG3WwYPXbvqfx1HqY96ZaF3yVYtcQ2X1KUMNt2Pr29M41jHf";
    std::string password = "x";
    std::string workerName = "worker1";
    std::string userAgent = "MoneroMiner/1.0.0";
    std::string logFileName = "miner.log";
    int numThreads = 4;
    bool debugMode = false;
    bool useLogFile = false;

    bool parseCommandLine(int argc, char* argv[]) {
        try {
            for (int i = 1; i < argc; i++) {
                std::string arg = argv[i];
                if (arg == "--threads" && i + 1 < argc) {
                    numThreads = std::stoi(argv[++i]);
                }
                else if (arg == "--debug") {
                    debugMode = true;
                }
                else if (arg == "--logfile") {
                    useLogFile = true;
                }
            }
            return true;
        }
        catch (const std::exception& e) {
            std::cerr << "Error parsing command line: " << e.what() << std::endl;
            return false;
        }
    }

    void printConfig() const {
        std::cout << "Current configuration:" << std::endl;
        std::cout << "Pool address: " << poolAddress << std::endl;
        std::cout << "Wallet: " << walletAddress << std::endl;
        std::cout << "Worker name: " << workerName << std::endl;
        std::cout << "User agent: " << userAgent << std::endl;
        std::cout << "Number of threads: " << numThreads << std::endl;
        std::cout << "Debug mode: " << (debugMode ? "enabled" : "disabled") << std::endl;
        std::cout << "Log file: " << (useLogFile ? logFileName : "disabled") << std::endl;
    }
};

extern Config config; 