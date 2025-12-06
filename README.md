# MoneroMiner v1.0.0

A lightweight, high-performance Monero (XMR) CPU miner using the RandomX algorithm.

## Features

- **Multi-threaded mining** with optimized CPU utilization
- **Pool mining support** with stratum protocol
- **Real-time statistics** with console output
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
- `--pool ADDRESS:PORT`: Mining pool (default: xmr-us-east1.nanopool.org:10300)
- `--worker NAME`: Worker name for pool identification (default: worker1)
- `--debug`: Enable detailed debug logging with mining statistics
- `--logfile`: Enable logging to file (default: monerominer.log)

## Examples

Basic usage:

```bash
MoneroMiner.exe --wallet YOUR_WALLET_ADDRESS --threads 8
```

Advanced usage with debug mode:

```bash
MoneroMiner.exe --wallet YOUR_WALLET_ADDRESS --pool pool.supportxmr.com:3333 --threads 4 --debug
```

## Console Output

### Normal Mode

- Real-time hashrate in H/s
- Current difficulty
- Accepted/Rejected shares

### Debug Mode

- Per-thread mining progress with hash comparisons
- Job change notifications with height/difficulty
- Share submission details and pool responses
- Target and difficulty information
- RandomX initialization details

## Performance Tips

- **Run as Administrator** for 10-30% hashrate boost (enables large pages/huge pages)
- Set thread count to match your CPU's physical core count
- RandomX dataset is cached to disk for faster startup
- Use `--debug` to monitor initialization and mining status
- Modern CPUs with AES-NI support will have better performance

### Large Pages Support

For optimal performance, run the miner as Administrator. This enables:

- **Large Pages (Huge Pages)**: 2MB pages instead of 4KB, reducing TLB misses
- **Performance gain**: 10-30% hashrate improvement
- **Windows requirement**: "Lock pages in memory" privilege

To enable permanently:

1. Run `gpedit.msc` (Local Group Policy Editor)
2. Navigate to: Computer Configuration → Windows Settings → Security Settings → Local Policies → User Rights Assignment
3. Open "Lock pages in memory"
4. Add your user account
5. Restart your computer

The miner will automatically detect and use large pages when available.

## Author

Hacker Fantastic (https://hacker.house)

## License

Attribution-NonCommercial-NoDerivatives 4.0 International  
https://creativecommons.org/licenses/by-nc-nd/4.0/

## Donations

If you find this miner useful, consider donating XMR to support development:

```
488XamY1RKhUVpHPirdcXwb9ePGjGrcNoi8FA2MHDWCrYEfpz1ansYR4gUuhkjDVWR2rmgitM9LtZUXx4SrBSvPB9smskV8
```
