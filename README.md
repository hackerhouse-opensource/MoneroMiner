# MoneroMiner

A high-performance Monero (XMR) mining implementation in C++ using the RandomX algorithm. This miner is optimized for modern CPUs and provides efficient mining capabilities with a simple command-line interface.

## Features

- RandomX algorithm implementation
- Multi-threading support
- Efficient CPU utilization
- Pool mining support
- Configurable via command-line options
- Debug mode for detailed logging
- Persistent dataset caching

## Building

### Prerequisites

- C++17 compatible compiler
- CMake 3.10 or higher
- Windows SDK 10.0 or higher
- RandomX library

### Build Instructions

Compile with Visual Studio 2022.

## Usage

```bash
MoneroMiner.exe [OPTIONS]

Options:
  --help               Show this help message
  --debug              Enable debug logging
  --threads N          Number of mining threads (default: number of CPU cores)
  --pool-address URL   Pool address (default: xmr-eu1.nanopool.org)
  --pool-port PORT     Pool port (default: 10300)
  --wallet ADDRESS     Wallet address
  --worker-name NAME   Worker name (default: miniminer)
  --password PASS      Pool password (default: x)
  --user-agent AGENT   User agent string (default: miniminer/1.0.0)
```

### Example

```bash
MoneroMiner.exe --threads 4 --pool-address xmr-eu1.nanopool.org --pool-port 10300 --wallet YOUR_WALLET_ADDRESS
```

## Performance

The miner is optimized for modern CPUs and achieves competitive hash rates. Performance varies depending on your CPU architecture and number of cores.

## Donations

If you find this miner useful, consider donating XMR to support development:

```
8BghJxGWaE2Ekh8KrrEEqhGMLVnB17cCATNscfEyH8qq9uvrG3WwYPXbvqfx1HqY96ZaF3yVYtcQ2X1KUMNt2Pr29M41jHf
```

## License

This project is licensed under the Attribution-NonCommercial-NoDerivatives 4.0 International License.
For more details, visit: https://creativecommons.org/licenses/by-nc-nd/4.0/

## Author

Created by [Hacker Fantastic](https://hacker.house)

## Acknowledgments

- RandomX algorithm developers
- Monero community
- All contributors to this project
