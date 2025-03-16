# MoneroMiner v1.0.0

A lightweight, high-performance Monero (XMR) CPU miner using the RandomX algorithm.

## Features

- Multi-threaded mining with optimized CPU utilization
- Pool mining support with stratum protocol
- Configurable via command-line options
- Persistent dataset caching for improved startup time
- Comprehensive debug logging and file logging support
- Real-time mining statistics and share tracking

## Usage

```bash
MoneroMiner.exe [OPTIONS]
```

### Command Line Options

- `--help`: Show help message and exit
- `--debug`: Enable detailed debug logging (default: false)
- `--logfile [FILE]`: Enable logging to file
  - If no filename is provided, defaults to 'monerominer.log'
  - All console output will be saved to this file
  - Useful for long-running sessions and debugging
- `--threads N`: Number of mining threads (default: auto-detected)
- `--pool-address URL`: Mining pool address (default: xmr-eu1.nanopool.org)
- `--pool-port PORT`: Mining pool port (default: 10300)
- `--wallet ADDRESS`: Your Monero wallet address
- `--worker-name NAME`: Worker name for pool identification (default: miniminer)
- `--password PASS`: Pool password (default: x)
- `--user-agent AGENT`: User agent string (default: miniminer/1.0.0)

### Examples

Basic usage with default settings:

```bash
MoneroMiner.exe --wallet YOUR_WALLET_ADDRESS
```

Advanced usage with multiple options:

```bash
MoneroMiner.exe --threads 4 --pool-address xmr-eu1.nanopool.org --pool-port 10300 --wallet YOUR_WALLET_ADDRESS --worker-name worker1
```

Debug mode with logging:

```bash
MoneroMiner.exe --debug --logfile debug.log --threads 4
```

## Logging

The miner supports two types of logging:

1. Console logging: All mining activity is displayed in real-time on the console
2. File logging: When enabled with `--logfile`, all console output is also saved to the specified file

### File Logging Features

- **Default Log File**: If no filename is specified with `--logfile`, the miner will create and use 'monerominer.log' in the current directory
- **Append Mode**: The log file is opened in append mode, preserving previous mining sessions' logs
- **Timestamped Entries**: Each log entry includes a timestamp in the format [YYYY-MM-DD HH:MM:SS]
- **Synchronized Output**: All console output is automatically synchronized with the log file
- **Thread-Safe**: Log file access is thread-safe, ensuring no log entries are lost or corrupted
- **Automatic Flushing**: The log file is flushed after each write to prevent data loss

### Debug Logging

When `--debug` is enabled, the log file will contain additional information such as:

- Detailed hash validation results
- Share submission details
- Pool communication
- Thread-specific performance metrics
- RandomX initialization details
- Nonce updates and hash calculations
- Job processing information
- Share acceptance/rejection details

### Log File Format

Each log entry follows this format:

```
[2024-03-21 15:30:45] Message content
```

Example log entries:

```
[2024-03-21 15:30:45] Initializing with 4 mining threads...
[2024-03-21 15:30:46] Connected to pool.
[2024-03-21 15:30:47] New job received - Height: 3368719, Job ID: 310631, Target: f3220000
[2024-03-21 15:30:48] Thread 00 | HR: 1234.56 H/s | Job: 310631 | Nonce: 0x12345678
```

## Performance Considerations

- The miner automatically detects the optimal number of threads based on your CPU
- For best performance, the number of threads should match your CPU's physical core count
- The RandomX dataset is cached to disk to improve startup time on subsequent runs

## Author

Hacker Fantastic (https://hacker.house)

## License

Attribution-NonCommercial-NoDerivatives 4.0 International
https://creativecommons.org/licenses/by-nc-nd/4.0/
