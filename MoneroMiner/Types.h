#pragma once

#include <string>
#include <vector>
#include <atomic>
#include <chrono>
#include <mutex>
#include <cstdint>
#include <array>

// Forward declaration of formatHashrate function
std::string formatHashrate(double hashrate);

// Global statistics structure
struct GlobalStats {
    std::atomic<uint64_t> totalHashes{0};
    std::atomic<uint64_t> acceptedShares{0};
    std::atomic<uint64_t> rejectedShares{0};
    std::atomic<uint64_t> totalShares{0};
    std::atomic<double> currentHashrate{0.0};
    std::atomic<int> elapsedSeconds{0};
    std::string currentJobId;
    std::atomic<uint32_t> currentNonce{0};
    std::chrono::steady_clock::time_point startTime;
};

// Mining statistics structure for individual threads
struct ThreadMiningStats {
    std::chrono::steady_clock::time_point startTime;
    uint64_t totalHashes;
    uint64_t acceptedShares;
    uint64_t rejectedShares;
    uint64_t currentHashrate;
    uint64_t runtime;
    std::mutex statsMutex;
};

struct uint256_t {
    std::array<uint64_t, 4> words;  // Using std::array instead of raw array

    uint256_t() : words{0, 0, 0, 0} {}  // Initialize all words to 0

    // Array access operator
    uint64_t& operator[](size_t index) {
        return words[index];
    }
    
    const uint64_t& operator[](size_t index) const {
        return words[index];
    }

    // Comparison operators
    bool operator<=(const uint256_t& other) const {
        for (int i = 3; i >= 0; i--) {
            if (words[i] < other.words[i]) return true;
            if (words[i] > other.words[i]) return false;
        }
        return true;
    }

    bool operator<(const uint256_t& other) const {
        for (int i = 3; i >= 0; i--) {
            if (words[i] < other.words[i]) return true;
            if (words[i] > other.words[i]) return false;
        }
        return false;
    }

    bool operator>(const uint256_t& other) const {
        return !(*this <= other);
    }

    // Clear method to replace memset
    void clear() {
        words.fill(0);
    }
}; 