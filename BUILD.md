# MoneroMiner - Build and Optimization Guide

Complete instructions for building MoneroMiner from source and optimizing for maximum performance.

---

## Table of Contents

- [Windows Build](#windows-build)
- [Linux Build](#linux-build)
- [Performance Optimization](#performance-optimization)
- [Troubleshooting](#troubleshooting)

---

## Windows Build

**IMPORTANT:** On Windows, use `build.ps1` NOT `make`. The Makefile is only for Linux.

### Prerequisites

- Visual Studio 2019 or 2022 (Community Edition is free)
- Git for Windows

### Build Steps

```powershell
# Clone repository
git clone https://github.com/hackerhouse-opensource/MoneroMiner.git
cd MoneroMiner

# Build with PowerShell script
.\build.ps1

# Output: x64\Release\MoneroMiner.exe
```

### Build Options

```powershell
.\build.ps1          # Release build (default)
.\build.ps1 -debug   # Debug build with symbols
```

### Manual Build with Visual Studio

1. Open `MoneroMiner.sln` in Visual Studio
2. Select `Release` configuration and `x64` platform
3. Build -> Build Solution (F7)
4. Executable: `x64\Release\MoneroMiner.exe`

---

## Linux Build

**IMPORTANT:** On Linux, use `make` NOT `build.ps1`.

### Prerequisites

**Ubuntu/Debian:**

```bash
sudo apt update
sudo apt install -y build-essential cmake git libssl-dev
```

**Fedora/RHEL:**

```bash
sudo dnf install -y gcc gcc-c++ cmake git openssl-devel
```

**Arch Linux:**

```bash
sudo pacman -S base-devel cmake git openssl
```

**Raspberry Pi OS (64-bit):**

```bash
sudo apt update
sudo apt install -y build-essential cmake git libssl-dev
```

### Build Steps

```bash
# Clone repository
git clone https://github.com/hackerhouse-opensource/MoneroMiner.git
cd MoneroMiner

# Build
make

# Output: bin/monerominer
```

### Makefile Targets

```bash
make              # Build everything (RandomX + MoneroMiner)
make randomx      # Build RandomX library only
make clean        # Clean build artifacts
make distclean    # Clean everything including RandomX
make install      # Install to /usr/local/bin (requires sudo)
make run          # Build and run with default wallet
make debug        # Build with debug symbols
make info         # Show build configuration
make help         # Show all available targets
```

### Running After Build

```bash
# Run directly
bin/monerominer --wallet YOUR_WALLET_ADDRESS

# Or use make run
make run
```

---

## Performance Optimization

### Thread Configuration

**Best Practice:** Use physical CPU core count, NOT logical thread count.

```bash
# Check physical cores
# Linux:
lscpu | grep "Core(s) per socket"

# Windows:
wmic cpu get NumberOfCores
```

**Examples:**

```bash
# AMD Ryzen 9 3900X: 12 physical cores, 24 threads
--threads 12

# AMD Ryzen 24-core: 24 physical cores, 48 threads
--threads 24

# Raspberry Pi 5: 4 physical cores
--threads 4
```

**Why Physical Cores Only?**

- RandomX is memory-bandwidth limited
- Hyper-threading provides minimal benefit
- Using logical threads causes cache contention

### Huge Pages (Critical for Performance)

Huge pages provide **10-30% hashrate improvement**.

#### Windows - Large Pages (2MB)

**Permanent Setup (Recommended):**

1. Press `Win+R`, type `gpedit.msc`, Enter
2. Navigate: Computer Configuration -> Windows Settings -> Security Settings -> Local Policies -> User Rights Assignment
3. Double-click `Lock pages in memory`
4. Add your user account
5. **Restart computer** (required)

**Verify:**

Run miner and check startup output:

```
Huge pages: enabled (2MB pages)
```

If you see `unavailable (privilege not assigned)`, restart computer.

**Troubleshooting:**

```powershell
# Check if privilege is assigned
whoami /priv | findstr SeLockMemory

# Should show:
# SeLockMemoryPrivilege    Lock pages in memory    Enabled
```

#### Linux x86_64 - Huge Pages (2MB/1GB)

**2MB Huge Pages (Recommended):**

```bash
# Calculate pages needed:
# RandomX dataset = 2GB = 1024 pages of 2MB
# Add buffer for threads: 1168 pages total

# Temporary (until reboot)
sudo sysctl -w vm.nr_hugepages=1168

# Permanent
echo "vm.nr_hugepages=1168" | sudo tee -a /etc/sysctl.conf
sudo sysctl -p

# Verify
grep HugePages /proc/meminfo
# Should show: HugePages_Total: 1168
```

**1GB Huge Pages (Maximum Performance):**

Only works if CPU supports 1GB pages:

```bash
# Check CPU support
grep pdpe1gb /proc/cpuinfo
# If no output, CPU doesn't support 1GB pages

# Edit GRUB configuration
sudo nano /etc/default/grub

# Add to GRUB_CMDLINE_LINUX (inside quotes):
default_hugepagesz=1G hugepagesz=1G hugepages=3

# Update GRUB
sudo update-grub              # Debian/Ubuntu
sudo grub2-mkconfig -o /boot/grub2/grub.cfg  # Fedora/RHEL

# Reboot
sudo reboot

# Verify after reboot
cat /proc/meminfo | grep Hugepagesize
# Should show: Hugepagesize: 1048576 kB
```

**Memory Locking (Optional):**

```bash
# Prevent swapping of miner memory
echo "* soft memlock 3000000" | sudo tee -a /etc/security/limits.conf
echo "* hard memlock 3000000" | sudo tee -a /etc/security/limits.conf
# Log out and back in for changes to take effect
```

#### Linux ARM64 - Transparent Huge Pages

ARM64 systems (Raspberry Pi, Graviton, Jetson) use Transparent Huge Pages:

```bash
# Check THP status
cat /sys/kernel/mm/transparent_hugepage/enabled

# Enable THP (temporary)
echo always | sudo tee /sys/kernel/mm/transparent_hugepage/enabled

# Enable THP (permanent via systemd)
sudo nano /etc/systemd/system/thp-enable.service
```

Add this content:

```ini
[Unit]
Description=Enable Transparent Huge Pages for RandomX
DefaultDependencies=no
After=sysinit.target local-fs.target
Before=basic.target

[Service]
Type=oneshot
ExecStart=/bin/sh -c 'echo always > /sys/kernel/mm/transparent_hugepage/enabled'

[Install]
WantedBy=basic.target
```

Enable service:

```bash
sudo systemctl daemon-reload
sudo systemctl enable thp-enable.service
sudo systemctl start thp-enable.service

# Verify
cat /sys/kernel/mm/transparent_hugepage/enabled
# Should show: [always] madvise never
```

### CPU Affinity (Advanced)

Pin mining threads to specific CPU cores:

**Linux:**

```bash
# Pin to cores 0-11 (12-core CPU)
taskset -c 0-11 ./MoneroMiner --wallet YOUR_WALLET --threads 12

# Raspberry Pi 5 (4 cores)
taskset -c 0-3 ./MoneroMiner --wallet YOUR_WALLET --threads 4
```

**Windows:**

Use Process Lasso, Process Hacker, or Task Manager to set CPU affinity manually.

### Compiler Optimizations

**Linux - CPU-Specific Build:**

```bash
# Default build uses -march=native (optimized for your CPU)
make

# For portable binary (runs on any x86_64):
CXXFLAGS="-march=x86-64" make

# For specific CPU (e.g., Haswell):
CXXFLAGS="-march=haswell" make
```

**Verify Optimizations:**

```bash
# Check what instructions are used
objdump -d MoneroMiner | grep -i "vperm\|vaes\|vpaddd"
```

---

## Troubleshooting

### Platform-Specific Build Issues

**Problem:** Typed `make` on Windows

```
Solution: Use PowerShell script build.ps1 instead of make
```

**Problem:** Compiler errors about missing headers

```
Solution:
1. Ensure Visual Studio C++ workload is installed
2. Run Visual Studio Installer
3. Modify installation
4. Check "Desktop development with C++"
```

**Problem:** LNK1104 error - cannot open randomx.lib

```
Solution:
1. Build RandomX project first (right-click -> Build)
2. Check x64/Release folder for randomx.lib
3. If missing, clean and rebuild RandomX
```

**Problem:** MT.exe error about manifest

```
Solution:
1. Install Windows SDK
2. Or disable manifest generation in project properties
```

**Problem:** g++: command not found

```bash
# Install compiler
sudo apt install build-essential  # Debian/Ubuntu
sudo dnf install gcc-c++           # Fedora
```

**Problem:** cmake: command not found

```bash
# Install CMake
sudo apt install cmake  # Debian/Ubuntu
sudo dnf install cmake  # Fedora
```

**Problem:** RandomX build fails with "undefined reference"

```bash
# Clean and rebuild
make distclean
make
```

**Problem:** Cannot find librandomx.so when running

```bash
# The Makefile handles library paths automatically
# If you still get this error:
export LD_LIBRARY_PATH=$PWD/randomx/build:$LD_LIBRARY_PATH
bin/monerominer --wallet YOUR_WALLET

# Or use make run (sets path automatically)
make run
```

**Problem:** Permission denied

```bash
# Make executable have correct permissions
chmod +x bin/monerominer
```

### Performance Issues

**Problem:** Low hashrate compared to expectations

```
Possible causes:
1. Huge pages not enabled (10-30% slower)
2. Using logical thread count instead of physical cores
3. CPU thermal throttling
4. Background processes consuming CPU
5. Slow RAM (below DDR4-2666)

Solutions:
1. Enable huge pages (see Performance Optimization)
2. Set threads = physical cores only
3. Check CPU temperature (should be under 85C)
4. Close unnecessary programs
5. Check RAM speed in BIOS/system info
```

**Problem:** High CPU usage but zero hashrate

```
Possible causes:
1. RandomX dataset initialization failed
2. VM creation failed for threads
3. Pool connection issues

Solutions:
1. Check miner log for errors
2. Ensure 3GB free RAM available
3. Run with --debug --logfile to see detailed errors
4. Try different pool
```

**Problem:** Hashrate drops over time

```
Cause: CPU thermal throttling

Solutions:
1. Check CPU temperature
2. Improve case cooling
3. Clean CPU cooler
4. Reduce thread count by 1-2
5. Lower CPU power limit in BIOS
```

**Problem:** Shares all rejected

```
Possible causes:
1. Network latency too high
2. System clock incorrect
3. Pool compatibility issue

Solutions:
1. Choose geographically closer pool
2. Check system time is correct
3. Try different pool from list in README
```

### Build Optimization Issues

**Problem:** Build is very slow

```
Solutions:
# Linux: Use multiple cores
make -j$(nproc)

# Windows: Enable multi-processor compilation
MSBuild.exe /m MoneroMiner.sln
```

**Problem:** Out of memory during build

```
Solutions:
1. Close other programs
2. Reduce parallel jobs:
   make -j2  # Use only 2 cores
3. Add swap space (Linux):
   sudo fallocate -l 4G /swapfile
   sudo chmod 600 /swapfile
   sudo mkswap /swapfile
   sudo swapon /swapfile
```

---

## Running as Service

### Linux systemd Service

Create `/etc/systemd/system/monerominer.service`:

```ini
[Unit]
Description=MoneroMiner CPU Miner
After=network.target

[Service]
Type=simple
User=miner
WorkingDirectory=/opt/monerominer
Environment="LD_LIBRARY_PATH=/opt/monerominer/randomx/build"
ExecStart=/opt/monerominer/MoneroMiner --wallet YOUR_WALLET --threads 12 --logfile
Restart=always
RestartSec=10
StandardOutput=journal
StandardError=journal

[Install]
WantedBy=multi-user.target
```

Manage service:

```bash
sudo systemctl daemon-reload
sudo systemctl enable monerominer
sudo systemctl start monerominer
sudo systemctl status monerominer

# View logs
journalctl -u monerominer -f
```

### Windows Task Scheduler

Run at startup without login:

1. Open Task Scheduler
2. Create Basic Task
3. Trigger: "When the computer starts"
4. Action: "Start a program"
5. Program: `C:\path\to\MoneroMiner.exe`
6. Arguments: `--wallet YOUR_WALLET --threads 12 --headless --logfile`
7. Finish and test

---

**For additional help, see the main [README.md](README.md) or visit [DeepWiki Documentation](https://deepwiki.com/hackerhouse-opensource/MoneroMiner)**
