# MoneroMiner v1.0.0

A lightweight, high-performance Monero (XMR) CPU miner using the RandomX algorithm.

## Features

- Multi-threaded mining with optimized CPU utilization
- Pool mining support with stratum protocol
- Configurable via command-line options
- Persistent dataset caching for improved startup time
- Real-time mining statistics and share tracking

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

- `--threads N`: Number of mining threads (default: auto-detected)
- `--pool-address URL`: Mining pool address (default: xmr-eu1.nanopool.org)
- `--pool-port PORT`: Mining pool port (default: 10300)
- `--worker-name NAME`: Worker name for pool identification (default: miniminer)
- `--debug`: Enable detailed debug logging
- `--logfile [FILE]`: Enable logging to file (default: monerominer.log)

## Examples

Basic usage:

```bash
MoneroMiner.exe --wallet YOUR_WALLET_ADDRESS
```

Advanced usage:

```bash
MoneroMiner.exe --wallet YOUR_WALLET_ADDRESS --threads 4 --pool-address xmr-eu1.nanopool.org --debug
```

## Performance Tips

- Set thread count to match your CPU's physical core count
- RandomX dataset is cached to disk for faster startup
- Monitor debug output for initialization and mining status

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
