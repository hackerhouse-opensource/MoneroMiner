# MoneroMiner v1.0.0

A lightweight, high-performance Monero (XMR) CPU miner using the RandomX algorithm.

## Features

- **Multi-threaded mining** with optimized CPU utilization
- **Pool mining support** with stratum protocol
- **Real-time statistics** with console output
- **Persistent dataset caching** for improved startup time
- **Share tracking** and acceptance rate monitoring
- **Debug mode** for detailed mining information
- **Cross-platform support** (Windows, Linux, ARM64/AArch64)

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

For optimal performance, enable large/huge pages for 10-30% hashrate improvement.

#### Windows - 2MB Large Pages

Run the miner as Administrator to enable 2MB large pages automatically.

To enable permanently without Administrator:

1. Run `gpedit.msc` (Local Group Policy Editor)
2. Navigate to: Computer Configuration → Windows Settings → Security Settings → Local Policies → User Rights Assignment
3. Open "Lock pages in memory"
4. Add your user account
5. Restart your computer
6. Run miner normally (no admin needed)

#### Linux - 2MB Huge Pages

Enable 2MB huge pages (recommended - 1168 pages = 2.3GB):

```bash
# Temporary (until reboot)
sudo sysctl -w vm.nr_hugepages=1168

# Permanent
echo "vm.nr_hugepages=1168" | sudo tee -a /etc/sysctl.conf
sudo sysctl -p
```

#### Linux - 1GB Pages (Advanced)

For maximum performance on supported CPUs (requires reboot):

```bash
# Check CPU support
grep pdpe1gb /proc/cpuinfo

# Add to /etc/default/grub
GRUB_CMDLINE_LINUX="default_hugepagesz=1G hugepagesz=1G hugepages=3"

# Update GRUB and reboot
sudo update-grub
sudo reboot

# Verify after reboot
grep HugePages /proc/meminfo
```

The miner will automatically detect and use the best available page size.

## Author

Hacker Fantastic (https://hacker.house)

## License

Attribution-NonCommercial-NoDerivatives 4.0 International  
https://creativecommons.org/licenses/by-nc-nd/4.0/

## Donations

If you find this miner useful, consider donating XMR to support development:

```
8C6hFb4Buo6dYwJiZEaFhyYhZTJaR4NyXSBzKMF1BnNKMGD92yeaY3a9PxuWp9bhTAh6dAXwqyyLfFxaPRct7j81L8t4iK2
```
