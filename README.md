# MoneroMiner v1.0.0

A lightweight, high-performance Monero (XMR) CPU miner using the RandomX algorithm.

## Features

- **Multi-threaded mining** with optimized CPU utilization
- **Pool mining support** with stratum protocol
- **Real-time statistics** with exciting console output
- **Persistent dataset caching** for improved startup time
- **Share tracking** and acceptance rate monitoring
- **Debug mode** for detailed mining information

## Quick Start

```bash
MoneroMiner.exe --wallet YOUR_WALLET_ADDRESS --threads 4
```

## Required Configuration

The wallet address is required for mining. Without it, the miner will not start.

```bash
MoneroMiner.exe --wallet YOUR_WALLET_ADDRESS
```

## Optional Configuration

- `--threads N`: Number of mining threads (default: CPU cores)
- `--pool-address URL`: Mining pool address (default: xmr-eu1.nanopool.org)
- `--pool-port PORT`: Mining pool port (default: 14444)
- `--worker-name NAME`: Worker name for pool identification (default: worker1)
- `--debug`: Enable detailed debug logging with mining statistics
- `--logfile [FILE]`: Enable logging to file (default: monerominer.log)

## Examples

Basic usage:

```bash
MoneroMiner.exe --wallet YOUR_WALLET_ADDRESS
```

Advanced usage with debug mode:

```bash
MoneroMiner.exe --wallet YOUR_WALLET_ADDRESS --threads 4 --debug
```

## Console Output

### Normal Mode

- Real-time hashrate in kH/s
- Total hashes computed
- Accepted/Rejected shares with percentage
- Uptime display

### Debug Mode

- Per-thread mining progress
- Job change notifications
- Share submission details
- Target and difficulty information
- RandomX initialization details

## Performance Tips

- Set thread count to match your CPU's physical core count
- RandomX dataset is cached to disk for faster startup
- Use `--debug` to monitor initialization and mining status
- Modern CPUs with AES-NI support will have better performance

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
