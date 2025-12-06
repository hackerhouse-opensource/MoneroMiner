#pragma once

// Platform detection
#if defined(_WIN32) || defined(_WIN64)
    #define PLATFORM_WINDOWS
    
    // CRITICAL: Define this BEFORE including any Windows headers
    // This prevents winsock.h from being included (we want winsock2.h only)
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    
    // Include WinSock2 BEFORE windows.h to prevent winsock.h inclusion
    #include <winsock2.h>
    #include <ws2tcpip.h>
    
    // Now safe to include windows.h
    #include <windows.h>
    #include <intrin.h>
    #include <sysinfoapi.h>
    #include <powerbase.h>
    
    #pragma comment(lib, "ws2_32.lib")
    #pragma comment(lib, "PowrProf.lib")
    
    typedef SOCKET socket_t;
    #define INVALID_SOCKET_VALUE INVALID_SOCKET
    #define CLOSE_SOCKET closesocket
    #define SOCKET_ERROR_VALUE SOCKET_ERROR
    #define GET_SOCKET_ERROR() WSAGetLastError()
    #define WOULD_BLOCK WSAEWOULDBLOCK
#else
    #define PLATFORM_LINUX
    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <arpa/inet.h>
    #include <unistd.h>
    #include <netdb.h>
    #include <errno.h>
    #include <sys/ioctl.h>
    #include <fcntl.h>
    #include <sys/sysinfo.h>
    #include <sys/utsname.h>
    #include <fstream>
    #include <string.h>
    #include <thread>
    
    typedef int socket_t;
    #define INVALID_SOCKET_VALUE -1
    #define CLOSE_SOCKET close
    #define SOCKET_ERROR_VALUE -1
    #define GET_SOCKET_ERROR() errno
    #define WOULD_BLOCK EWOULDBLOCK
#endif

// Cross-platform socket initialization
namespace Platform {
    bool initializeSockets();
    void cleanupSockets();
    
    // System info functions
    std::string getCPUBrand();
    std::string getCPUFeatures();
    std::string getHugePagesInfo();
    void getMemoryInfo(double& usedGB, double& totalGB, int& usage);
    std::string getMotherboardInfo();
    unsigned int getLogicalProcessors();
    std::string getComputerName();
    bool isRunningElevated();
}
