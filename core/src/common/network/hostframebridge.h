#pragma once

/// @file hostframebridge.h
/// @brief IHostFrames over libpcap (macOS, Linux) or Npcap (Windows), loaded at run time: no build dependency, and a
/// machine without the library reports "not available" instead of failing to start (sn6-bridge-design.md §2).
///
/// Two handles on the adapter: a promiscuous one read by a capture thread (frames to the cards' MACs, broadcasts and
/// multicasts go into a bounded queue, the rest is counted as filtered) and one for sending, so the two threads never
/// share a handle. Naive first (project rule): a mutex-guarded queue and a 20 ms read timeout.
///
/// Permissions: macOS needs read / write on /dev/bpf* (Wireshark's ChmodBPF, or root), Linux CAP_NET_RAW +
/// CAP_NET_ADMIN, Windows an installed Npcap; the error text says which.

#include <atomic>
#include <deque>
#include <memory>
#include <mutex>
#include <thread>

#include "common/network/hostframes.h"

class PcapLibrary;

class HostFrameBridge final : public IHostFrames
{
public:
    static constexpr size_t kMaxQueuedFrames = 1024;  ///< frames for the cards not yet drained; more are dropped
    static constexpr size_t kMaxFrame = 1518;         ///< an Ethernet frame without the CRC, VLAN tag included

    HostFrameBridge();
    ~HostFrameBridge() override;

    HostFrameBridge(const HostFrameBridge&) = delete;
    HostFrameBridge& operator=(const HostFrameBridge&) = delete;

    // IHostFrames
    std::vector<HostAdapter> Adapters(std::string& error) override;
    bool Open(const std::string& adapter, std::string& error) override;
    void Close() override;
    bool IsOpen() const override { return _rx != nullptr; }
    std::string Adapter() const override;
    void SetStations(const std::vector<Mac>& stations) override;
    void Send(const uint8_t* frame, size_t length) override;
    void Drain(std::vector<std::vector<uint8_t>>& out) override;
    Counters GetCounters() const override;
    std::string LastError() const override;
    std::string Library() const override;

    /// Whether a received frame goes to the cards: to one of `stations`, a broadcast or a multicast, and not sent by
    /// one of them (the adapter's echo of our own frame). Shared with the tests
    static bool WantsFrame(const uint8_t* frame, size_t length, const std::vector<Mac>& stations);

private:
    void CaptureLoop();
    bool LoadPcap(std::string& error);

    std::unique_ptr<PcapLibrary> _pcap;
    void* _rx = nullptr;   ///< pcap_t*: the capture handle
    void* _tx = nullptr;   ///< pcap_t*: the send handle
    std::string _adapter;

    std::thread _thread;
    std::atomic<bool> _running{false};

    mutable std::mutex _mutex;   ///< guards everything below
    std::vector<Mac> _stations;
    std::deque<std::vector<uint8_t>> _queue;
    Counters _counters;
    std::string _lastError;
};
