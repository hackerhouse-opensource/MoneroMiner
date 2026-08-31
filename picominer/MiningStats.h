#pragma once

#include <atomic>
#include <cstdint>

// Simple namespace for global mining statistics
namespace MiningStatsUtil {
    extern std::atomic<uint64_t> acceptedShares;
    extern std::atomic<uint64_t> rejectedShares;
    
    // Stats monitoring thread (runs in background)
    void globalStatsMonitor();
}