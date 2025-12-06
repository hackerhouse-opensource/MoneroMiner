#include "Platform.h"
#include <sstream>
#include <iomanip>

namespace Platform {

#ifdef PLATFORM_WINDOWS
    bool initializeSockets() {
        WSADATA wsaData;
        return WSAStartup(MAKEWORD(2, 2), &wsaData) == 0;
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
        while (std::getline(cpuinfo, line)) {
            if (line.find("model name") != std::string::npos) {
                size_t pos = line.find(':');
                if (pos != std::string::npos) {
                    std::string brand = line.substr(pos + 2);
                    // Trim whitespace
                    brand.erase(0, brand.find_first_not_of(" \t"));
                    brand.erase(brand.find_last_not_of(" \t") + 1);
                    return brand;
                }
            }
        }
        return "Unknown CPU";
    }
    
    std::string getCPUFeatures() {
        std::ifstream cpuinfo("/proc/cpuinfo");
        std::string line;
        std::string features;
        
        while (std::getline(cpuinfo, line)) {
            if (line.find("flags") != std::string::npos) {
                if (line.find("aes") != std::string::npos) features += " AES";
                if (line.find("avx") != std::string::npos) features += " AVX";
                if (line.find("avx2") != std::string::npos) features += " AVX2";
                features += " VM";
                break;
            }
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
                    int total = std::stoi(value);
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
            usage = (int)((usedGB / totalGB) * 100.0);
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
        return std::thread::hardware_concurrency();
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

#endif

} // namespace Platform
