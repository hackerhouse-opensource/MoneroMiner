#include "Platform.h"
#include <sstream>
#include <iomanip>
// ADDED for diagnostic output
#include <iostream>
// ADDED includes for POSIX and general usage
#include <fstream>
#include <cstring>
#include <thread>
#ifndef PLATFORM_WINDOWS
    #include <sys/sysinfo.h>
    #include <sys/utsname.h>
    #include <unistd.h>
#endif

namespace Platform {

#ifdef PLATFORM_WINDOWS
    bool initializeSockets() {
        WSADATA wsaData;
        int res = WSAStartup(MAKEWORD(2, 2), &wsaData);
        if (res != 0) {
            // Emit an immediate diagnostic so Windows runs show why sockets failed
            std::cerr << "Platform::initializeSockets: WSAStartup failed with error: " << res << std::endl;
            return false;
        }
        return true;
    }
    
    void cleanupSockets() {
        WSACleanup();
    }
    
    std::string getCPUBrand() {
        int cpuInfo[4] = {0};
        char cpuBrand[0x40] = {0};
        
        __cpuid(cpuInfo, 0x80000002);
        memcpy(cpuBrand, cpuInfo, sizeof(cpuInfo));
        __cpuid(cpuInfo, 0x80000003);
        memcpy(cpuBrand + 16, cpuInfo, sizeof(cpuInfo));
        __cpuid(cpuInfo, 0x80000004);
        memcpy(cpuBrand + 32, cpuInfo, sizeof(cpuInfo));
        
        return std::string(cpuBrand);
    }
    
    std::string getCPUFeatures() {
        int cpuInfo[4] = {0};
        __cpuid(cpuInfo, 1);
        
        bool aes = (cpuInfo[2] & (1 << 25)) != 0;
        bool avx = (cpuInfo[2] & (1 << 28)) != 0;
        
        __cpuid(cpuInfo, 7);
        bool avx2 = (cpuInfo[1] & (1 << 5)) != 0;
        
        std::string features;
        if (aes) features += " AES";
        if (avx) features += " AVX";
        if (avx2) features += " AVX2";
        features += " VM";
        
        return features;
    }
    
    std::string getHugePagesInfo() {
        HANDLE token;
        if (OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) {
            LUID luid;
            if (LookupPrivilegeValue(NULL, SE_LOCK_MEMORY_NAME, &luid)) {
                PRIVILEGE_SET ps;
                ps.PrivilegeCount = 1;
                ps.Control = PRIVILEGE_SET_ALL_NECESSARY;
                ps.Privilege[0].Luid = luid;
                ps.Privilege[0].Attributes = SE_PRIVILEGE_ENABLED;
                
                BOOL result = FALSE;
                if (PrivilegeCheck(token, &ps, &result) && result) {
                    CloseHandle(token);
                    return "permission granted";
                }
            }
            CloseHandle(token);
        }
        return "unavailable";
    }
    
    void getMemoryInfo(double& usedGB, double& totalGB, int& usage) {
        MEMORYSTATUSEX memInfo;
        memInfo.dwLength = sizeof(MEMORYSTATUSEX);
        GlobalMemoryStatusEx(&memInfo);
        
        usedGB = (memInfo.ullTotalPhys - memInfo.ullAvailPhys) / (1024.0 * 1024.0 * 1024.0);
        totalGB = memInfo.ullTotalPhys / (1024.0 * 1024.0 * 1024.0);
        usage = memInfo.dwMemoryLoad;
    }
    
