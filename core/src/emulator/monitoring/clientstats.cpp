#include "emulator/monitoring/clientstats.h"

namespace monitoring
{

const char* ClientPlaneName(ClientPlane plane)
{
    switch (plane)
    {
        case ClientPlane::Rest:
            return "rest";
        case ClientPlane::Ws:
            return "ws";
        case ClientPlane::Events:
            return "events";
        case ClientPlane::DebugWs:
            return "debug_ws";
        case ClientPlane::Mcp:
            return "mcp";
        case ClientPlane::Cli:
            return "cli";
        case ClientPlane::Count:
            break;
    }
    return "unknown";
}

ClientStats& ClientStats::Instance()
{
    static ClientStats* stats = new ClientStats();  // never destroyed: sockets may close during exit
    return *stats;
}

void ClientStats::Opened(ClientPlane plane)
{
    Counters& counters = _counters[static_cast<size_t>(plane)];
    counters.connections.fetch_add(1, std::memory_order_relaxed);
    counters.opened.fetch_add(1, std::memory_order_relaxed);
}

void ClientStats::Closed(ClientPlane plane)
{
    _counters[static_cast<size_t>(plane)].connections.fetch_sub(1, std::memory_order_relaxed);
}

void ClientStats::Request(ClientPlane plane)
{
    _counters[static_cast<size_t>(plane)].requests.fetch_add(1, std::memory_order_relaxed);
}

void ClientStats::Dropped(ClientPlane plane)
{
    _counters[static_cast<size_t>(plane)].dropped.fetch_add(1, std::memory_order_relaxed);
}

ClientCounts ClientStats::Read(ClientPlane plane) const
{
    const Counters& counters = _counters[static_cast<size_t>(plane)];
    ClientCounts counts;
    counts.connections = counters.connections.load(std::memory_order_relaxed);
    counts.opened = counters.opened.load(std::memory_order_relaxed);
    counts.requests = counters.requests.load(std::memory_order_relaxed);
    counts.dropped = counters.dropped.load(std::memory_order_relaxed);
    return counts;
}

void ClientStats::Reset()
{
    for (Counters& counters : _counters)
    {
        counters.connections.store(0, std::memory_order_relaxed);
        counters.opened.store(0, std::memory_order_relaxed);
        counters.requests.store(0, std::memory_order_relaxed);
        counters.dropped.store(0, std::memory_order_relaxed);
    }
}

}  // namespace monitoring
