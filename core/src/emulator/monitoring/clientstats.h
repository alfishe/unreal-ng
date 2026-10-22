#pragma once

/// @file clientstats.h
/// @brief Who talks to the emulator (the monitoring planes, `host.clients`): per automation plane the connections open
/// now, opened since start, and the requests served. The planes count themselves: the WebAPI per HTTP request and
/// per socket (/api/v1/ws, /api/v1/companions/events, /api/v1/websocket), MCP per tool call and session, the CLI per
/// connection and command. Relaxed atomics only: a count costs one increment, never a lock.

#include <array>
#include <atomic>
#include <cstdint>
#include <string>

namespace monitoring
{

enum class ClientPlane : uint8_t
{
    Rest,       ///< HTTP requests of the WebAPI (requests; a WebSocket `call` is counted on its socket, not here)
    Ws,         ///< /api/v1/ws connections (requests: the `call`s on them)
    Events,     ///< /api/v1/companions/events sockets
    DebugWs,    ///< /api/v1/websocket (the debugger event socket)
    Mcp,        ///< MCP sessions (requests: the tool calls)
    Cli,        ///< CLI TCP connections (requests: the commands)
    Count
};

/// "rest", "ws", "events", "debug_ws", "mcp", "cli"
const char* ClientPlaneName(ClientPlane plane);

struct ClientCounts
{
    int64_t connections = 0;  ///< open now
    uint64_t opened = 0;      ///< since start
    uint64_t requests = 0;    ///< since start
};

class ClientStats
{
public:
    static ClientStats& Instance();

    void Opened(ClientPlane plane);
    void Closed(ClientPlane plane);
    void Request(ClientPlane plane);

    ClientCounts Read(ClientPlane plane) const;
    /// Tests: every counter to 0
    void Reset();

private:
    struct Counters
    {
        std::atomic<int64_t> connections{0};
        std::atomic<uint64_t> opened{0};
        std::atomic<uint64_t> requests{0};
    };
    std::array<Counters, static_cast<size_t>(ClientPlane::Count)> _counters;
};

/// Counts a connection open for its lifetime (Opened in the constructor, Closed in the destructor)
class ClientConnection
{
public:
    explicit ClientConnection(ClientPlane plane) : _plane(plane) { ClientStats::Instance().Opened(plane); }
    ~ClientConnection() { ClientStats::Instance().Closed(_plane); }
    ClientConnection(const ClientConnection&) = delete;
    ClientConnection& operator=(const ClientConnection&) = delete;

private:
    ClientPlane _plane;
};

}  // namespace monitoring
