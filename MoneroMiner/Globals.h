#pragma once

#include <string>
#include <fstream>
#include <mutex>
#include <atomic>

// Global variables
extern bool debugMode;
extern std::atomic<bool> shouldStop;
extern std::ofstream logFile;
extern std::mutex consoleMutex;
extern std::mutex logfileMutex; 