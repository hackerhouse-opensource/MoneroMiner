#pragma once

#include <string>
#include <cstdint>

class Config {
public:
    // Configuration settings
    std::string poolAddress;
    uint16_t poolPort;
    std::string walletAddress;
    std::string workerName;
    std::string password;
    std::string userAgent;
    uint32_t numThreads;
    bool debugMode;
    bool useLogFile;
    std::string logFileName;

    // Constructor
    Config();

    // Methods
    void setDefaults();
    bool parseCommandLine(int argc, char* argv[]);
    void printConfig() const;
    
private:
    void printUsage() const;
}; 