#pragma once

#include <cstdint>

// Mining-specific constants
namespace MiningConstants {
    // Core mining parameters
    static constexpr int NONCE_SIZE = 4;
    static constexpr int NONCE_OFFSET = 39;
    static constexpr int MAX_BLOB_SIZE = 128;
    static constexpr int HASH_SIZE = 32;
    static constexpr int JOB_ID_SIZE = 32;
    
    // Performance and timing constants
    static constexpr int STATS_INTERVAL_MS = 2500;
    static constexpr int MAX_MINING_THREADS = 64;
    static constexpr int THREAD_PAUSE_TIME = 100;  // Milliseconds to pause when no jobs available
    static constexpr int HASHRATE_AVERAGING_WINDOW_SIZE = 60;  // 60 seconds
    static constexpr int JOB_QUEUE_SIZE = 2;
    static constexpr int SHARE_SUBMISSION_RETRIES = 3;
}

// RandomX algorithm constants
namespace RandomXConstants {
    static constexpr int DATASET_ITEM_SIZE = 64;
    static constexpr uint64_t DATASET_BASE_SIZE = 2147483648ULL;  // 2GB for Fast Mode
    static constexpr uint64_t DATASET_EXTRA_SIZE = 33554368ULL;   // 32MB
    static constexpr uint64_t SCRATCHPAD_L3 = 2097152ULL;        // 2MB
    static constexpr uint64_t CACHE_BASE_SIZE = 2147483648ULL;   // 2GB for Fast Mode
    static constexpr int CACHE_ACCESSES = 4;
}

// Network constants
namespace NetworkConstants {
    static constexpr int DEFAULT_POOL_PORT = 3333;
    static constexpr int SOCKET_TIMEOUT_SEC = 30;
    static constexpr int MAX_RECEIVE_BUFFER = 8192;
    static constexpr int MAX_SEND_BUFFER = 4096;
}

// Default configuration values
namespace DefaultConfig {
    static constexpr bool DEBUG_MODE = false;
    static constexpr bool USE_LOG_FILE = false;
    static constexpr const char* LOG_FILE = "miner.log";
    static constexpr const char* USER_AGENT = "MoneroMiner/1.0.0";
    static constexpr const char* DEFAULT_PASSWORD = "x";
}

// RandomX feature flags
#ifndef RANDOMX_FEATURE_JIT
#define RANDOMX_FEATURE_JIT 1
#endif

// RandomX configuration
#define RANDOMX_DATASET_ITEM_SIZE 64
#define RANDOMX_DATASET_BASE_SIZE 2147483648  // 2GB for Fast Mode
#define RANDOMX_DATASET_EXTRA_SIZE 33554368   // 32MB
#define RANDOMX_SCRATCHPAD_L3 2097152        // 2MB
#define RANDOMX_CACHE_BASE_SIZE 2147483648   // 2GB for Fast Mode
#define RANDOMX_CACHE_ACCESSES 4

// Mining configuration
#define JOB_QUEUE_SIZE 2
#define HASHRATE_AVERAGING_WINDOW_SIZE 60  // 60 seconds
#define THREAD_PAUSE_TIME 100              // Milliseconds to pause when no jobs available 