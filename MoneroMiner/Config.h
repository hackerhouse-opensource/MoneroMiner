#pragma once

#include "Types.h"

struct Config {
    bool debugMode = false;
    bool useLogFile = false;
    std::string logFile = "miner.log";
    int numThreads = 0;
    std::string poolAddress = "xmr-eu1.nanopool.org";
    std::string poolPort = "10300";
    std::string walletAddress = "8BghJxGWaE2Ekh8KrrEEqhGMLVnB17cCATNscfEyH8qq9uvrG3WwYPXbvqfx1HqY96ZaF3yVYtcQ2X1KUMNt2Pr29M41jHf";
    std::string workerName = "worker1";
    std::string password = "x";
    std::string userAgent = "MoneroMiner/1.0.0";
};

extern Config config; 