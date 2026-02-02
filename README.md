# MoneroMiner v1.0.0

A lightweight, high-performance Monero (XMR) CPU miner using the RandomX proof-of-work algorithm. Designed for maximum efficiency and cross-platform compatibility.

**Author:** [Hacker Fantastic](https://hacker.house)  
**License:** [CC BY-NC-ND 4.0](https://creativecommons.org/licenses/by-nc-nd/4.0/)  
**Documentation:** [Complete API & Implementation Docs](https://deepwiki.com/hackerhouse-opensource/MoneroMiner)

---

## Features

**Core Capabilities:**

- Multi-threaded mining with per-thread nonce ranges
- Full stratum protocol with automatic reconnection
- Real-time hashrate monitoring and share tracking
- 2GB RandomX dataset caching to disk
- Debug mode with detailed hash comparisons
- Cross-platform: Windows and Linux on Intel, AMD, and ARM

**Advanced:**

- Background job management thread
- Huge pages support (10-30% hashrate boost)
- Share deduplication and stale detection
- Graceful shutdown and cleanup
- Headless mode (Windows)

---

## Platform Support

**Operating Systems:**

- Microsoft Windows 10/11 (x64)
- Linux (Ubuntu, Debian, Fedora, Arch, Raspberry Pi OS)

**CPU Architectures:**

- Intel x86/x86_64 (Core i3+, Xeon)
- AMD x86/x86_64 (Ryzen, EPYC, Threadripper)
- ARM 32-bit (ARMv7+, Cortex-A series)
- ARM 64-bit (ARMv8/AArch64, Cortex-A72+)

**Tested Systems:**

- AMD Ryzen 9 3900X (12-core): 9,000 H/s
- AMD Ryzen 24-core: 16,092 H/s
- Raspberry Pi 5 (4-core): 450 H/s

---

## Quick Start

### Windows

```powershell
# Clone repository
git clone https://github.com/hackerhouse-opensource/MoneroMiner.git
cd MoneroMiner

# Build (use build.ps1, NOT make)
.\build.ps1

# Run with your wallet address
x64\Release\MoneroMiner.exe --wallet YOUR_WALLET_ADDRESS
```

### Linux

```bash
# Clone repository
git clone https://github.com/hackerhouse-opensource/MoneroMiner.git
cd MoneroMiner

# Build (use make, NOT build.ps1)
make

# Run with your wallet address
bin/monerominer --wallet YOUR_WALLET_ADDRESS
```

---

## Configuration

### Command Line Options

```
MoneroMiner [OPTIONS]

Required:
  --wallet ADDRESS      Your Monero wallet address

Optional:
  --threads N          Mining threads (default: auto-detect)
  --pool ADDRESS:PORT  Pool (default: xmr-us-east1.nanopool.org:10300)
  --worker NAME        Worker ID (default: hostname)
  --password PASS      Pool password (default: x)
  --debug              Detailed logging
  --logfile            Log to file
  --headless           Hide console (Windows)
  --help               Show help
```

### Default Configuration

The default configuration is set in `Config.cpp` which can be over-written on the command-line or at compile time:

```cpp
// Default pool and settings (Config.cpp lines 13-25)
poolAddress = "xmr-us-east1.nanopool.org";
poolPort = 10300;
walletAddress = "8C6hFb4Buo6dYwJiZEaFhyYhZTJaR4NyXSBzKMF1BnNKMGD92yeaY3a9PxuWp9bhTAh6dAXwqyyLfFxaPRct7j81L8t4iK2";
workerName = "worker1";
password = "x";
numThreads = 1; // Auto-detected if not specified
```

---

## Mining Output

```
PS C:\Users\Fantastic\source\repos\MoneroMiner> .\x64\Release\MoneroMiner.exe
12/08/2025 (13:06:55.808) 1765220815: Platform sockets initialized
Auto-detected 24 logical processors, using 23 mining threads (leaving 1 thread for system)
12/08/2025 (13:06:55.809) 1765220815: === MoneroMiner v1.0.0 ===
CPU:          AMD Ryzen 9 3900X 12-Core Processor (24 threads) 64-bit AES AVX AVX2 VM
Memory:       19.3/47.9 GB (40%)
Motherboard:  Gigabyte Technology Co., Ltd. - X570 AORUS ELITE WIFI
Threads:      23
Algorithm:    RandomX (rx/0)
Privileges: elevated
Huge pages: enabled (2MB pages)
Current Configuration:
Pool Address: xmr-us-east1.nanopool.org:10300
Wallet: 8C6hFb4Buo6dYwJiZEaFhyYhZTJaR4NyXSBzKMF1BnNKMGD92yeaY3a9PxuWp9bhTAh6dAXwqyyLfFxaPRct7j81L8t4iK2
Worker Name: worker1
User Agent: MoneroMiner/1.0.0
Threads: 23
Debug Mode: No
Logfile: Disabled

12/08/2025 (13:06:55.812) 1765220815: Sockets initialized successfully
12/08/2025 (13:06:55.812) 1765220815: Connecting to xmr-us-east1.nanopool.org:10300
12/08/2025 (13:06:55.866) 1765220815: Connected to pool
12/08/2025 (13:06:55.866) 1765220815: Sending login request
12/08/2025 (13:06:56.079) 1765220816: Received login response
12/08/2025 (13:06:56.079) 1765220816: Session ID: 1
12/08/2025 (13:06:56.079) 1765220816: === INITIALIZING RANDOMX ===
12/08/2025 (13:06:56.079) 1765220816: Seed hash: 9f6d235b951eccc6b7d86ae928a73d405d989742490512749fcb59e640d284c8
12/08/2025 (13:06:56.079) 1765220816: Detected CPU flags: 0x0000002a
12/08/2025 (13:06:56.080) 1765220816: Large pages enabled in RandomX
12/08/2025 (13:06:56.080) 1765220816: Mode: FULL (2GB dataset)
12/08/2025 (13:06:56.080) 1765220816: Cache flags: 0x0000002b
12/08/2025 (13:06:56.080) 1765220816: VM/Dataset flags: 0x0000002f
12/08/2025 (13:06:56.564) 1765220816: Cache initialized with seed hash: 9f6d235b951eccc6...
12/08/2025 (13:06:56.564) 1765220816: Loading dataset from disk...
12/08/2025 (13:06:58.216) 1765220818: RandomX: allocated 2336 MB (2080+256) huge pages 100% +JIT +AES +FULL
12/08/2025 (13:06:58.217) 1765220818: Successfully logged in to pool
12/08/2025 (13:06:58.217) 1765220818: Worker: 8C6hFb4Buo6dYwJiZEaFhyYhZTJaR4NyXSBzKMF1BnNKMGD92yeaY3a9PxuWp9bhTAh6dAXwqyyLfFxaPRct7j81L8t4iK2.worker1
12/08/2025 (13:06:58.217) 1765220818: RandomX already initialized for seed hash
12/08/2025 (13:06:58.220) 1765220818: Initialized 23 mining threads
12/08/2025 (13:06:58.221) 1765220818: Mining started - Press Ctrl+C to stop
12/08/2025 (13:06:58.221) 1765220818: === MINER IS NOW RUNNING ===
12/08/2025 (13:06:58.221) 1765220818: Press Ctrl+C to stop mining
12/08/2025 (13:07:08.290) 1765220828: Hashrate: 9006.6 H/s | Difficulty: 480045 | Accepted: 0 | Rejected: 0
12/08/2025 (13:07:18.358) 1765220838: Hashrate: 8760.4 H/s | Difficulty: 480045 | Accepted: 0 | Rejected: 0
12/08/2025 (13:07:18.617) 1765220838: Share found! J: 18867 Nonce: ea3cbde9 Attempts: 1358
12/08/2025 (13:07:18.618) 1765220838: Hash: 69944ebac65955cf7b5454179e766f69f88bf8dc4dd0804071698c78b3080000
12/08/2025 (13:07:18.758) 1765220838: Share submitted - ACCEPTED (Total: 1)
```

---

## Technical Details

### RandomX Algorithm

- **Hash Function**: Blake2b-based with AES, integer/float math, conditional branches
- **Memory Hard**: 2GB dataset prevents ASIC/GPU advantages
- **CPU Optimized**: Designed for general-purpose CPUs
- **VM Execution**: Interprets random code sequences per hash

### Architecture

**Core Components:**

- **PoolClient**: Stratum protocol (JSON-RPC 2.0)
- **RandomXManager**: VM/dataset management
- **MiningThreadData**: Per-thread state and statistics
- **Job**: Work encapsulation (blob, target, difficulty)
- **Platform**: OS abstraction (sockets, CPU info, huge pages)

### Nonce Distribution

Each thread mines a unique 32-bit range:

```
Thread 0:  0x00000000 - 0x15555554
Thread 1:  0x15555555 - 0x2AAAAAAA
Thread 2:  0x2AAAAAAB - 0x3FFFFFFF
...
Thread 11: 0xEAAAAAAB - 0xFFFFFFFF
```

Nonces are 4-byte little-endian at blob offset 39-42.

### Dataset Caching

```
Windows: %USERPROFILE%\AppData\Local\MoneroMiner\randomx_<seedhash>.bin
Linux:   ~/.cache/monerominer/randomx_<seedhash>.bin
```

Size: 2,080,374,784 bytes (2GB per seed hash)

---

## Documentation

Complete implementation documentation: https://deepwiki.com/hackerhouse-opensource/MoneroMiner

Includes API reference, architecture diagrams, performance tuning, protocol specs, and debugging tips.

---

## Donations

If you find MoneroMiner useful, XMR donations support continued development:

```
8C6hFb4Buo6dYwJiZEaFhyYhZTJaR4NyXSBzKMF1BnNKMGD92yeaY3a9PxuWp9bhTAh6dAXwqyyLfFxaPRct7j81L8t4iK2
```

**Note:** This is the default wallet in `Config.cpp`. Mining without `--wallet YOUR_ADDRESS` donates to the project.
Thank you for your support!

---

## License

These files are available under the 3-clause BSD license.

**Third-Party:**

- RandomX: BSD 3-Clause (see RandomX/LICENSE)
- picojson: BSD 2-Clause