    std::string getMotherboardInfo() {
        HKEY hKey;
        if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, 
                          "HARDWARE\\DESCRIPTION\\System\\BIOS", 
                          0, KEY_READ, &hKey) == ERROR_SUCCESS) {
            char manufacturer[256] = {0};
            char product[256] = {0};
            DWORD size = sizeof(manufacturer);
            
            RegQueryValueExA(hKey, "SystemManufacturer", NULL, NULL, (LPBYTE)manufacturer, &size);
            size = sizeof(product);
            RegQueryValueExA(hKey, "SystemProductName", NULL, NULL, (LPBYTE)product, &size);
            
            RegCloseKey(hKey);
            return std::string(manufacturer) + " - " + product;
        }
        return "Unknown";
    }
    
    unsigned int getLogicalProcessors() {
        SYSTEM_INFO sysInfo;
        GetSystemInfo(&sysInfo);
        return sysInfo.dwNumberOfProcessors;
    }
    
    std::string getComputerName() {
        char name[MAX_COMPUTERNAME_LENGTH + 1];
        DWORD size = sizeof(name);
        if (GetComputerNameA(name, &size)) {
            return std::string(name);
        }
        return "unknown";
    }
    
    bool isRunningElevated() {
        BOOL isElevated = FALSE;
        HANDLE token = NULL;
        
        if (OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) {
            TOKEN_ELEVATION elevation;
            DWORD size;
            if (GetTokenInformation(token, TokenElevation, &elevation, sizeof(elevation), &size)) {
                isElevated = elevation.TokenIsElevated;
            }
            CloseHandle(token);
        }
        return isElevated;
    }
    
    bool hasHugePagesSupport() {
        // Windows: Check if we have SeLockMemoryPrivilege and can get large page size
        // To enable permanently: Run gpedit.msc -> Computer Configuration -> Windows Settings
        // -> Security Settings -> Local Policies -> User Rights Assignment
        // -> "Lock pages in memory" -> Add your user account -> Restart
        HANDLE token;
        if (!OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &token)) {
            return false;
        }
        
        // Try to enable the privilege
        TOKEN_PRIVILEGES tp;
        LUID luid;
        
        if (!LookupPrivilegeValue(NULL, SE_LOCK_MEMORY_NAME, &luid)) {
            CloseHandle(token);
            return false;
        }
        
        tp.PrivilegeCount = 1;
        tp.Privileges[0].Luid = luid;
        tp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
        
        BOOL result = AdjustTokenPrivileges(token, FALSE, &tp, 0, NULL, NULL);
        DWORD error = GetLastError();
        
        CloseHandle(token);
        
        // Check if privilege was successfully enabled AND large pages are available
        if (result && error == ERROR_SUCCESS) {
            SIZE_T pageSize = GetLargePageMinimum();
            return pageSize != 0;
        }
        
        return false;
    }
    
    bool has1GBPagesSupport() {
        return false; // Windows doesn't support 1GB pages
    }
    
    size_t getHugePageSize() {
        if (hasHugePagesSupport()) {
            return GetLargePageMinimum(); // Usually 2MB on Windows
        }
        return 0;
    }
    
    std::string getHugePagesStatus() {
        if (!isRunningElevated()) {
            return "unavailable (not elevated)";
        }
        
        // Try to enable the privilege
        HANDLE token;
        if (!OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &token)) {
            return "unavailable (token error)";
        }
        
        TOKEN_PRIVILEGES tp;
        LUID luid;
        
        if (!LookupPrivilegeValue(NULL, SE_LOCK_MEMORY_NAME, &luid)) {
            CloseHandle(token);
            return "unavailable (privilege not found)";
        }
        
        tp.PrivilegeCount = 1;
        tp.Privileges[0].Luid = luid;
        tp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
        
        BOOL result = AdjustTokenPrivileges(token, FALSE, &tp, 0, NULL, NULL);
        DWORD error = GetLastError();
        
        CloseHandle(token);
        
        if (result && error == ERROR_SUCCESS) {
            SIZE_T pageSize = GetLargePageMinimum();
            if (pageSize > 0) {
                std::stringstream ss;
                ss << "enabled (" << (pageSize / 1024 / 1024) << "MB pages)";
                return ss.str();
            }
            return "unavailable (GetLargePageMinimum failed)";
        }
        
        // Error codes for better diagnostics
        if (error == ERROR_NOT_ALL_ASSIGNED) {
            return "unavailable (privilege not assigned)";
        }
        
        return "unavailable";
    }

