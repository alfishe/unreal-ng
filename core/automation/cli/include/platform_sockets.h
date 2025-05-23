#pragma once

// Platform-specific socket implementation
#ifdef _WIN32
// Windows socket headers
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")

// Define UNIX-like constants and types for Windows
#define SOCK_NONBLOCK 0
#define SOL_TCP IPPROTO_TCP

// Windows doesn't have socklen_t, it uses int instead
#ifndef socklen_t
typedef int socklen_t;
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

// Windows-specific socket error handling
inline int getLastSocketError()
{
    return WSAGetLastError();
}

// Set socket to non-blocking mode (Windows implementation)
inline bool setSocketNonBlocking(SOCKET sock)
{
    u_long mode = 1;
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
    // No cleanup needed for UNIX sockets
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
