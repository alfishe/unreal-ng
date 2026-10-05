#pragma once

/// @file hostframes.h
/// @brief Raw Ethernet frames to and from a host network adapter: the host side of the network bridge (network SN6,
/// sn6-bridge-design.md). The bridge mode of the Ethernet gateway puts a frame-level card (NE2000, 3C509B) on the
/// host's real LAN through this interface; the implementation is HostFrameBridge (libpcap / Npcap loaded at run
/// time), tests use a fake.
///
/// Threads: Send / Drain / SetStations are called on the emulator thread; the implementation keeps its own capture
/// thread. The emulator thread never waits for the host.

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

/// One host adapter as the packet library lists it
struct HostAdapter
{
    std::string name;          ///< "en0", "eth0", "\\Device\\NPF_{...}"
    std::string description;   ///< the library's description (Windows: the adapter's friendly name)
    std::vector<std::string> ipv4;
    std::array<uint8_t, 6> mac{};   ///< the adapter's own address (valid when hasMac)
    bool hasMac = false;
    bool loopback = false;
    bool wireless = false;     ///< Wi-Fi: bridged through MAC translation (an access point drops foreign MACs)
    bool up = false;
    bool running = false;
};

class IHostFrames
{
public:
    using Mac = std::array<uint8_t, 6>;

    struct Counters
    {
        uint64_t sent = 0;         ///< frames handed to the adapter
        uint64_t sendErrors = 0;
        uint64_t received = 0;     ///< frames taken for the cards
        uint64_t filtered = 0;     ///< frames on the wire that were not for the cards (or our own, echoed)
        uint64_t dropped = 0;      ///< frames for the cards lost because the queue was full
    };

    virtual ~IHostFrames() = default;

    /// The host's adapters (empty with `error` when the packet library is missing or refuses)
    virtual std::vector<HostAdapter> Adapters(std::string& error) = 0;

    /// Start bridging `adapter`. False with `error` (no library, no permission, unknown or wireless adapter)
    virtual bool Open(const std::string& adapter, std::string& error) = 0;
    virtual void Close() = 0;
    virtual bool IsOpen() const = 0;
    /// The adapter in use (empty while closed)
    virtual std::string Adapter() const = 0;

    /// The cards' station addresses: frames to them, broadcasts and multicasts come in; frames from them (the
    /// adapter's echo of what we sent) do not
    virtual void SetStations(const std::vector<Mac>& stations) = 0;

    /// Wi-Fi (network SN6b): the frames leave with the adapter's own MAC (MacTranslator); the capture keeps group
    /// frames and the unicasts to the host MAC that carry one of the guests' IPv4 addresses (SetGuestIps)
    virtual bool Translates() const = 0;
    virtual Mac HostMac() const = 0;
    virtual void SetGuestIps(const std::vector<uint32_t>& ips) = 0;

    /// Put a frame on the wire (no preamble, no CRC)
    virtual void Send(const uint8_t* frame, size_t length) = 0;

    /// Move the frames received since the last call into `out` (appended), oldest first
    virtual void Drain(std::vector<std::vector<uint8_t>>& out) = 0;

    virtual Counters GetCounters() const = 0;
    /// The last error of the open adapter (a capture failure while running), empty when none
    virtual std::string LastError() const = 0;
    /// The packet library in use ("libpcap version 1.10.1"), empty when not loaded
    virtual std::string Library() const = 0;
};
