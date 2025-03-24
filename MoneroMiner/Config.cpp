bool validateConfig(const Config& config) {
    if (config.walletAddress.empty()) {
        threadSafePrint("Error: Wallet address is required\n", false);
        return false;
    }

    if (config.threadCount <= 0) {
        threadSafePrint("Error: Invalid thread count\n", false);
        return false;
    }

    if (config.poolPort <= 0) {
        threadSafePrint("Error: Invalid pool port\n", false);
        return false;
    }

    return true;
}

bool parseCommandLine(int argc, char* argv[], Config& config) {
    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        if (arg == "--help") {
            printHelp();
            return false;
        }
        else if (arg == "--wallet" && i + 1 < argc) {
            config.walletAddress = argv[++i];
        }
        else if (arg == "--threads" && i + 1 < argc) {
            config.threadCount = std::stoi(argv[++i]);
        }
        else if (arg == "--pool-address" && i + 1 < argc) {
            config.poolAddress = argv[++i];
        }
        else if (arg == "--pool-port" && i + 1 < argc) {
            config.poolPort = std::stoi(argv[++i]);
        }
        else if (arg == "--worker-name" && i + 1 < argc) {
            config.workerName = argv[++i];
        }
        else if (arg == "--debug") {
            config.debugMode = true;
        }
        else if (arg == "--logfile") {
            config.logFile = true;
            if (i + 1 < argc && argv[i + 1][0] != '-') {
                config.logFileName = argv[++i];
            }
        }
    }

    return validateConfig(config);
} 