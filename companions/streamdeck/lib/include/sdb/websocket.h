// WebSocket client (RFC 6455) on the daemon's EventLoop: the connection to unreal-ng's /api/v1/ws
// (docs/emulator/design/control-interfaces/websocket-interface.md). Plain ws:// only (the WebAPI is local), no
// extensions. Non-blocking, no thread of its own; it reconnects with backoff, pings, and bounds its send queue.
//
// All calls and callbacks run on the loop's thread; from another thread, go through EventLoop::Post.
#pragma once
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "sdb/eventloop.h"

namespace sdb::ws
{

enum class Opcode : std::uint8_t
{
    Continuation = 0x0,
    Text = 0x1,
    Binary = 0x2,
    Close = 0x8,
    Ping = 0x9,
    Pong = 0xA,
};

// Close codes the client sends or reports (RFC 6455 §7.4.1).
inline constexpr int kCloseNormal = 1000;
inline constexpr int kCloseGoingAway = 1001;
inline constexpr int kCloseProtocolError = 1002;
inline constexpr int kCloseNoStatus = 1005;
inline constexpr int kCloseAbnormal = 1006;  // no close frame: connect failed, refused handshake, drop, timeout
inline constexpr int kCloseTooBig = 1009;

// Sec-WebSocket-Accept for a Sec-WebSocket-Key: base64(SHA-1(key + GUID)).
std::string AcceptKey(std::string_view key);
std::string Base64(std::string_view bytes);

// One frame. mask != nullptr masks the payload with those 4 bytes (a client must; a server must not).
std::string EncodeFrame(Opcode op, std::string_view payload, bool fin = true, const std::uint8_t* mask = nullptr);

struct Frame
{
    Opcode op = Opcode::Text;
    bool fin = true;
    bool masked = false;
    std::string payload;  // unmasked
};

enum class DecodeResult
{
    NeedMore,
    Frame,
    Error,
};

// Takes one frame off the front of buffer. Error: reserved bits, an unknown opcode, a fragmented or long control
// frame, or a payload over maxPayload (*closeCode says which close to send).
DecodeResult DecodeFrame(std::string& buffer, Frame& out, std::size_t maxPayload, int* closeCode);

struct ClientOptions
{
    std::string url;                                           // ws://host:port/path
    std::vector<std::pair<std::string, std::string>> headers;  // e.g. Authorization: Bearer unc_...
    std::chrono::milliseconds connectTimeout{3000};            // TCP connect plus the upgrade
    std::chrono::milliseconds pingInterval{15000};             // 0 = no pings
    std::chrono::milliseconds idleTimeout{30000};              // nothing received for this long: dropped
    std::chrono::milliseconds closeTimeout{1000};              // our close frame unanswered: drop
    bool reconnect = true;
    std::chrono::milliseconds reconnectMin{250};  // doubles after each failure up to reconnectMax
    std::chrono::milliseconds reconnectMax{10000};
    std::size_t maxMessage = 16u << 20;  // a bigger incoming message closes with 1009
    std::size_t highWater = 4u << 20;    // Send* refuses while this much is queued
};

struct ParsedUrl
{
    std::string host;
    int port = 80;
    std::string path = "/";
};
// false (and *error) for anything but ws://host[:port][/path]; IPv6 as [::1].
bool ParseUrl(std::string_view url, ParsedUrl* out, std::string* error);

class Client
{
public:
    enum class State
    {
        Idle,        // before Start()
        Connecting,  // TCP connect or the upgrade
        Open,
        Closing,  // our close frame sent, waiting for the answer
        Waiting,  // between attempts
        Stopped,  // Close() or reconnect off: no more attempts
    };

    Client(net::EventLoop& loop, ClientOptions options);
    ~Client();
    Client(const Client&) = delete;
    Client& operator=(const Client&) = delete;

    std::function<void()> onOpen;
    std::function<void(std::string_view)> onText;
    std::function<void(std::string_view)> onBinary;
    // After every connection or attempt that ends: code (kCloseAbnormal when there was no close frame) and why.
    std::function<void(int, std::string_view)> onClose;

    void Start();
    // Closes (a close frame when open) and stops reconnecting.
    void Close(int code = kCloseNormal, std::string_view reason = {});

    bool SendText(std::string_view text);
    bool SendBinary(std::string_view bytes);

    [[nodiscard]] State GetState() const
    {
        return _state;
    }
    [[nodiscard]] bool IsOpen() const
    {
        return _state == State::Open;
    }
    [[nodiscard]] std::size_t Buffered() const
    {
        return _out.size() - _outSent;
    }
    [[nodiscard]] int Connections() const  // successful upgrades so far
    {
        return _connections;
    }
    [[nodiscard]] const ClientOptions& Options() const
    {
        return _options;
    }

private:
    void Connect();
    void OnIo(bool readable, bool writable, bool error);
    void OnConnected();
    bool ReadHandshake();
    void ReadFrames();
    void HandleFrame(Frame& frame);
    void Queue(Opcode op, std::string_view payload);
    void Flush();
    void UpdateInterest();
    void SendClose(int code, std::string_view reason);
    void Drop(int code, std::string_view reason);  // closes the socket, reports, schedules the next attempt
    void ScheduleReconnect();
    void CancelTimers();
    std::uint8_t NextRandomByte();

    net::EventLoop& _loop;
    ClientOptions _options;
    ParsedUrl _url;
    std::string _urlError;

    State _state = State::Idle;
    net::SocketHandle _socket = net::kInvalidSocket;
    bool _tcpConnected = false;
    bool _upgraded = false;
    std::string _key;

    std::string _in;
    std::string _out;
    std::size_t _outSent = 0;
    std::string _message;  // a fragmented message being assembled
    Opcode _messageOp = Opcode::Text;
    bool _inMessage = false;
    int _closeCode = kCloseNormal;  // what we sent, reported when the server just closes the connection
    std::string _closeReason;

    net::EventLoop::TimerId _deadline = 0;  // connect, close or reconnect
    net::EventLoop::TimerId _pinger = 0;
    net::EventLoop::Clock::time_point _lastRx{};
    std::chrono::milliseconds _backoff{0};
    int _connections = 0;
    std::uint32_t _rng;
};

}  // namespace sdb::ws