#else // PLATFORM_LINUX

    bool initializeSockets() {
        // No initialization needed on Linux
        return true;
    }

    void cleanupSockets() {
        // No cleanup needed on Linux
    }

    std::string getCPUBrand() {
        std::ifstream cpuinfo("/proc/cpuinfo");
        std::string line;
        
        // Try standard x86/x64 "model name" first
        while (std::getline(cpuinfo, line)) {
            if (line.find("model name") != std::string::npos) {
                size_t pos = line.find(':');
                if (pos != std::string::npos) {
                    std::string brand = line.substr(pos + 1);
                    brand.erase(0, brand.find_first_not_of(" \t"));
                    brand.erase(brand.find_last_not_of(" \t") + 1);
                    return brand;
                }
            }
        }
        
        // For ARM64/AArch64, try alternative fields
        cpuinfo.clear();
        cpuinfo.seekg(0);
        
        std::string hardware, model, processor;
        while (std::getline(cpuinfo, line)) {
            if (line.find("Hardware") != std::string::npos) {
                size_t pos = line.find(':');
                if (pos != std::string::npos) {
                    hardware = line.substr(pos + 1);
                    hardware.erase(0, hardware.find_first_not_of(" \t"));
                    hardware.erase(hardware.find_last_not_of(" \t") + 1);
                }
            }
            else if (line.find("model name") != std::string::npos || line.find("Model") != std::string::npos) {
                size_t pos = line.find(':');
                if (pos != std::string::npos) {
                    model = line.substr(pos + 1);
                    model.erase(0, model.find_first_not_of(" \t"));
                    model.erase(model.find_last_not_of(" \t") + 1);
                }
            }
            else if (line.find("Processor") != std::string::npos && processor.empty()) {
                size_t pos = line.find(':');
                if (pos != std::string::npos) {
                    processor = line.substr(pos + 1);
                    processor.erase(0, processor.find_first_not_of(" \t"));
                    processor.erase(processor.find_last_not_of(" \t") + 1);
                }
            }
        }
        
        // Return best available info
        if (!model.empty()) return model;
        if (!processor.empty()) return processor;
        if (!hardware.empty()) return hardware;
        
        return "Unknown CPU";
    }

    std::string getCPUFeatures() {
        std::ifstream cpuinfo("/proc/cpuinfo");
        std::string line;
        std::string features;
        while (std::getline(cpuinfo, line)) {
            if (line.find("flags") != std::string::npos || line.find("Features") != std::string::npos) {
                if (line.find("aes") != std::string::npos) features += " AES";
                if (line.find("avx") != std::string::npos) features += " AVX";
                if (line.find("avx2") != std::string::npos) features += " AVX2";
                features += " VM";
                break;
            }
        }
        
        // For ARM, just add VM if no features found
        if (features.empty()) {
            features = " VM";
        }
        
        return features;
    }

    std::string getHugePagesInfo() {
        std::ifstream meminfo("/proc/meminfo");
        std::string line;
        while (std::getline(meminfo, line)) {
            if (line.find("HugePages_Total") != std::string::npos) {
                size_t pos = line.find(':');
                if (pos != std::string::npos) {
                    std::string value = line.substr(pos + 1);
                    value.erase(0, value.find_first_not_of(" \t"));
                    int total = 0;
                    try { total = std::stoi(value); } catch (...) { total = 0; }
                    return total > 0 ? "available (" + std::to_string(total) + " pages)" : "unavailable";
                }
            }
        }
        return "unavailable";
    }

    void getMemoryInfo(double& usedGB, double& totalGB, int& usage) {
        struct sysinfo info;
        if (sysinfo(&info) == 0) {
            totalGB = info.totalram / (1024.0 * 1024.0 * 1024.0);
            double freeGB = info.freeram / (1024.0 * 1024.0 * 1024.0);
            usedGB = totalGB - freeGB;
            usage = (totalGB > 0.0) ? static_cast<int>((usedGB / totalGB) * 100.0) : 0;
        } else {
            usedGB = totalGB = 0;
            usage = 0;
        }
    }

    std::string getMotherboardInfo() {
        std::string vendor, product;
        std::ifstream vendorFile("/sys/devices/virtual/dmi/id/board_vendor");
        if (vendorFile) std::getline(vendorFile, vendor);
        std::ifstream productFile("/sys/devices/virtual/dmi/id/board_name");
        if (productFile) std::getline(productFile, product);
        if (!vendor.empty() && !product.empty()) {
            return vendor + " - " + product;
        }
        return "Unknown";
    }

    unsigned int getLogicalProcessors() {
        unsigned int hc = std::thread::hardware_concurrency();
        return hc == 0 ? 1u : hc;
    }

    std::string getComputerName() {
        struct utsname buffer;
        if (uname(&buffer) == 0) {
            return std::string(buffer.nodename);
        }
        return "unknown";
    }

    bool isRunningElevated() {
        return geteuid() == 0;
    }
    
    bool hasHugePagesSupport() {
#if defined(__aarch64__) || defined(__arm__)
        // ARM64: Check for THP (Transparent Huge Pages) support
        std::ifstream thp("/sys/kernel/mm/transparent_hugepage/enabled");
        if (thp) {
            std::string line;
            std::getline(thp, line);
            // Check if THP is enabled (shows as "[always]" or "[madvise]")
            return line.find("[always]") != std::string::npos || 
                   line.find("[madvise]") != std::string::npos;
        }
        return false;
#else
        // x86_64: Check traditional huge pages
        std::ifstream meminfo("/proc/meminfo");
        std::string line;
        while (std::getline(meminfo, line)) {
            if (line.find("Hugepagesize:") != std::string::npos) {
                size_t pos = line.find(':');
                if (pos != std::string::npos) {
                    std::string value = line.substr(pos + 1);
                    value.erase(0, value.find_first_not_of(" \t"));
                    int sizeKB = 0;
                    try { sizeKB = std::stoi(value); } catch (...) { return false; }
                    return sizeKB >= 2048; // At least 2MB
                }
            }
        }
        return false;
#endif
    }
    
    bool has1GBPagesSupport() {
#if defined(__aarch64__) || defined(__arm__)
        // ARM64 doesn't support 1GB pages in the same way as x86
        return false;
#else
        // Check if CPU supports 1GB pages (x86_64 only)
        std::ifstream cpuinfo("/proc/cpuinfo");
        std::string line;
        while (std::getline(cpuinfo, line)) {
            if (line.find("pdpe1gb") != std::string::npos) {
                cpuinfo.close();
                
                // Check if 1GB pages are configured in kernel
                std::ifstream meminfo("/proc/meminfo");
                while (std::getline(meminfo, line)) {
                    if (line.find("Hugepagesize:") != std::string::npos) {
                        if (line.find("1048576 kB") != std::string::npos) {
                            return true;
                        }
                    }
                }
                return false;
            }
        }
        return false;
#endif
    }
    
    size_t getHugePageSize() {
#if defined(__aarch64__) || defined(__arm__)
        // ARM64: THP typically uses 2MB pages
        std::ifstream thp("/sys/kernel/mm/transparent_hugepage/hpage_pmd_size");
        if (thp) {
            size_t size;
            thp >> size;
            return size;
        }
        return 2097152; // Default 2MB
#else
        std::ifstream meminfo("/proc/meminfo");
        std::string line;
        while (std::getline(meminfo, line)) {
            if (line.find("Hugepagesize:") != std::string::npos) {
                size_t pos = line.find(':');
                if (pos != std::string::npos) {
                    std::string value = line.substr(pos + 1);
                    value.erase(0, value.find_first_not_of(" \t"));
                    int sizeKB = 0;
                    try { 
                        sizeKB = std::stoi(value); 
                        return static_cast<size_t>(sizeKB) * 1024;
                    } catch (...) { 
                        return 0; 
                    }
                }
            }
        }
        return 0;
#endif
    }
    
    std::string getHugePagesStatus() {
#if defined(__aarch64__) || defined(__arm__)
        // ARM64: Check Transparent Huge Pages (THP)
        // To enable THP on ARM64:
        //   Temporary: echo always | sudo tee /sys/kernel/mm/transparent_hugepage/enabled
        //   Permanent: Add to /etc/rc.local or create systemd service
        std::ifstream thp("/sys/kernel/mm/transparent_hugepage/enabled");
        if (thp) {
            std::string line;
            std::getline(thp, line);
            
            if (line.find("[always]") != std::string::npos) {
                return "enabled (THP: always)";
            } else if (line.find("[madvise]") != std::string::npos) {
                return "enabled (THP: madvise)";
            } else if (line.find("[never]") != std::string::npos) {
                return "unavailable (THP disabled)";
            }
        }
        return "unavailable (THP not supported)";
#else
        // x86_64: Check traditional huge pages
        // To enable huge pages on x86_64:
        //   Temporary: sudo sysctl -w vm.nr_hugepages=1168
        //   Permanent: echo "vm.nr_hugepages=1168" | sudo tee -a /etc/sysctl.conf && sudo sysctl -p
        std::ifstream meminfo("/proc/meminfo");
        std::string line;
        int totalPages = 0;
        int freePages = 0;
        
        while (std::getline(meminfo, line)) {
            if (line.find("HugePages_Total:") != std::string::npos) {
                size_t pos = line.find(':');
                if (pos != std::string::npos) {
                    std::string value = line.substr(pos + 1);
                    value.erase(0, value.find_first_not_of(" \t"));
                    try { totalPages = std::stoi(value); } catch (...) {}
                }
            }
            if (line.find("HugePages_Free:") != std::string::npos) {
                size_t pos = line.find(':');
                if (pos != std::string::npos) {
                    std::string value = line.substr(pos + 1);
                    value.erase(0, value.find_first_not_of(" \t"));
                    try { freePages = std::stoi(value); } catch (...) {}
                }
            }
        }
        
        if (totalPages > 0) {
            size_t pageSize = getHugePageSize();
            std::stringstream ss;
            ss << freePages << "/" << totalPages << " available";
            if (pageSize > 0) {
                ss << " (" << (pageSize / 1024 / 1024) << "MB pages)";
            }
            return ss.str();
        }
        
        return "unavailable";
#endif
    }

#endif

} // namespace Platform
