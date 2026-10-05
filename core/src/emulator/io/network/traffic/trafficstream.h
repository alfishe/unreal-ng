#pragma once

/// @file trafficstream.h
/// @brief The traffic tap as a live pcapng stream on a TCP port (network #91 T3, owner Q3): Wireshark reads it with
/// `wireshark -k -i TCP@127.0.0.1:<port>` on macOS, Linux and Windows alike (portable sockets, netsockets.h); the
/// extcap script tools/wireshark/unreal-ng-extcap.py lists running emulators in Wireshark and reads the same stream.
///
/// Every client gets the pcapng header and the ring as it is, then each new packet as it is recorded (the tap's live
/// readers). One port per emulator; port 0 picks a free one. The listener binds the address the network's host
/// listeners use ([NETWORK] RemoteAccess: every interface, or 127.0.0.1 only). One thread accepts and sends; the
/// emulation thread only encodes into the readers' queues.

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>

class NetworkTrafficTap;

class TrafficStream
{
public:
    explicit TrafficStream(NetworkTrafficTap& tap) : _tap(tap) {}
    ~TrafficStream();

    TrafficStream(const TrafficStream&) = delete;
    TrafficStream& operator=(const TrafficStream&) = delete;

    /// Listen on `port` (0: any free port) at `address` (IPv4, host byte order; 0 = every interface). False with
    /// `error` when the port is taken
    bool Start(uint32_t address, uint16_t port, std::string& error);
    void Stop();
    bool Running() const { return _running; }
    uint16_t Port() const { return _port; }
    unsigned Clients() const { return _clients; }

private:
    void Loop();

    NetworkTrafficTap& _tap;
    std::thread _thread;
    std::atomic<bool> _running{false};
    std::atomic<uint16_t> _port{0};
    std::atomic<unsigned> _clients{0};
    intptr_t _listener = -1;
};
