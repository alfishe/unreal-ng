#pragma once

/// @file serialpeer.h
/// @brief The other end of an emulated UART (network adapters TDD §7.2).
///
/// The UART owns the timing: it moves one byte per character time (10 or 11
/// bit times at the programmed baud rate, in emulated T-states) and asks the
/// peer only at those moments. A peer is therefore deterministic as long as
/// what it holds is: the loopback peer echoes, the stream peers hold what the
/// virtual network delivered (journaled NetEvents), the ESP modules (step N3)
/// answer from their own state.

#include <cstdint>
#include <deque>
#include <functional>
#include <string>
#include <vector>

#include "common/network/nettypes.h"
#include "emulator/io/network/netstate.h"
#include "emulator/io/serial/comportspec.h"

class VirtualNetwork;

class ISerialPeer
{
public:
    virtual ~ISerialPeer() = default;

    /// The ZX finished sending `byte` (its stop bit left the TX pin)
    virtual void Transmit(uint8_t byte) = 0;

    /// The peer has a byte for the ZX (the UART takes it when its receiver
    /// is free and RTS allows)
    virtual bool HasByte() const = 0;
    virtual uint8_t TakeByte() = 0;

    /// The peer starts a byte only while the ZX's RTS allows it (hardware flow
    /// control on its side); false: it sends regardless (an ESP whose AT
    /// UART_CUR switched flow control off)
    virtual bool HonorsRts() const { return true; }

    /// Modem lines the peer drives: CTS lets the ZX send; DSR / DCD say the
    /// other end is there; RI rings
    virtual bool Cts() const { return true; }
    virtual bool Dsr() const { return true; }
    virtual bool Dcd() const { return true; }
    virtual bool Ri() const { return false; }

    /// A loopback test plug: the UART reads its own RTS on CTS and its own DTR on DSR / DCD instead of the
    /// lines above (they are wires of the plug, so they need no state of their own: MCR is the UART's)
    virtual bool MirrorsModemLines() const { return false; }

    /// RTS / DTR the ZX drives (MCR)
    virtual void OnModemLines(bool rts, bool dtr)
    {
        (void)rts;
        (void)dtr;
    }

    /// The line format the ZX programmed (divisor, LCR): a host serial device
    /// follows it
    virtual void OnLineSettings(const SerialLine& line) { (void)line; }

    /// The emulated clock (base T-states, as the UART's) and its rate: peers
    /// with their own timing (an ESP module's turnaround, timeouts) use it
    void SetClock(std::function<uint64_t()> now, uint32_t baseClockHz)
    {
        _clock = std::move(now);
        _clockHz = baseClockHz ? baseClockHz : 3500000;
    }

    /// Frame boundary on the machine thread: flush what the ZX sent, retry a
    /// lost connection
    virtual void OnFrame() {}

    /// Machine reset
    virtual void Reset() {}

    /// Short name for status views: loopback, tcp, serial
    virtual const char* Kind() const = 0;

    /// Where it goes (host:port, device), empty for loopback
    virtual std::string Target() const { return {}; }

    /// Bytes waiting for the ZX
    virtual size_t Pending() const = 0;

    /// Called when bytes for the ZX arrive from outside (a journaled network
    /// event, at its emulated time): the UART moves its clock there, so a peer
    /// that starts sending while the program is busy fills the FIFO (and
    /// overruns it) in the background as on hardware
    std::function<void()> onReceive;

protected:
    uint64_t Now() const { return _clock ? _clock() : 0; }
    uint64_t MicrosToT(uint64_t us) const { return us * _clockHz / 1000000u; }

private:
    std::function<uint64_t()> _clock;
    uint32_t _clockHz = 3500000;

public:

    /// True while the other end is reachable (TCP connected, device open)
    virtual bool Connected() const { return true; }
};

