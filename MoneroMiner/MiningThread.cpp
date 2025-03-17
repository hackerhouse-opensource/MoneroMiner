void miningThread(MiningThreadData* data) {
    threadSafePrint("Mining thread " + std::to_string(data->getId()) + " started");
    
    // Initialize RandomX VM for this thread
    threadSafePrint("Initializing VM for thread " + std::to_string(data->getId()));
    if (!RandomXManager::initializeVM(data->getId())) {
        threadSafePrint("Failed to initialize VM for thread " + std::to_string(data->getId()));
        return;
    }
    
    // Debug VM state
    if (config.debugMode) {
        threadSafePrint("RandomX VM State for thread " + std::to_string(data->getId()) + ":");
        threadSafePrint("  Dataset Size: " + std::to_string(RANDOMX_DATASET_SIZE) + " bytes");
        threadSafePrint("  Dataset Items: " + std::to_string(RANDOMX_DATASET_ITEM_COUNT));
        threadSafePrint("  Item Size: " + std::to_string(RANDOMX_DATASET_ITEM_SIZE) + " bytes");
    }
    
    threadSafePrint("VM initialized successfully for thread " + std::to_string(data->getId()));
    
    // Mark thread as running
    data->isRunning = true;
    MiningStats::updateThreadStats(data);
    
    while (!data->shouldStop) {
        try {
            Job job;
            {
                std::unique_lock<std::mutex> lock(data->jobMutex);
                data->jobCondition.wait(lock, [data]() {
                    return !data->jobQueue.empty() || data->shouldStop;
                });
                if (data->shouldStop) break;
                job = data->jobQueue.front();
                data->jobQueue.pop();
            }

            // Initialize target and nonce
            std::vector<uint8_t> targetBytes = hexToBytes(job.target);
            uint32_t compactTarget = (targetBytes[0] << 24) | (targetBytes[1] << 16) | 
                                   (targetBytes[2] << 8) | targetBytes[3];
            
            // Convert compact target to 256-bit target
            uint32_t exponent = compactTarget >> 24;
            uint32_t mantissa = compactTarget & 0x00FFFFFF;
            
            if (config.debugMode) {
                std::stringstream ss;
                ss << "Converting compact target: " << job.target << std::endl;
                ss << "Compact target components - Exponent: " << exponent << ", Mantissa: 0x" 
                   << std::hex << mantissa << std::endl;
                threadSafePrint(ss.str());
            }

            // Calculate the actual target
            uint256_t target;
            if (exponent >= 3) {
                // For exponents >= 3, we can directly shift the mantissa
                target = uint256_t(mantissa) << (8 * (exponent - 3));
            } else {
                // For smaller exponents, we need to handle the case where the mantissa
                // would be shifted right
                target = uint256_t(mantissa) >> (8 * (3 - exponent));
            }

            if (config.debugMode) {
                std::stringstream ss;
                ss << "Converted to 256-bit target: " << target << std::endl;
                threadSafePrint(ss.str());
            }

            // Set nonce to 0 for this job
            uint32_t nonce = 0;

            // Debug output for first hash calculation
            if (config.debugMode) {
                std::stringstream ss;
                ss << "Starting first hash calculation:" << std::endl;
                ss << "  Job ID: " << job.jobId << std::endl;
                ss << "  Height: " << job.height << std::endl;
                ss << "  Target: 0x" << job.target << std::endl;
                ss << "  Blob: " << job.blob << std::endl;
                ss << "  Seed Hash: " << job.seedHash << std::endl;
                ss << "  Difficulty: " << job.difficulty << std::endl;
                threadSafePrint(ss.str());
            }

            // Prepare input for hashing
            std::vector<uint8_t> input = hexToBytes(job.blob);
            if (input.size() != 76) {
                threadSafePrint("Error: Invalid blob size");
                continue;
            }

            // Set nonce in big-endian order
            input[39] = (nonce >> 24) & 0xFF;
            input[40] = (nonce >> 16) & 0xFF;
            input[41] = (nonce >> 8) & 0xFF;
            input[42] = nonce & 0xFF;

            // Calculate first hash
            uint256_t hash = calculateHash(input);

            if (config.debugMode) {
                std::stringstream ss;
                ss << "First hash result:" << std::endl;
                ss << "  Hash: 0x" << hash << std::endl;
                ss << "  Target: 0x" << target << std::endl;
                ss << "  Meets target: " << (hash <= target ? "Yes" : "No") << std::endl;
                threadSafePrint(ss.str());
            }

            // Update thread stats
            data->hashes++;
            data->lastUpdate = std::chrono::steady_clock::now();
            MiningStats::updateThreadStats(data);

            // Update global stats
            MiningStats::updateGlobalStats(data);

            // Main mining loop
            while (!data->shouldStop && job.jobId == data->currentJobId) {
                // Increment nonce
                nonce++;

                // Set nonce in big-endian order
                input[39] = (nonce >> 24) & 0xFF;
                input[40] = (nonce >> 16) & 0xFF;
                input[41] = (nonce >> 8) & 0xFF;
                input[42] = nonce & 0xFF;

                // Calculate hash
                hash = calculateHash(input);

                // Update stats
                data->hashes++;

                // Check if we need to update stats
                auto now = std::chrono::steady_clock::now();
                if (now - data->lastUpdate >= std::chrono::seconds(1)) {
                    data->lastUpdate = now;
                    MiningStats::updateThreadStats(data);
                    MiningStats::updateGlobalStats(data);
                }

                // Check if hash meets target
                if (hash <= target) {
                    // Found a valid share
                    std::stringstream ss;
                    ss << "Found share!" << std::endl;
                    ss << "  Hash: 0x" << hash << std::endl;
                    ss << "  Target: 0x" << target << std::endl;
                    ss << "  Nonce: 0x" << std::hex << nonce << std::endl;
                    threadSafePrint(ss.str());

                    // Submit share
                    submitShare(job, nonce);
                }
            }
        }
        catch (const std::exception& e) {
            threadSafePrint("Mining thread " + std::to_string(data->getId()) + " error: " + e.what(), true);
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
    }
    
    data->isRunning = false;
    threadSafePrint("Mining thread " + std::to_string(data->getId()) + " stopped");
} 