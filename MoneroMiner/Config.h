#pragma once

#include <string>
#include <fstream>

struct Config {
    std::string poolAddress = "xmr-eu1.nanopool.org";
    std::string poolPort = "10300";
    std::string walletAddress = "8BghJxGWaE2Ekh8KrrEEqhGMLVnB17cCATNscfEyH8qq9uvrG3WwYPXbvqfx1HqY96ZaF3yVYtcQ2X1KUMNt2Pr29M41jHf";
    std::string workerName = "worker1";
    std::string password = "x";
    std::string userAgent = "MoneroMiner/1.0.0";
    int numThreads = 4;
    bool debugMode = false;
    bool useLogFile = false;
    std::string logFileName = "miner.log";
};

extern Config config; 