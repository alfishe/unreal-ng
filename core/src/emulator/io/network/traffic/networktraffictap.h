#pragma once

/// @file networktraffictap.h
/// @brief Everything a machine's network adapters send and receive, recorded at the one door they all use - the
/// virtual network (network traffic task #91, design.md). Frame cards' Ethernet frames and socket adapters'
/// operations (connect, send, data, close, ...) with their bytes, each stamped with machine time and its TTD
/// position.
///
/// Two stores (owner Q1): a ring that always records (the newest packets up to a byte budget), and a pcapng file
/// that records from Start until Stop with no limit. The tap only records: a TTD replay runs the same code and so
/// records the same traffic again. Written on the emulation thread; automation reads from its own threads: every
/// public member takes the tap's mutex (network events are rare next to CPU work: the lock costs nothing that matters).

#include <cstdint>
#include <deque>
#include <fstream>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "common/network/nettypes.h"
#include "emulator/state/statenode.h"

class SocketPacketizer;

struct TrafficTime
{
    uint64_t frame = 0;       ///< the machine frame (TTD position, with tInFrame)
    uint32_t tInFrame = 0;    ///< TTD units into the frame
    uint64_t us = 0;          ///< emulated microseconds since power-on (pcapng time stamps)
};

struct TrafficRecord
{
    enum class Kind : uint8_t
    {
        Frame = 0,    ///< an Ethernet frame of a frame card (or of the host LAN in BRIDGE)
        Socket = 1,   ///< a socket adapter's operation
    };

    uint64_t index = 0;            ///< running number (never reused, survives the ring's trimming)
    TrafficTime time;
    Kind kind = Kind::Frame;
    bool out = false;              ///< adapter -> network (false: network -> adapter)
    std::string adapter;           ///< "isa2.eth", "lan", "zxnetusb", "isa1.esp", ...
    // Socket operations
    std::string op;                ///< connect, connected, connect-failed, send, data, sendto, datagram, close,
                                   ///< peer-closed, reset, listen, accepted, echo, echo-reply
    uint16_t socket = 0;
    NetProto proto = NetProto::Tcp;
    NetEndpoint peer;
    uint16_t localPort = 0;
    std::vector<uint8_t> bytes;    ///< the frame, or the operation's data
};

class NetworkTrafficTap
{
public:
    static constexpr size_t kDefaultRingBytes = 8u << 20;   ///< owner Q1: 8 MiB of packet bytes

    explicit NetworkTrafficTap(std::function<TrafficTime()> clock);
    ~NetworkTrafficTap();

    NetworkTrafficTap(const NetworkTrafficTap&) = delete;
    NetworkTrafficTap& operator=(const NetworkTrafficTap&) = delete;

    // --- Recording (the virtual network calls these) ---------------------------------------------------------------

    void Frame(const std::string& adapter, bool out, const uint8_t* frame, size_t length);
    void Socket(const std::string& adapter, bool out, const char* op, uint16_t socket, NetProto proto,
                const NetEndpoint& peer, uint16_t localPort, const uint8_t* data, size_t length);

    // --- The ring ----------------------------------------------------------------------------------------------------

    struct Filter
    {
        uint64_t since = 0;          ///< records with index >= since
        std::string adapter;         ///< empty = every adapter
        int kind = -1;               ///< -1 any, else TrafficRecord::Kind
        size_t last = 0;             ///< the newest N (0 = all)
    };
    std::vector<TrafficRecord> Records(const Filter& filter) const;
    void Clear();
    void SetRingBytes(size_t bytes);
    size_t RingBytes() const;
    uint64_t NextIndex() const;

    /// The ring as a pcapng file: frames as they are, socket operations as synthetic TCP / UDP / ICMP packets
    /// (SocketPacketizer), every packet with its record's TTD position as the comment
    std::vector<uint8_t> Pcapng(const Filter& filter) const;

    // --- The file (unbounded, from Start to Stop) --------------------------------------------------------------------

    bool StartFile(const std::string& path, std::string& error);
    void StopFile();
    bool FileOpen() const;
    std::string FilePath() const;

    /// The report every automation surface prints: ring size and budget, counters, the file
    StateNode Describe() const;
    /// One record for the reports (index, time, kind, direction, adapter, op / socket / peer, length, summary, hex)
    static StateNode RecordNode(const TrafficRecord& r);
    /// A one-line summary ("ARP who-has ...", "TCP send 120 bytes to 93.184.216.34:80: GET / HTTP/1.0")
    static std::string Summary(const TrafficRecord& r);

private:
    void Add(TrafficRecord r);
    void WriteToFile(const TrafficRecord& r);

    mutable std::mutex _mutex;
    std::function<TrafficTime()> _clock;
    std::deque<TrafficRecord> _ring;
    size_t _ringBytes = 0;
    size_t _ringBudget = kDefaultRingBytes;
    uint64_t _nextIndex = 1;
    uint64_t _frames = 0, _socketOps = 0, _bytes = 0, _trimmed = 0;

    std::unique_ptr<std::ofstream> _file;   ///< non-ASCII paths through FileHelper::ToFsPath (Windows)
    std::string _filePath;
    std::map<std::string, uint32_t> _fileInterfaces;   ///< adapter -> pcapng interface id in the file
    uint64_t _fileRecords = 0;
    std::unique_ptr<SocketPacketizer> _filePacketizer;   ///< the file's socket conversations (T2)
};
