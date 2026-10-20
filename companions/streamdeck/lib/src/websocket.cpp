#include "sdb/websocket.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <random>

namespace sdb::ws
{
namespace
{

constexpr std::string_view kGuid = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
constexpr std::size_t kMaxHandshake = 16 * 1024;

// SHA-1 (FIPS 180-4), for the handshake only.
std::array<std::uint8_t, 20> Sha1(std::string_view data)
{
    std::uint32_t h[5] = {0x67452301, 0xEFCDAB89, 0x98BADCFE, 0x10325476, 0xC3D2E1F0};
    std::string msg(data);
    const std::uint64_t bits = static_cast<std::uint64_t>(data.size()) * 8;
    msg += static_cast<char>(0x80);
    while (msg.size() % 64 != 56)
        msg += '\0';
    for (int i = 7; i >= 0; --i)
        msg += static_cast<char>((bits >> (i * 8)) & 0xFF);
    auto rol = [](std::uint32_t v, int n) { return (v << n) | (v >> (32 - n)); };
    for (std::size_t chunk = 0; chunk < msg.size(); chunk += 64)
    {
        std::uint32_t w[80];
        for (int i = 0; i < 16; ++i)
        {
            const auto* p =
                reinterpret_cast<const unsigned char*>(msg.data() + chunk + static_cast<std::size_t>(i) * 4);
            w[i] = (std::uint32_t{p[0]} << 24) | (std::uint32_t{p[1]} << 16) | (std::uint32_t{p[2]} << 8) | p[3];
        }
        for (int i = 16; i < 80; ++i)
            w[i] = rol(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
        std::uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4];
        for (int i = 0; i < 80; ++i)
        {
            std::uint32_t f = 0, k = 0;
            if (i < 20)
            {
                f = (b & c) | (~b & d);
                k = 0x5A827999;
            }
            else if (i < 40)
            {
                f = b ^ c ^ d;
                k = 0x6ED9EBA1;
            }
            else if (i < 60)
            {
                f = (b & c) | (b & d) | (c & d);
                k = 0x8F1BBCDC;
            }
            else
            {
                f = b ^ c ^ d;
                k = 0xCA62C1D6;
            }
            const std::uint32_t t = rol(a, 5) + f + e + k + w[i];
            e = d;
            d = c;
            c = rol(b, 30);
            b = a;
            a = t;
        }
        h[0] += a;
        h[1] += b;
        h[2] += c;
        h[3] += d;
        h[4] += e;
    }
    std::array<std::uint8_t, 20> out{};
    for (int i = 0; i < 5; ++i)
        for (int j = 0; j < 4; ++j)
            out[static_cast<std::size_t>(i * 4 + j)] = static_cast<std::uint8_t>(h[i] >> (24 - j * 8));
    return out;
}

bool EqualsNoCase(std::string_view a, std::string_view b)
{
    return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(), [](char x, char y) {
               return std::tolower(static_cast<unsigned char>(x)) == std::tolower(static_cast<unsigned char>(y));
           });
}

bool ContainsTokenNoCase(std::string_view list, std::string_view token)
{
    std::size_t i = 0;
    while (i <= list.size())
    {
        std::size_t j = list.find(',', i);
        if (j == std::string_view::npos)
            j = list.size();
        std::string_view item = list.substr(i, j - i);
        while (!item.empty() && item.front() == ' ')
            item.remove_prefix(1);
        while (!item.empty() && item.back() == ' ')
            item.remove_suffix(1);
        if (EqualsNoCase(item, token))
            return true;
        i = j + 1;
    }
    return false;
}

std::string HeaderValue(std::string_view head, std::string_view name)
{
    std::size_t pos = head.find("\r\n");
    while (pos != std::string_view::npos && pos + 2 < head.size())
    {
        const std::size_t next = head.find("\r\n", pos + 2);
        const std::string_view line =
            head.substr(pos + 2, next == std::string_view::npos ? std::string_view::npos : next - pos - 2);
        if (const std::size_t colon = line.find(':');
            colon != std::string_view::npos && EqualsNoCase(line.substr(0, colon), name))
        {
            std::string_view v = line.substr(colon + 1);
            while (!v.empty() && v.front() == ' ')
                v.remove_prefix(1);
            return std::string(v);
        }
        pos = next;
    }
    return {};
}

bool IsControl(Opcode op)
{
    return (static_cast<std::uint8_t>(op) & 0x8) != 0;
}

}  // namespace

