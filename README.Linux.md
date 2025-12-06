# MoneroMiner - Linux Build Guide

This project was originally developed for Windows and has been ported to Linux.

## Prerequisites

### Ubuntu/Debian

```bash
sudo apt update
sudo apt install -y build-essential cmake git libssl-dev
```

### Fedora/RHEL

```bash
sudo dnf install -y gcc gcc-c++ cmake git openssl-devel
```

### Arch Linux

```bash
sudo pacman -S base-devel cmake git openssl
```

## Building

### Quick Start

```bash
# Make build script executable
chmod +x build.sh

# Build everything
./build.sh

# Or use make directly
make -j$(nproc)
```

### Build Targets

```bash
# Build everything (default)
make

# Build RandomX library only
make randomx

# Build with debug symbols
make debug

# Clean build artifacts
make clean

# Clean everything including RandomX
make distclean

# Install to system
sudo make install

# Run the miner
make run
# or
LD_LIBRARY_PATH=randomx/build:$LD_LIBRARY_PATH ./MoneroMiner
```

## Configuration

Edit `config.txt` with your pool and wallet information:

```
pool=gulf.moneroocean.stream:10128
wallet=YOUR_MONERO_WALLET_ADDRESS
worker=linux-rig-1
threads=4
```

## Differences from Windows Version

### Socket Implementation

- WinSock2 replaced with POSIX sockets
- Error handling uses `errno` instead of `WSAGetLastError()`
- No `WSAStartup()`/`WSACleanup()` required

### Library Linking

- RandomX compiled as shared object (.so) instead of static lib
- Runtime library path set with `-rpath`

### Threading

- Uses pthreads (already part of C++11 std::thread)

## Performance Notes

### CPU Optimization

The build uses `-march=native` for optimal performance on your CPU.
If building for distribution, use `-march=x86-64` instead.

### Huge Pages (Recommended)

Enable huge pages for better RandomX performance:

```bash
# Temporary (until reboot)
sudo sysctl -w vm.nr_hugepages=1168

# Permanent
echo "vm.nr_hugepages=1168" | sudo tee -a /etc/sysctl.conf
sudo sysctl -p
```

### Memory Locking

Allow the miner to lock memory:

```bash
# Add to /etc/security/limits.conf
echo "* soft memlock 3000000" | sudo tee -a /etc/security/limits.conf
echo "* hard memlock 3000000" | sudo tee -a /etc/security/limits.conf
```

## Troubleshooting

### Library not found

If you see `error while loading shared libraries: librandomx.so`:

```bash
export LD_LIBRARY_PATH=$PWD/randomx/build:$LD_LIBRARY_PATH
./MoneroMiner
```

Or use the provided run target:

```bash
make run
```

### Permission denied

If you get permission errors:

```bash
chmod +x MoneroMiner
chmod +x build.sh
```

### CMake version too old

Minimum CMake 3.10 required. On older systems:

```bash
# Install latest CMake
wget https://github.com/Kitware/CMake/releases/download/v3.27.0/cmake-3.27.0-linux-x86_64.sh
sudo sh cmake-3.27.0-linux-x86_64.sh --prefix=/usr/local --skip-license
```

## Running as Service

Create systemd service at `/etc/systemd/system/monerominer.service`:

```ini
[Unit]
Description=MoneroMiner
After=network.target

[Service]
Type=simple
User=miner
WorkingDirectory=/opt/monerominer
Environment="LD_LIBRARY_PATH=/opt/monerominer/randomx/build"
ExecStart=/opt/monerominer/MoneroMiner
Restart=always
RestartSec=10

[Install]
WantedBy=multi-user.target
```

Enable and start:

```bash
sudo systemctl daemon-reload
sudo systemctl enable monerominer
sudo systemctl start monerominer
sudo systemctl status monerominer
```

## WSL Support

Works perfectly in WSL2. Performance notes:

- WSL2 has near-native performance
- Huge pages may not work in WSL1
- Use WSL2 for best results

## License

Same as Windows version - see LICENSE file.
