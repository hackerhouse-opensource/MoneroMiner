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

**Requirements:**

- 64-bit CPU with 2+ cores
- 3 GB RAM minimum (2GB dataset + 1GB overhead)
- 500 MB disk space for dataset cache
- Network connection

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

# Build with Visual Studio
.\build.ps1

# Run with your wallet address
x64\Release\MoneroMiner.exe --wallet YOUR_WALLET_ADDRESS
```

### Linux

```bash
# Clone repository
git clone https://github.com/hackerhouse-opensource/MoneroMiner.git
cd MoneroMiner

# Build
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

The default configuration is set in `Config.cpp` and can be over-written on the command-line:

```cpp
// Default pool and settings (Config.cpp lines 13-25)
poolAddress = "xmr-us-east1.nanopool.org";
poolPort = 10300;
walletAddress = "8C6hFb4Buo6dYwJiZEaFhyYhZTJaR4NyXSBzKMF1BnNKMGD92yeaY3a9PxuWp9bhTAh6dAXwqyyLfFxaPRct7j81L8t4iK2";
workerName = "worker1";
password = "x";
numThreads = 1; // Auto-detected if not specified
```

## Change the walletAddress to mine to your own wallet.

## Building from Source

See [BUILD.md](BUILD.md) for complete build instructions, performance optimization, and troubleshooting.

**Quick build:**

```bash
# Windows (Visual Studio)
.\build.ps1

# Linux (Make)
make
```

---

## Mining Output

```
=== MoneroMiner v1.0.0 ===
CPU:          AMD Ryzen 9 3900X 12-Core Processor (24 threads) 64-bit AES AVX AVX2 VM
Memory:       7.2/31.9 GB (23%)
Motherboard:  ASUSTeK Computer INC. - ROG CROSSHAIR VIII HERO
Threads:      12
Algorithm:    RandomX (rx/0)
Privileges:   elevated
Huge pages:   enabled (2MB pages)

Pool:         xmr-us-east1.nanopool.org:10300
Wallet:       4AdUndX...684Rge
Worker:       worker1

New job: 123456 | Height: 2845123
Hashrate: 9000.0 H/s | Difficulty: 100000 | Accepted: 5 | Rejected: 0
Share found! J: 123456 Nonce: 0xa3f2e1d0 Attempts: 125043
Hash: 00000000123456789abcdef012345678fedcba98765432100011223344556677
[POOL] Share accepted (6/6 = 100.0%)
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
Windows: %USERPROFILE%\AppData\Local\MoneroMiner\randomx_<seedhash>.dat
Linux:   ~/.cache/monerominer/randomx_<seedhash>.dat
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

**MoneroMiner** is licensed under the [Creative Commons Attribution-NonCommercial-NoDerivatives 4.0 International License](https://creativecommons.org/licenses/by-nc-nd/4.0/).

**Third-Party:**

- RandomX: BSD 3-Clause (see RandomX/LICENSE)
- picojson: BSD 2-Clause