/// Echo: every byte the ZX sends comes back (tests, a quick self-check). As a test plug (PLUG) its wires also
/// loop the ZX's RTS to CTS and DTR to DSR and DCD; plain LOOPBACK holds those inputs active
class LoopbackPeer final : public ISerialPeer
{
public:
    explicit LoopbackPeer(bool plug = false) : _plug(plug) {}
    bool MirrorsModemLines() const override { return _plug; }
    void Transmit(uint8_t byte) override { _queue.push_back(byte); }
    bool HasByte() const override { return !_queue.empty(); }
    uint8_t TakeByte() override
    {
        const uint8_t b = _queue.front();
        _queue.pop_front();
        return b;
    }
    void Reset() override { _queue.clear(); }
    const char* Kind() const override { return _plug ? "plug" : "loopback"; }
    size_t Pending() const override { return _queue.size(); }

    /// TTD state: the echo queue holds bytes the ZX wrote, stored as they are
    const std::deque<uint8_t>& Queue() const { return _queue; }
    void SetQueue(const uint8_t* data, size_t length) { _queue.assign(data, data + length); }

private:
    bool _plug = false;
    std::deque<uint8_t> _queue;
};

/// A byte stream through the machine's virtual network: a host TCP endpoint
/// (TCP:<host>:<port>, a telnet BBS, a test harness) or a host serial device
/// (SERIAL:<device>[,baud], a real ESP on USB). Everything that comes from
/// outside is a journaled NetEvent (bytes, the DNS answer for a host name,
/// the device's modem lines); what the ZX sends goes out at the frame boundary.
///
/// Link phases: Resolving (a DNS query for the host name through the virtual
/// network: Hosts= first, then the host resolver), Connecting, Connected;
/// Idle between a failure and the next attempt (kRetryFrames)
class StreamPeer final : public ISerialPeer, public INetGuest
{
public:
    enum class Phase : uint8_t
    {
        Idle,
        Resolving,
        Connecting,
        Connected
    };

    /// @param network nullptr: never connects
    /// @param modemLines a serial device gets RTS / DTR and reports CTS / DSR /
    ///        RI / DCD ([NETWORK] ComModemLines); otherwise CTS reads asserted
    StreamPeer(VirtualNetwork* network, const ComPortSpec& spec, bool modemLines = false);

    /// A dialed link (the Hayes modem's: network tdd §10): it stays idle until Dial(), never retries on its own,
    /// and opens its sockets for `owner` (the modem: one guest of the virtual network for its link and its
    /// listener), which hands the events of cookies kCookieLink / kCookieDns back to OnNetEvent
    struct Dialer
    {
        INetGuest* owner = nullptr;
    };
    StreamPeer(VirtualNetwork* network, const Dialer& dialer);
    ~StreamPeer() override;

    StreamPeer(const StreamPeer&) = delete;
    StreamPeer& operator=(const StreamPeer&) = delete;

    // ISerialPeer
    void Transmit(uint8_t byte) override;
    bool HasByte() const override { return !_rx.empty(); }
    uint8_t TakeByte() override;
    bool Cts() const override;
    bool Dsr() const override;
    bool Dcd() const override;
    bool Ri() const override;
    void OnModemLines(bool rts, bool dtr) override;
    void OnLineSettings(const SerialLine& line) override;
    void OnFrame() override;
    void Reset() override;
    const char* Kind() const override { return _spec.kind == ComPortSpec::Kind::Serial ? "serial" : "tcp"; }
    std::string Target() const override;
    size_t Pending() const override { return _rx.size(); }
    bool Connected() const override { return _phase == Phase::Connected; }

    Phase GetPhase() const { return _phase; }
    /// Why the last attempt failed (empty after a success)
    const std::string& LastError() const { return _lastError; }
    /// The address the host name resolved to (0 while unknown)
    uint32_t ResolvedAddress() const { return _resolvedAddr; }

    /// Drop the link and open it again now (a host that came back, tests)
    void Reconnect() { Open(); }

