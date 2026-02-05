#pragma once

// Platform-specific socket implementation
#ifdef _WIN32
// Windows socket headers
// CRITICAL: winsock2.h MUST be included BEFORE windows.h to avoid conflicts
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX  // Prevent min/max macros from windows.h
#endif
#include <io.h>
#include <iphlpapi.h>
#include <windows.h>
#include <winsock2.h>  // Must come first!
#include <ws2tcpip.h>


#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "iphlpapi.lib")

// Platform-specific defines
#define SOCKET_CLOSE_GRACEFUL 1
#define SOCKET_ERROR_AGAIN WSAEWOULDBLOCK

#ifndef _WIN32
// Platform-specific defines for non-Windows
#define SOCKET_ERROR_AGAIN EAGAIN
#endif

// Ensure FIONBIO is defined
#ifndef FIONBIO
#define FIONBIO 0x8004667E  // Standard FIONBIO value for Windows
#endif

// Define UNIX-like constants and types for Windows
#define SOCK_NONBLOCK 0
#define SOL_TCP IPPROTO_TCP

// Windows doesn't have socklen_t, it uses int instead
#ifndef socklen_t
typedef int socklen_t;
#endif

// Windows doesn't have ssize_t
#ifndef ssize_t
#ifdef _WIN64
typedef __int64 ssize_t;
#else
typedef long ssize_t;
#endif
#endif

// Map POSIX error codes to Windows socket error codes
#ifndef EWOULDBLOCK
#define EWOULDBLOCK WSAEWOULDBLOCK
#endif
#ifndef EAGAIN
#define EAGAIN WSAEWOULDBLOCK
#endif
#ifndef EINTR
#define EINTR WSAEINTR
#endif
#ifndef EBADF
#define EBADF WSAEBADF
#endif

// Windows uses WSAGetLastError() instead of errno for socket operations
// We'll define a macro for convenience (used alongside getLastSocketError())
#ifndef errno
#define errno WSAGetLastError()
#endif

namespace win_sockets
{
// Windows doesn't have these functions, so we need to define them
inline int close(SOCKET s)
{
    return closesocket(s);
}

// Initialize Windows sockets
inline bool initializeSockets()
{
    WSADATA wsaData;
    return WSAStartup(MAKEWORD(2, 2), &wsaData) == 0;
}

// Cleanup Windows sockets
inline void cleanupSockets()
{
    WSACleanup();
}

// Close socket safely (Windows implementation)
inline void closeSocket(SOCKET& sock)
{
    if (sock != INVALID_SOCKET)
    {
        shutdown(sock, SD_BOTH);
        closesocket(sock);
        sock = INVALID_SOCKET;
    }
}

// Windows-specific socket error handling
inline int getLastSocketError()
{
    return WSAGetLastError();
}

// Set socket to non-blocking mode (Windows implementation)
inline bool setSocketNonBlocking(SOCKET sock)
{
    if (sock == INVALID_SOCKET)
    {
        return false;
    }
    u_long mode = 1;  // 1 to enable non-blocking mode
    return ioctlsocket(sock, FIONBIO, &mode) == 0;
}
}  // namespace win_sockets
#else
// UNIX socket headers
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

// UNIX socket type
typedef int SOCKET;
#define INVALID_SOCKET (-1)
#define SOCKET_ERROR (-1)

namespace unix_sockets
{
// Initialize and cleanup are no-ops on UNIX
inline bool initializeSockets()
{
    return true;
}

inline void cleanupSockets()
{
    // No cleanup needed for UNIX
}

// Close socket safely (UNIX implementation)
inline void closeSocket(SOCKET& sock)
{
    if (sock != INVALID_SOCKET)
    {
        shutdown(sock, SHUT_RDWR);
        close(sock);
        sock = INVALID_SOCKET;
    }
}

// UNIX-specific socket error handling
inline int getLastSocketError()
{
    return errno;
}

// Set socket to non-blocking mode (UNIX implementation)
inline bool setSocketNonBlocking(SOCKET sock)
{
    int flags = fcntl(sock, F_GETFL, 0);
    if (flags == -1)
        return false;
    return fcntl(sock, F_SETFL, flags | O_NONBLOCK) != -1;
}
}  // namespace unix_sockets
#endif

// Platform-agnostic interface

// Close socket safely (platform-agnostic)
inline void closeSocket(SOCKET& sock)
{
#ifdef _WIN32
    win_sockets::closeSocket(sock);
#else
    unix_sockets::closeSocket(sock);
#endif
}

#ifdef _WIN32
// Use Windows implementations
inline int close(SOCKET s)
{
    return win_sockets::close(s);
}

inline bool initializeSockets()
{
    return win_sockets::initializeSockets();
}

inline void cleanupSockets()
{
    win_sockets::cleanupSockets();
}

inline int getLastSocketError()
{
    return win_sockets::getLastSocketError();
}

inline bool setSocketNonBlocking(SOCKET sock)
{
    return win_sockets::setSocketNonBlocking(sock);
}
#else
// Use UNIX implementations
inline bool initializeSockets()
{
    return unix_sockets::initializeSockets();
}

inline void cleanupSockets()
{
    unix_sockets::cleanupSockets();
}

inline int getLastSocketError()
{
    return unix_sockets::getLastSocketError();
}

inline bool setSocketNonBlocking(SOCKET sock)
{
    return unix_sockets::setSocketNonBlocking(sock);
}
#endif
