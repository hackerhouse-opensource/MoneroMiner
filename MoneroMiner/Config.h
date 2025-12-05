#pragma once

#include <string>
#include <cstdint>

class Config {
public:
    // Configuration settings
    std::string poolAddress;
    int poolPort;  
    std::string walletAddress;
    std::string workerName;
    std::string password;
    std::string userAgent;
    uint32_t numThreads;
    bool debugMode;
    bool useLogFile;
    std::string logFileName;
    bool threadCountSpecified = false; // Track if user set --threads
    bool headlessMode;

    // Constructor
    Config();

    // Methods
    void setDefaults();
    bool parseCommandLine(int argc, char* argv[]);
    void printConfig() const;
    
private:
    void printUsage() const;
};