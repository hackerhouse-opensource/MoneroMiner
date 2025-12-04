# MoneroMiner v1.0.0

A lightweight, high-performance Monero (XMR) CPU miner using the RandomX algorithm.

## ⚠️ IMPORTANT: Pool Selection for CPU Mining

**NOT ALL POOLS SUPPORT CPU MINERS PROPERLY!**

### ✅ Recommended Pools for CPU Mining:

- **supportxmr.com:3333** - Best for CPU miners, starts at difficulty 10,000-50,000
- **xmrpool.eu:3333** - CPU-friendly with proper vardiff
- **monero.crypto-pool.fr:3333** - Good for low hashrate miners

### ❌ NOT Recommended for CPU Mining:

- **nanopool.org:14444** - Starts at difficulty 4+ billion (designed for GPUs/ASICs)
  - At 6000 H/s CPU speed, you need ~7 days to find ONE share
  - Vardiff may not lower difficulty for CPU speeds

## Features

- **Multi-threaded mining** with optimized CPU utilization
- **Pool mining support** with stratum protocol
- **Real-time statistics** with exciting console output
- **Persistent dataset caching** for improved startup time
- **Share tracking** and acceptance rate monitoring
- **Debug mode** for detailed mining information

## Quick Start

```bash
# Use a CPU-friendly pool:
MoneroMiner.exe --wallet YOUR_WALLET --pool pool.supportxmr.com:3333 --threads 4
```

## Performance Expectations

### CPU Mining Hashrates:

- Modern desktop CPU (8 cores): 3000-4000 H/s
- High-end CPU (16+ cores): 6000-12000 H/s
- Server CPU (24+ cores): 10000-20000 H/s

### Share Frequency (at recommended pools):

- Difficulty 10,000: ~3 shares/minute @ 500 H/s
- Difficulty 50,000: ~1 share/minute @ 1000 H/s
- Difficulty 4 billion (nanopool): **DAYS between shares** (CPU)

## Configuration

The wallet address is required for mining. Without it, the miner will not start.

```bash
MoneroMiner.exe --wallet YOUR_WALLET_ADDRESS --pool pool.supportxmr.com:3333
```

## Optional Configuration

- `--threads N`: Number of mining threads (default: CPU cores)
- `--pool ADDRESS:PORT`: Mining pool address (default: xmr-eu1.nanopool.org:14444)
- `--worker NAME`: Worker name for pool identification (default: worker1)
- `--debug`: Enable detailed debug logging with mining statistics
- `--logfile [FILE]`: Enable logging to file (default: monerominer.log)

## Examples

Basic usage with recommended pool:

```bash
MoneroMiner.exe --wallet YOUR_WALLET --pool pool.supportxmr.com:3333 --threads 8
```

Advanced usage with debug mode:

```bash
MoneroMiner.exe --wallet YOUR_WALLET --pool pool.supportxmr.com:3333 --threads 4 --debug
```

## Console Output

### Normal Mode

```
[10s] Hashrate: 6421.0 H/s | Difficulty: 50000 | Accepted: 12 | Rejected: 0
```

- Real-time hashrate in H/s
- Current pool difficulty
- Accepted/Rejected shares

### Debug Mode

- Per-thread mining progress with hash comparisons
- Job change notifications with height/difficulty
- Share submission details
- Target and difficulty information
- RandomX initialization details
- Hash-by-hash validation (every 1000 hashes)

## Performance Tips

- **Use recommended CPU-friendly pools** - Critical for finding shares quickly
- Set thread count to match your CPU's physical core count
- RandomX dataset is cached to disk for faster startup
- Use `--debug` to monitor initialization and mining status
- Modern CPUs with AES-NI support will have better performance
- Disable SMT/Hyper-Threading for better performance per core

## Troubleshooting

### "No shares found after hours of mining"

- **Solution**: Switch to supportxmr.com:3333 or another CPU-friendly pool
- Nanopool's high difficulty means you need millions of hashes per share

### "Hashrate is low"

- Use physical cores, not logical cores (disable hyper-threading in BIOS)
- Ensure RandomX dataset loaded successfully (watch startup logs)
- Close other CPU-intensive applications

### "Shares rejected"

- Usually means outdated job (stale share)
- Your connection to pool may be unstable
- Switch to a geographically closer pool server

## Author

Hacker Fantastic (https://hacker.house)

## License

Attribution-NonCommercial-NoDerivatives 4.0 International
https://creativecommons.org/licenses/by-nc-nd/4.0/

## Donations

If you find this miner useful, consider donating XMR to support development:

```
8BghJxGWaE2Ekh8KrrEEqhGMLVnB17cCATNscfEyH8qq9uvrG3WwYPXbvqfx1HqY96ZaF3yVYtcQ2X1KUMNt2Pr29M41jHf
```