    // --- Dialer links (Dialer constructor) ------------------------------------------------------
    /// Open the link to `spec` (TCP:<host>:<port>): the name resolves, the connect follows; onLinkChange tells
    /// how it ends (Connected, or Idle with the reason)
    void Dial(const ComPortSpec& spec);
    /// Close the link now (sockets at the next frame boundary); bytes in either direction are dropped
    void HangUp();
    /// Take over an accepted connection (a listener's socket, already bound to the owner with kCookieLink): the
    /// link is Connected to `peer` at once; no onLinkChange
    void Adopt(uint16_t socket, const NetEndpoint& peer);
    /// The target the link dials (TTD restore: the state does not carry the spec)
    void SetDialSpec(const ComPortSpec& spec) { _spec = spec; }
    const ComPortSpec& Spec() const { return _spec; }
    bool IsDialer() const { return _dialer; }
    /// A dialer link changed phase by itself: Connected, or Idle after a failure / the other end closing
    /// (`status`: why a connect failed - Refused for a refused one; `why` in words)
    std::function<void(Phase phase, NetEventStatus status, const std::string& why)> onLinkChange;
    /// The socket of the connection (0: none)
    uint16_t Socket() const { return _socket; }
    /// Where the link goes: the resolved address and port (0 while unknown)
    NetEndpoint Remote() const { return _remote; }
    void SetRemote(const NetEndpoint& remote) { _remote = remote; }

    static constexpr uint32_t kCookieLink = 0;
    static constexpr uint32_t kCookieDns = 1;

    // INetGuest
    void OnNetEvent(uint32_t cookie, NetEventType type, NetEventStatus status, const NetEndpoint& peer,
                    const uint8_t* data, uint32_t length, uint32_t source) override;

    /// A byte waiting for the ZX and where it came from (TTD state keeps the
    /// source, not the byte: 1-based journal index of the NetEvent + offset)
    struct RxByte
    {
        uint8_t value = 0;
        uint32_t source = 0;
        uint32_t offset = 0;
    };
    const std::deque<RxByte>& Received() const { return _rx; }
    void SetReceived(std::deque<RxByte> rx) { _rx = std::move(rx); }
    const std::vector<uint8_t>& Unsent() const { return _tx; }
    void SetUnsent(const uint8_t* data, size_t length) { _tx.assign(data, data + length); }

    /// TTD: the link's phase and sockets (netstate::StreamLink)
    void SaveLink(netstate::StreamLink& out) const;
    void LoadLink(const netstate::StreamLink& in);

    uint64_t BytesIn() const { return _bytesIn; }
    uint64_t BytesOut() const { return _bytesOut; }
    uint32_t RetryFrames() const { return _retryFrames; }

    /// Frames between attempts after a failure or a close (~5 s)
    static constexpr uint32_t kRetryFrames = 250;
    /// Frames a DNS answer may take (~5 s)
    static constexpr uint32_t kResolveFrames = 250;
    /// Received bytes kept for the ZX; more are dropped (the ZX is not reading)
    static constexpr size_t kMaxPending = 64 * 1024;

private:
    void Open();
    void StartConnect(uint32_t addr);
    void Fail(const std::string& why, NetEventStatus status = NetEventStatus::Error);
    /// The guest the sockets belong to (the owner of a dialer link, else this)
    INetGuest* Guest() { return _owner ? _owner : this; }
    void CloseSockets();
    void SendLineAndLines();

    VirtualNetwork* _network = nullptr;
    ComPortSpec _spec;
    bool _modemLines = false;
    bool _dialer = false;
    INetGuest* _owner = nullptr;
    NetEndpoint _remote;

    Phase _phase = Phase::Idle;
    uint16_t _socket = 0;
    uint16_t _dnsSocket = 0;
    uint16_t _dnsId = 0;
    uint16_t _dnsSeq = 0;
    uint32_t _resolvedAddr = 0;
    bool _connectPending = false;  ///< the name resolved: connect at the frame boundary
    bool _closePending = false;    ///< the link dropped: close the sockets at the frame boundary
    uint32_t _retryFrames = 0;     ///< frames until the next attempt (0 = none planned)
    uint32_t _waitFrames = 0;      ///< frames the DNS answer may still take
    uint8_t _deviceLines = 0;      ///< the device's CTS / DSR / RI / DCD (MSR layout)
    bool _rts = false, _dtr = false;
    SerialLine _line;
    std::string _lastError;
    std::deque<RxByte> _rx;
    std::vector<uint8_t> _tx;
    uint64_t _bytesIn = 0;
    uint64_t _bytesOut = 0;
};
