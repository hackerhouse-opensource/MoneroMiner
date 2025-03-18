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
            if (targetBytes.size() != 4) {
                threadSafePrint("Error: Invalid target size", true);
                continue;
            }

            // Create 256-bit target (32 bytes)
            std::vector<uint8_t> expandedTarget(32, 0);

            // Place the compact target in the least significant 4 bytes (big-endian)
            expandedTarget[28] = targetBytes[0];
            expandedTarget[29] = targetBytes[1];
            expandedTarget[30] = targetBytes[2];
            expandedTarget[31] = targetBytes[3];

            if (config.debugMode) {
                std::stringstream ss;
                ss << "[" << getCurrentTimestamp() << "] randomx  new job:" << std::endl;
                ss << "  Height: " << job.height << std::endl;
                ss << "  Target: 0x" << job.target << std::endl;
                ss << "  Target bytes: " << bytesToHex(targetBytes) << std::endl;
                ss << "  Expanded target: " << bytesToHex(expandedTarget) << std::endl;
                ss << "  Difficulty: " << job.difficulty << std::endl;
                ss << "  Blob: " << job.blob << std::endl;
                ss << "  Seed Hash: " << job.seedHash << std::endl;
                threadSafePrint(ss.str(), true);
            }

            // Set nonce to 0 for this job
            uint32_t nonce = 0;

            // Prepare input for hashing
            std::vector<uint8_t> input = hexToBytes(job.blob);
            if (input.size() != 76) {
                threadSafePrint("Error: Invalid blob size", true);
                continue;
            }

            // Set nonce in big-endian order
            input[39] = (nonce >> 24) & 0xFF;
            input[40] = (nonce >> 16) & 0xFF;
            input[41] = (nonce >> 8) & 0xFF;
            input[42] = nonce & 0xFF;

            if (config.debugMode) {
                std::stringstream ss;
                ss << "[" << getCurrentTimestamp() << "] randomx  first hash:" << std::endl;
                ss << "  Input: " << bytesToHex(input) << std::endl;
                ss << "  Nonce: 0x" << std::hex << nonce << std::endl;
                threadSafePrint(ss.str(), true);
            }

            // Calculate first hash
            uint256_t hash = calculateHash(input);

            if (config.debugMode) {
                std::stringstream ss;
                ss << "[" << getCurrentTimestamp() << "] randomx  hash result:" << std::endl;
                ss << "  Hash: 0x" << std::hex << hash << std::endl;
                ss << "  Target: 0x" << bytesToHex(expandedTarget) << std::endl;
                ss << "  Meets target: " << (meetsTarget(hash, expandedTarget) ? "Yes" : "No") << std::endl;
                threadSafePrint(ss.str(), true);
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
                if (meetsTarget(hash, expandedTarget)) {
                    // Found a valid share
                    std::stringstream ss;
                    ss << "Found share!" << std::endl;
                    ss << "  Hash: 0x" << std::hex << hash << std::endl;
                    ss << "  Target: 0x" << bytesToHex(expandedTarget) << std::endl;
                    ss << "  Nonce: 0x" << std::hex << nonce << std::endl;
                    threadSafePrint(ss.str(), true);

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