std::string Base64(std::string_view bytes)
{
    static constexpr char kTable[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve((bytes.size() + 2) / 3 * 4);
    for (std::size_t i = 0; i < bytes.size(); i += 3)
    {
        const std::size_t n = std::min<std::size_t>(3, bytes.size() - i);
        std::uint32_t v = 0;
        for (std::size_t j = 0; j < 3; ++j)
            v = (v << 8) | (j < n ? static_cast<unsigned char>(bytes[i + j]) : 0u);
        out += kTable[(v >> 18) & 63];
        out += kTable[(v >> 12) & 63];
        out += n > 1 ? kTable[(v >> 6) & 63] : '=';
        out += n > 2 ? kTable[v & 63] : '=';
    }
    return out;
}

std::string AcceptKey(std::string_view key)
{
    std::string joined(key);
    joined += kGuid;
    const auto digest = Sha1(joined);
    return Base64(std::string_view(reinterpret_cast<const char*>(digest.data()), digest.size()));
}

std::string EncodeFrame(Opcode op, std::string_view payload, bool fin, const std::uint8_t* mask)
{
    std::string f;
    f.reserve(payload.size() + 14);
    f += static_cast<char>((fin ? 0x80 : 0x00) | static_cast<std::uint8_t>(op));
    const std::uint8_t maskBit = mask ? 0x80 : 0x00;
    const std::uint64_t n = payload.size();
    if (n < 126)
    {
        f += static_cast<char>(maskBit | n);
    }
    else if (n <= 0xFFFF)
    {
        f += static_cast<char>(maskBit | 126);
        f += static_cast<char>((n >> 8) & 0xFF);
        f += static_cast<char>(n & 0xFF);
    }
    else
    {
        f += static_cast<char>(maskBit | 127);
        for (int i = 7; i >= 0; --i)
            f += static_cast<char>((n >> (i * 8)) & 0xFF);
    }
    if (!mask)
    {
        f += payload;
        return f;
    }
    f.append(reinterpret_cast<const char*>(mask), 4);
    const std::size_t at = f.size();
    f += payload;
    for (std::size_t i = 0; i < payload.size(); ++i)
        f[at + i] = static_cast<char>(static_cast<std::uint8_t>(f[at + i]) ^ mask[i & 3]);
    return f;
}

DecodeResult DecodeFrame(std::string& buffer, Frame& out, std::size_t maxPayload, int* closeCode)
{
    auto fail = [&](int code) {
        if (closeCode)
            *closeCode = code;
        return DecodeResult::Error;
    };
    if (buffer.size() < 2)
        return DecodeResult::NeedMore;
    const auto b0 = static_cast<std::uint8_t>(buffer[0]);
    const auto b1 = static_cast<std::uint8_t>(buffer[1]);
    if (b0 & 0x70)
        return fail(kCloseProtocolError);  // no extension was negotiated
    const auto op = static_cast<Opcode>(b0 & 0x0F);
    switch (op)
    {
        case Opcode::Continuation:
        case Opcode::Text:
        case Opcode::Binary:
        case Opcode::Close:
        case Opcode::Ping:
        case Opcode::Pong:
            break;
        default:
            return fail(kCloseProtocolError);
    }
    const bool fin = (b0 & 0x80) != 0;
    const bool masked = (b1 & 0x80) != 0;
    std::uint64_t n = b1 & 0x7F;
    std::size_t at = 2;
    if (n == 126)
    {
        if (buffer.size() < 4)
            return DecodeResult::NeedMore;
        n = (std::uint64_t{static_cast<std::uint8_t>(buffer[2])} << 8) | static_cast<std::uint8_t>(buffer[3]);
        at = 4;
    }
    else if (n == 127)
    {
        if (buffer.size() < 10)
            return DecodeResult::NeedMore;
        n = 0;
        for (int i = 0; i < 8; ++i)
            n = (n << 8) | static_cast<std::uint8_t>(buffer[static_cast<std::size_t>(2 + i)]);
        if (n >> 63)
            return fail(kCloseProtocolError);
        at = 10;
    }
    if (IsControl(op) && (!fin || n > 125))
        return fail(kCloseProtocolError);
    if (n > maxPayload)
        return fail(kCloseTooBig);
    std::uint8_t mask[4] = {};
    if (masked)
    {
        if (buffer.size() < at + 4)
            return DecodeResult::NeedMore;
        for (int i = 0; i < 4; ++i)
            mask[i] = static_cast<std::uint8_t>(buffer[at + static_cast<std::size_t>(i)]);
        at += 4;
    }
    if (buffer.size() - at < n)
        return DecodeResult::NeedMore;
    out.op = op;
    out.fin = fin;
    out.masked = masked;
    out.payload.assign(buffer, at, static_cast<std::size_t>(n));
    if (masked)
        for (std::size_t i = 0; i < out.payload.size(); ++i)
            out.payload[i] = static_cast<char>(static_cast<std::uint8_t>(out.payload[i]) ^ mask[i & 3]);
    buffer.erase(0, at + static_cast<std::size_t>(n));
    return DecodeResult::Frame;
}

bool ParseUrl(std::string_view url, ParsedUrl* out, std::string* error)
{
    auto fail = [&](const char* why) {
        if (error)
            *error = std::string(why) + ": " + std::string(url);
        return false;
    };
    constexpr std::string_view kScheme = "ws://";
    if (url.substr(0, kScheme.size()) != kScheme)
        return fail(url.substr(0, 6) == "wss://" ? "wss:// is not supported" : "not a ws:// URL");
    std::string_view rest = url.substr(kScheme.size());
    const std::size_t slash = rest.find('/');
    std::string_view authority = rest.substr(0, slash);
    ParsedUrl u;
    u.path = slash == std::string_view::npos ? "/" : std::string(rest.substr(slash));
    std::string_view port;
    if (!authority.empty() && authority.front() == '[')
    {
        const std::size_t close = authority.find(']');
        if (close == std::string_view::npos)
            return fail("bad IPv6 host");
        u.host = std::string(authority.substr(1, close - 1));
        if (close + 1 < authority.size())
        {
            if (authority[close + 1] != ':')
                return fail("bad host");
            port = authority.substr(close + 2);
        }
    }
    else
    {
        const std::size_t colon = authority.rfind(':');
        u.host = std::string(authority.substr(0, colon));
        if (colon != std::string_view::npos)
            port = authority.substr(colon + 1);
    }
    if (u.host.empty())
        return fail("no host");
    if (!port.empty())
    {
        int p = 0;
        for (char c : port)
        {
            if (c < '0' || c > '9')
                return fail("bad port");
            p = p * 10 + (c - '0');
            if (p > 65535)
                return fail("bad port");
        }
        u.port = p;
    }
    *out = std::move(u);
    return true;
}

Client::Client(net::EventLoop& loop, ClientOptions options)
    : _loop(loop), _options(std::move(options)), _rng(std::random_device{}())
{
    ParseUrl(_options.url, &_url, &_urlError);
}

Client::~Client()
{
    CancelTimers();
    if (_pinger)
        _loop.Cancel(_pinger);
    if (_socket != net::kInvalidSocket)
    {
        _loop.Unwatch(_socket);
        net::CloseSocket(_socket);
    }
}

void Client::Start()
{
    if (_state != State::Idle && _state != State::Stopped)
        return;
    _backoff = std::chrono::milliseconds(0);
    if (_options.pingInterval.count() > 0 && !_pinger)
    {
        _pinger = _loop.Every(_options.pingInterval, [this] {
            if (_state != State::Open)
                return;
            if (net::EventLoop::Clock::now() - _lastRx > _options.idleTimeout)
            {
                Drop(kCloseAbnormal, "no traffic from the server");
                return;
            }
            Queue(Opcode::Ping, {});
        });
    }
    Connect();
}

void Client::Close(int code, std::string_view reason)
{
    const State was = _state;
    _options.reconnect = false;
    if (was == State::Open)
    {
        SendClose(code, reason);
        return;
    }
    if (was == State::Closing || was == State::Stopped)
        return;
    CancelTimers();
    if (_socket != net::kInvalidSocket)
        Drop(kCloseAbnormal, "closed before the connection opened");
    _state = State::Stopped;
}

bool Client::SendText(std::string_view text)
{
    if (_state != State::Open || Buffered() > _options.highWater)
        return false;
    Queue(Opcode::Text, text);
    return true;
}

bool Client::SendBinary(std::string_view bytes)
{
    if (_state != State::Open || Buffered() > _options.highWater)
        return false;
    Queue(Opcode::Binary, bytes);
    return true;
}

void Client::Connect()
{
    if (!_urlError.empty())
    {
        _state = State::Stopped;
        if (onClose)
            onClose(kCloseAbnormal, _urlError);
        return;
    }
    _state = State::Connecting;
    _in.clear();
    _out.clear();
    _outSent = 0;
    _message.clear();
    _inMessage = false;
    _tcpConnected = false;
    _upgraded = false;

    std::string error;
    _socket = net::ConnectStart(_url.host, _url.port, &error);
    if (_socket == net::kInvalidSocket)
    {
        Drop(kCloseAbnormal, error);
        return;
    }
    _loop.Watch(_socket, false, true, [this](bool r, bool w, bool e) { OnIo(r, w, e); });
    _deadline = _loop.After(_options.connectTimeout, [this] {
        _deadline = 0;
        Drop(kCloseAbnormal, "connect timed out");
    });
}

void Client::OnIo(bool readable, bool writable, bool error)
{
    if (!_tcpConnected)
    {
        if (!writable && !error)
            return;
        if (const int e = net::SocketError(_socket); e != 0 || error)
        {
            Drop(kCloseAbnormal, "connect " + _url.host + ":" + std::to_string(_url.port) + " failed");
            return;
        }
        OnConnected();
        return;
    }
    if (readable || error)
    {
        char buf[16384];
        bool gone = false;
        for (;;)
        {
            const long n = net::Recv(_socket, buf, sizeof buf);
            if (n == 0)
                break;
            if (n < 0)
            {
                gone = true;
                break;
            }
            _in.append(buf, static_cast<std::size_t>(n));
            _lastRx = net::EventLoop::Clock::now();
        }
        // What arrived before the end is handled first: a close frame then reports its own code.
        if (_upgraded || ReadHandshake())
            ReadFrames();
        if (_socket == net::kInvalidSocket)
            return;
        if (gone)
        {
            // After our close frame the end of the stream is the answer: some servers (Drogon) close the TCP
            // connection instead of echoing the frame.
            if (_state == State::Closing)
                Drop(_closeCode, _closeReason);
            else
                Drop(kCloseAbnormal, "connection lost");
            return;
        }
    }
    if (writable)
        Flush();
}

void Client::OnConnected()
{
    _tcpConnected = true;
    std::string nonce(16, '\0');
    for (auto& c : nonce)
        c = static_cast<char>(NextRandomByte());
    _key = Base64(nonce);
    const bool v6 = _url.host.find(':') != std::string::npos;
    std::string host = v6 ? "[" + _url.host + "]" : _url.host;
    if (_url.port != 80)
        host += ":" + std::to_string(_url.port);
    std::string req = "GET " + _url.path + " HTTP/1.1\r\nHost: " + host +
                      "\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Key: " + _key +
                      "\r\nSec-WebSocket-Version: 13\r\n";
    for (const auto& [k, v] : _options.headers)
        req += k + ": " + v + "\r\n";
    req += "\r\n";
    _out += req;
    Flush();
}

bool Client::ReadHandshake()
{
    const std::size_t end = _in.find("\r\n\r\n");
    if (end == std::string::npos)
    {
        if (_in.size() > kMaxHandshake)
            Drop(kCloseAbnormal, "upgrade answer too long");
        return false;
    }
    const std::string head = _in.substr(0, end);
    _in.erase(0, end + 4);
    const std::size_t sp = head.find(' ');
    const int status = sp == std::string::npos ? 0 : std::atoi(head.c_str() + sp + 1);
    if (status != 101)
    {
        const std::size_t eol = head.find("\r\n");
        Drop(kCloseAbnormal,
             "upgrade refused: " + head.substr(sp == std::string::npos ? 0 : sp + 1,
                                               eol == std::string::npos ? std::string::npos : eol - sp - 1));
        return false;
    }
    if (!EqualsNoCase(HeaderValue(head, "Upgrade"), "websocket") ||
        !ContainsTokenNoCase(HeaderValue(head, "Connection"), "upgrade") ||
        HeaderValue(head, "Sec-WebSocket-Accept") != AcceptKey(_key))
    {
        Drop(kCloseAbnormal, "bad upgrade answer");
        return false;
    }
    _upgraded = true;
    if (_deadline)
    {
        _loop.Cancel(_deadline);
        _deadline = 0;
    }
    _state = State::Open;
    _backoff = std::chrono::milliseconds(0);
    _lastRx = net::EventLoop::Clock::now();
    ++_connections;
    UpdateInterest();
    if (onOpen)
        onOpen();
    return _socket != net::kInvalidSocket;
}

void Client::ReadFrames()
{
    while (_socket != net::kInvalidSocket && _upgraded)
    {
        Frame frame;
        int code = 0;
        const DecodeResult r = DecodeFrame(_in, frame, _options.maxMessage, &code);
        if (r == DecodeResult::NeedMore)
            return;
        if (r == DecodeResult::Error)
        {
            SendClose(code, code == kCloseTooBig ? "message too big" : "protocol error");
            _in.clear();
            return;
        }
        if (frame.masked)
        {
            SendClose(kCloseProtocolError, "masked frame from the server");
            _in.clear();
            return;
        }
        HandleFrame(frame);
    }
}

void Client::HandleFrame(Frame& frame)
{
    switch (frame.op)
    {
        case Opcode::Ping:
            if (_state == State::Open)
                Queue(Opcode::Pong, frame.payload);
            return;
        case Opcode::Pong:
            return;
        case Opcode::Close: {
            int code = kCloseNoStatus;
            std::string reason;
            if (frame.payload.size() >= 2)
            {
                code = (static_cast<std::uint8_t>(frame.payload[0]) << 8) | static_cast<std::uint8_t>(frame.payload[1]);
                reason = frame.payload.substr(2);
            }
            if (_state == State::Open)
                Queue(Opcode::Close, frame.payload.substr(0, 2));  // echo the code
            Flush();
            Drop(code, reason);
            return;
        }
        case Opcode::Text:
        case Opcode::Binary:
            if (_inMessage)
            {
                SendClose(kCloseProtocolError, "new message inside a fragmented one");
                return;
            }
            if (frame.fin)
            {
                if (_state != State::Open)
                    return;
                if (frame.op == Opcode::Text && onText)
                    onText(frame.payload);
                else if (frame.op == Opcode::Binary && onBinary)
                    onBinary(frame.payload);
                return;
            }
            _inMessage = true;
            _messageOp = frame.op;
            _message = std::move(frame.payload);
            return;
        case Opcode::Continuation:
            if (!_inMessage)
            {
                SendClose(kCloseProtocolError, "continuation without a message");
                return;
            }
            if (_message.size() + frame.payload.size() > _options.maxMessage)
            {
                SendClose(kCloseTooBig, "message too big");
                return;
            }
            _message += frame.payload;
            if (!frame.fin)
                return;
            _inMessage = false;
            if (_state == State::Open)
            {
                const std::string message = std::move(_message);
                _message.clear();
                if (_messageOp == Opcode::Text && onText)
                    onText(message);
                else if (_messageOp == Opcode::Binary && onBinary)
                    onBinary(message);
            }
            return;
    }
}

std::uint8_t Client::NextRandomByte()
{
    _rng = _rng * 1664525u + 1013904223u;  // masks and the nonce need no secrecy (RFC 6455 §10.3: unpredictable)
    return static_cast<std::uint8_t>(_rng >> 24);
}

void Client::Queue(Opcode op, std::string_view payload)
{
    std::uint8_t mask[4];
    for (auto& b : mask)
        b = NextRandomByte();
    _out += EncodeFrame(op, payload, true, mask);
    Flush();
}

void Client::Flush()
{
    while (_socket != net::kInvalidSocket && _outSent < _out.size())
    {
        const long n = net::Send(_socket, _out.data() + _outSent, _out.size() - _outSent);
        if (n == 0)
            break;
        if (n < 0)
        {
            Drop(kCloseAbnormal, "connection lost");
            return;
        }
        _outSent += static_cast<std::size_t>(n);
    }
    if (_outSent == _out.size())
    {
        _out.clear();
        _outSent = 0;
    }
    else if (_outSent > (64u << 10))
    {
        _out.erase(0, _outSent);
        _outSent = 0;
    }
    UpdateInterest();
}

void Client::UpdateInterest()
{
    if (_socket != net::kInvalidSocket && _tcpConnected)
        _loop.Modify(_socket, true, Buffered() > 0);
}

void Client::SendClose(int code, std::string_view reason)
{
    if (_state != State::Open)
        return;
    std::string payload;
    payload += static_cast<char>((code >> 8) & 0xFF);
    payload += static_cast<char>(code & 0xFF);
    payload += reason.substr(0, 123);
    Queue(Opcode::Close, payload);
    if (_socket == net::kInvalidSocket)
        return;
    _state = State::Closing;
    _closeCode = code;
    _closeReason = std::string(reason);
    CancelTimers();
    _deadline = _loop.After(_options.closeTimeout, [this, code, r = std::string(reason)] {
        _deadline = 0;
        Drop(code, r);
    });
}

void Client::Drop(int code, std::string_view reason)
{
    CancelTimers();
    if (_socket != net::kInvalidSocket)
    {
        _loop.Unwatch(_socket);
        net::CloseSocket(_socket);
        _socket = net::kInvalidSocket;
    }
    _upgraded = false;
    _tcpConnected = false;
    _inMessage = false;
    _message.clear();
    _out.clear();
    _outSent = 0;
    _state = _options.reconnect ? State::Waiting : State::Stopped;
    const std::string why(reason);
    if (onClose)
        onClose(code, why);
    if (_state == State::Waiting)
        ScheduleReconnect();
}

void Client::ScheduleReconnect()
{
    _backoff = _backoff.count() == 0 ? _options.reconnectMin : std::min(_backoff * 2, _options.reconnectMax);
    _deadline = _loop.After(_backoff, [this] {
        _deadline = 0;
        if (_state == State::Waiting)
            Connect();
    });
}

void Client::CancelTimers()
{
    if (_deadline)
    {
        _loop.Cancel(_deadline);
        _deadline = 0;
    }
}

}  // namespace sdb::ws
