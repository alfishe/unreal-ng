#pragma once

/// @file hostnetbridge.h
/// @brief IHostNet over real host sockets (network adapters TDD §3).
///
/// One worker thread owns every host socket: commands from the emulation
/// thread go into a queue, the worker polls its sockets and turns what happens
/// into HostNetEvents. A second worker resolves names (getaddrinfo blocks). The
/// emulation thread never waits for the host.
///
/// Naive first (project rule): mutex-guarded queues and a 10 ms poll timeout.
/// Optimisation candidates (a wake-up socket instead of the timeout, lock-free
/// rings) wait for measurements.

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <mutex>
#include <thread>
#include <vector>

#include "common/network/hostnet.h"
#include "common/network/netsockets.h"

class HostNetBridge : public IHostNet
{
public:
    struct Options
    {
        uint32_t connectTimeoutMs = 10000;       ///< a TCP connect gives up after this (the W5300 would take 31.8 s)
        uint32_t maxQueuedSendBytes = 1u << 20;  ///< per TCP socket; more is dropped with a Reset
        uint32_t maxQueuedEvents = 4096;         ///< events not yet taken; more closes the socket with a Reset
    };

    HostNetBridge();
    explicit HostNetBridge(const Options& options);
    ~HostNetBridge() override;

    HostNetBridge(const HostNetBridge&) = delete;
    HostNetBridge& operator=(const HostNetBridge&) = delete;

    // IHostNet
    void TcpConnect(uint16_t socket, const NetEndpoint& to) override;
    void TcpSend(uint16_t socket, const uint8_t* data, uint32_t length) override;
    void TcpShutdownWrite(uint16_t socket) override;
    void TcpListen(uint16_t socket, uint16_t hostPort) override;
    void UdpSend(uint16_t socket, const NetEndpoint& to, const uint8_t* data, uint32_t length) override;
    void DnsQuery(uint16_t socket, const NetEndpoint& server, const uint8_t* query, uint32_t length) override;
    void IcmpEcho(uint16_t socket, const NetEndpoint& to, const uint8_t* data, uint32_t length) override;
    void Close(uint16_t socket) override;
    void CloseAll() override;
    bool PollEvent(HostNetEvent& out) override;

    /// Name lookup used for DNS answers; tests replace it (no host resolver)
    using Resolver = std::function<bool(const std::string& name, std::vector<uint32_t>& out)>;
    void SetResolver(Resolver resolver);

    /// Host port a listener actually bound (0 while unknown / failed)
    uint16_t ListenerHostPort(uint16_t socket) const;

private:
    enum class CommandType : uint8_t { Connect, Send, ShutdownWrite, Listen, UdpSend, Close, CloseAll };
    struct Command
    {
        CommandType type = CommandType::Close;
        uint16_t socket = 0;
        NetEndpoint endpoint;
        std::vector<uint8_t> data;
    };

    enum class State : uint8_t { Connecting, Connected, Listening, Udp };
    struct HostSocket
    {
        netsock::Handle handle = netsock::kInvalid;
        State state = State::Connecting;
        std::deque<uint8_t> sendQueue;
        bool shutdownPending = false;   ///< FIN after the queue drains
        bool readClosed = false;        ///< peer FIN seen
        uint64_t connectDeadlineMs = 0;
    };

    struct DnsJob
    {
        uint16_t socket = 0;
        NetEndpoint server;
        std::vector<uint8_t> query;
    };

    void Run();
    void RunResolver();
    void RunPinger();
    void Execute(Command& cmd);
    void Service(uint16_t id, HostSocket& s, const netsock::PollItem& item);
    void FlushSend(uint16_t id, HostSocket& s);
    void Drop(uint16_t id);
    void Emit(HostNetEvent ev);
    static uint64_t NowMs();

    Options _options;

    std::thread _worker;
    std::thread _resolverThread;
    std::atomic<bool> _stop{false};

    std::mutex _commandMutex;
    std::vector<Command> _commands;

    mutable std::mutex _eventMutex;
    std::deque<HostNetEvent> _events;

    std::mutex _dnsMutex;
    std::condition_variable _dnsCv;
    std::deque<DnsJob> _dnsJobs;
    Resolver _resolver;

    // Pings block for up to the timeout: their own worker, one at a time
    std::thread _pingThread;
    std::mutex _pingMutex;
    std::condition_variable _pingCv;
    std::deque<DnsJob> _pingJobs;   ///< same shape: socket, destination, the request
    static constexpr uint32_t kPingTimeoutMs = 2000;
    static constexpr size_t kMaxQueuedPings = 16;

    // Worker-thread state
    std::map<uint16_t, HostSocket> _sockets;
    uint16_t _nextAcceptedId = IHostNet::kFirstAcceptedId;
    mutable std::mutex _listenerPortMutex;
    std::map<uint16_t, uint16_t> _listenerPorts;
};
