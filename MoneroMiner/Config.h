#pragma once

#include <string>
#include <fstream>
#include <iostream>
#include <thread>
#include "Globals.h"

class Config {
public:
    std::string poolAddress;
    int poolPort;
    std::string walletAddress;
    std::string workerName;
    std::string password;
    std::string userAgent;
    int numThreads;
    std::string logFileName;
    bool debugMode;
    bool useLogFile;

    Config() : 
        poolAddress("xmr-eu1.nanopool.org"),
        poolPort(14444),
        walletAddress("8BghJxGWaE2Ekh8KrrEEqhGMLVnB17cCATNscfEyH8qq9uvrG3WwYPXbvqfx1HqY96ZaF3yVYtcQ2X1KUMNt2Pr29M41jHf"),
        workerName("worker1"),
        password("x"),
        userAgent("MoneroMiner/1.0.0"),
        numThreads(1),
        logFileName("monerominer.log"),
        debugMode(false),
        useLogFile(true) {}

    bool parseCommandLine(int argc, char* argv[]);

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