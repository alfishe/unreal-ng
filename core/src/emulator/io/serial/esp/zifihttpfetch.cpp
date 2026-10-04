#include "emulator/io/serial/esp/zifihttpfetch.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>

#include "emulator/io/serial/esp/zifistate.h"

// The firmware's behavior (ZiFi-ESP32-S3-Zero 2e5ba83): src/net_client.cpp (httpGet, readHttpHeader,
// applyRedirect, receive) and src/wc_update_service.cpp (NetWcFetcher::get)

namespace
{
constexpr uint64_t kTlsConnectTimeoutUs = 24000000;   // openTls: connect(host, port, 12000) + handshake 12 s
constexpr uint64_t kConnectTimeoutUs = 8000000;       // open: connect(host, port, 8000)
constexpr uint64_t kHeaderTimeoutUs = 10000000;       // readHttpHeader: 10 s
constexpr uint64_t kHttpTimeoutUs = 60000000;         // NetWcFetcher kHttpTimeoutMs
constexpr uint64_t kBodyIdleTimeoutUs = 30000000;     // receive: kHttpBodyIdleTimeoutMs
constexpr size_t kHeaderSize = 2048;                  // httpHeader_[2049]
constexpr unsigned kMaxRedirects = 5;

bool StartsNoCase(const std::string& s, size_t at, const char* prefix)
{
    for (size_t i = 0; prefix[i]; ++i)
    {
        if (at + i >= s.size() || std::tolower(static_cast<unsigned char>(s[at + i])) != prefix[i])
            return false;
    }
    return true;
}

bool Ipv4(const std::string& s, uint32_t& out)
{
    unsigned a = 0, b = 0, c = 0, d = 0;
    char tail = 0;
    if (std::sscanf(s.c_str(), "%u.%u.%u.%u%c", &a, &b, &c, &d, &tail) != 4 || a > 255 || b > 255 || c > 255 || d > 255)
        return false;
    out = NetIp(static_cast<uint8_t>(a), static_cast<uint8_t>(b), static_cast<uint8_t>(c), static_cast<uint8_t>(d));
    return true;
}
}  // namespace

ZiFiHttpFetch::ZiFiHttpFetch(EspStack& stack, int slot) : _stack(stack), _slot(slot)
{
}

std::string ZiFiHttpFetch::Activity() const
{
    static const char* const kPhases[] = {"idle", "resolving", "connecting", "reading the header", "reading the body", "done"};
    std::string text = kPhases[static_cast<int>(_phase)];
    if (Busy())
        text += " " + _host + _path.substr(0, 60);
    if (_phase == Phase::Body)
        text += " (" + std::to_string(_result.body.size()) + " bytes)";
    return text;
}

void ZiFiHttpFetch::Start(const std::string& host, uint16_t port, const std::string& path, size_t capacity)
{
    Cancel();
    _result = Result();
    _refsValid = true;
    _host = host;
    _port = port;
    _path = path.empty() ? std::string("/") : path;
    _capacity = capacity;
    _tls = port == 443;   // httpGet: useTls = port == 443
    _redirects = 0;
    Connect();
}

void ZiFiHttpFetch::Connect()
{
    _phase = Phase::Resolve;
    _header.clear();
    _deadline = now() + micros(_tls ? kTlsConnectTimeoutUs : kConnectTimeoutUs);
    uint32_t addr = 0;
    if (Ipv4(_host, addr))
        return OnResolve(addr);
    _stack.ResolveAux(_host);
}

void ZiFiHttpFetch::Cancel()
{
    if (Busy())
    {
        _stack.Close(_slot);
        Fail("stopped");
    }
}

void ZiFiHttpFetch::Forget()
{
    _phase = Phase::Idle;
    _result = Result();
    _header.clear();
    _refsValid = true;
}

void ZiFiHttpFetch::Fail(const std::string& error)
{
    _stack.Close(_slot);
    _result.ok = false;
    _result.error = error;
    _result.body.clear();
    _result.refs.clear();
    _phase = Phase::Done;
}

void ZiFiHttpFetch::Finish()
{
    _stack.Close(_slot);
    _result.ok = true;
    if (!_refsValid)
        _result.refs.clear();
    _phase = Phase::Done;
}

ZiFiHttpFetch::Result ZiFiHttpFetch::Take()
{
    Result r = std::move(_result);
    _result = Result();
    _phase = Phase::Idle;
    return r;
}

void ZiFiHttpFetch::OnResolve(uint32_t addr)
{
    if (_phase != Phase::Resolve)
        return;
    if (!addr)
        return Fail(_tls ? "tls connect failed" : "connect failed");
    _stack.Close(_slot);
    if (_stack.OpenAt(_slot, true) < 0)
        return Fail("connect failed");
    _phase = Phase::Connect;
    _stack.Connect(_slot, NetEndpoint{addr, _port}, _tls ? _host : std::string());
}

void ZiFiHttpFetch::OnConnect(bool ok)
{
    if (_phase != Phase::Connect)
        return;
    if (!ok)
        return Fail(_tls ? "tls connect failed" : "connect failed");
    // httpGet's request (TLS, no proxy): the default port is left out of Host
    std::string host = _host;
    if (_port != (_tls ? 443 : 80) && _port != 0)
        host += ":" + std::to_string(_port);
    const std::string request = "GET " + _path + " HTTP/1.0\r\nHost: " + host +
                                "\r\nUser-Agent: ZiFi (ZX Evo)\r\nAccept: */*\r\nAccept-Encoding: identity\r\n"
                                "Connection: close\r\n\r\n";
    if (request.size() >= 768)   // httpRequest_[768]
        return Fail("request too long");
    _stack.Send(_slot, reinterpret_cast<const uint8_t*>(request.data()), static_cast<uint32_t>(request.size()));
    _phase = Phase::Header;
    _deadline = now() + micros(kHeaderTimeoutUs);
    Poll();
}

void ZiFiHttpFetch::AppendBody(const std::vector<EspStack::RxByte>& bytes)
{
    for (const EspStack::RxByte& b : bytes)
    {
        _result.body.push_back(b.value);
        if (!_refsValid)
            continue;
        if (b.source == 0)
        {
            _refsValid = false;
            continue;
        }
        netstate::Reference* last = _result.refs.empty() ? nullptr : &_result.refs.back();
        if (last && last->source == b.source && last->sourceOffset + last->length == b.offset)
            ++last->length;
        else
            _result.refs.push_back({b.source, b.offset, 1});
    }
}

void ZiFiHttpFetch::ParseHeader()
{
    std::string text;
    for (const EspStack::RxByte& b : _header)
        text.push_back(static_cast<char>(b.value));
    uint16_t status = 0;
    const size_t space = text.find(' ');
    if (space != std::string::npos)
        status = static_cast<uint16_t>(std::strtoul(text.c_str() + space + 1, nullptr, 10));
    uint32_t contentLength = 0;
    bool lengthKnown = false, chunked = false;
    std::string location;
    for (size_t line = 0; line < text.size();)
    {
        const size_t eol = text.find("\r\n", line);
        if (eol == std::string::npos)
            break;
        if (StartsNoCase(text, line, "content-length:"))
        {
            size_t v = line + 15;
            while (v < eol && (text[v] == ' ' || text[v] == '\t'))
                ++v;
            char* parsedEnd = nullptr;
            const unsigned long parsed = std::strtoul(text.c_str() + v, &parsedEnd, 10);
            size_t e = static_cast<size_t>(parsedEnd - text.c_str());
            while (e < eol && (text[e] == ' ' || text[e] == '\t'))
                ++e;
            if (e == v || e != eol)
                return Fail("bad content length");
            contentLength = static_cast<uint32_t>(parsed);
            lengthKnown = true;
        }
        else if (StartsNoCase(text, line, "transfer-encoding:") && text.substr(line, eol - line).find("hunked") != std::string::npos)
            chunked = true;
        else if (StartsNoCase(text, line, "location:"))
        {
            size_t v = line + 9;
            while (v < eol && (text[v] == ' ' || text[v] == '\t'))
                ++v;
            size_t e = eol;
            while (e > v && (text[e - 1] == ' ' || text[e - 1] == '\t'))
                --e;
            if (e - v < 384)   // redirectLocation_[384]
                location = text.substr(v, e - v);
        }
        line = eol + 2;
    }
    const bool redirect = status == 301 || status == 302 || status == 303 || status == 307 || status == 308;
    if (chunked && !redirect)
        return Fail("chunked unsupported");
    _result.status = status;
    if (!redirect)
    {
        _lengthKnown = lengthKnown;
        _expected = contentLength;
        _phase = Phase::Body;
        _bodyStart = now();
        _bodyActivity = _bodyStart;
        return Poll();
    }
    if (location.empty())
        return Fail("redirect no location");
    if (_redirects >= kMaxRedirects - 1)
        return Fail("too many redirects");
    _stack.Close(_slot);
    // applyRedirect
    const bool wasTls = _tls;
    bool absolute = false;
    size_t authority = 0;
    if (StartsNoCase(location, 0, "https://"))
    {
        absolute = true;
        _tls = true;
        authority = 8;
    }
    else if (StartsNoCase(location, 0, "http://"))
    {
        absolute = true;
        _tls = false;
        authority = 7;
    }
    else if (location.rfind("//", 0) == 0)
    {
        absolute = true;
        authority = 2;
    }
    if (wasTls && !_tls)
        return Fail("redirect tls downgrade");
    if (absolute)
    {
        size_t end = authority;
        while (end < location.size() && location[end] != '/' && location[end] != '?' && location[end] != '#')
            ++end;
        const std::string auth = location.substr(authority, end - authority);
        const size_t colon = auth.rfind(':');
        const std::string host = colon == std::string::npos ? auth : auth.substr(0, colon);
        if (host.empty() || host.size() >= 254)
            return Fail("redirect host");
        _host = host;
        _port = _tls ? 443 : 80;
        if (colon != std::string::npos)
        {
            char* portEnd = nullptr;
            const std::string portText = auth.substr(colon + 1);
            const unsigned long parsed = std::strtoul(portText.c_str(), &portEnd, 10);
            if (*portEnd != 0 || parsed == 0 || parsed > 65535)
                return Fail("redirect port");
            _port = static_cast<uint16_t>(parsed);
        }
        if (end < location.size() && location[end] == '/')
            _path = location.substr(end);
        else if (end < location.size() && location[end] == '?')
            _path = "/" + location.substr(end);
        else
            _path = "/";
    }
    else if (location[0] == '/')
        _path = location;
    else if (location[0] == '?')
        _path = _path.substr(0, _path.find('?')) + location;
    else
    {
        const size_t slash = _path.rfind('/');
        _path = (slash == std::string::npos ? std::string("/") : _path.substr(0, slash + 1)) + location;
    }
    _path = _path.substr(0, _path.find('#'));
    if (_path.empty())
        _path = "/";
    if (_path.size() >= 384)
        return Fail("redirect path");
    ++_redirects;
    _result = Result();
    _refsValid = true;
    Connect();
}

void ZiFiHttpFetch::Poll()
{
    const uint64_t t = now();
    switch (_phase)
    {
        case Phase::Idle:
        case Phase::Done: return;
        case Phase::Resolve:
        case Phase::Connect:
            if (t >= _deadline)
                Fail(_tls ? "tls connect failed" : "connect failed");
            return;
        case Phase::Header:
        {
            const bool valid = _stack.Valid(_slot);
            while (valid && !_stack.GetSlot(_slot).rx.empty())
            {
                if (_header.size() >= kHeaderSize)
                    return Fail("header too long");
                const std::vector<EspStack::RxByte> one = _stack.ReadRx(_slot, 1);
                _header.push_back(one[0]);
                const size_t n = _header.size();
                if (n >= 4 && _header[n - 4].value == '\r' && _header[n - 3].value == '\n' && _header[n - 2].value == '\r' &&
                    _header[n - 1].value == '\n')
                    return ParseHeader();
            }
            if (!valid || _stack.GetSlot(_slot).finSeen)
                return Fail("closed in header");
            if (t >= _deadline)
                Fail("header timeout");
            return;
        }
        case Phase::Body:
            for (;;)
            {
                // NetWcFetcher::get: the body must stay below the capacity
                if (_result.body.size() >= _capacity)
                    return Fail("body too long");
                // receive: Content-Length ends the body
                if (_lengthKnown && _result.body.size() >= _expected)
                    return Finish();
                const bool valid = _stack.Valid(_slot);
                const size_t have = valid ? _stack.GetSlot(_slot).rx.size() : 0;
                if (have)
                {
                    size_t wanted = std::min(have, _capacity - _result.body.size());
                    if (_lengthKnown)
                        wanted = std::min<size_t>(wanted, _expected - _result.body.size());
                    AppendBody(_stack.ReadRx(_slot, static_cast<uint32_t>(wanted)));
                    _bodyActivity = t;
                    continue;
                }
                if (!valid || _stack.GetSlot(_slot).finSeen)
                    return Finish();
                if (t >= _bodyActivity + micros(kBodyIdleTimeoutUs))
                    return Fail("body timeout");
                if (t >= _bodyStart + micros(kHttpTimeoutUs))
                    return Fail("http timeout");
                return;
            }
    }
}

// --- TTD -----------------------------------------------------------------------------------------------------------

namespace
{
constexpr uint8_t kStateVersion = 1;
}  // namespace

void ZiFiHttpFetch::SaveBody(ZiFiStateWriter& w, const std::vector<uint8_t>& body, const std::vector<netstate::Reference>& refs)
{
    // By journal reference when every byte has one, else the bytes
    w.Bool(!refs.empty() || body.empty());
    if (!refs.empty() || body.empty())
    {
        w.U32(static_cast<uint32_t>(refs.size()));
        for (const netstate::Reference& r : refs)
            w.U32(r.source), w.U32(r.sourceOffset), w.U32(r.length);
    }
    else
        w.Bytes(body);
}

bool ZiFiHttpFetch::LoadBody(ZiFiStateReader& r, const EspStack::ByteSource& bytes, std::vector<uint8_t>& body,
                             std::vector<netstate::Reference>& refs)
{
    body.clear();
    refs.clear();
    if (!r.Bool())
    {
        body = r.Bytes();
        return true;
    }
    bool complete = true;
    const uint32_t count = r.U32();
    for (uint32_t i = 0; i < count && r.Ok(); ++i)
    {
        netstate::Reference ref{};
        ref.source = r.U32(), ref.sourceOffset = r.U32(), ref.length = r.U32();
        refs.push_back(ref);
        std::vector<uint8_t> chunk;
        if (!bytes || !bytes(ref.source, ref.sourceOffset, ref.length, chunk) || chunk.size() != ref.length)
        {
            complete = false;
            continue;
        }
        body.insert(body.end(), chunk.begin(), chunk.end());
    }
    return complete;
}

void ZiFiHttpFetch::Save(ZiFiStateWriter& w) const
{
    w.U8(kStateVersion);
    w.U8(static_cast<uint8_t>(_phase));
    w.Str(_host), w.U16(_port), w.Str(_path);
    w.U32(static_cast<uint32_t>(_capacity));
    w.Bool(_tls);
    w.U8(_redirects);
    w.U64(_deadline), w.U64(_bodyStart), w.U64(_bodyActivity);
    w.Bool(_lengthKnown);
    w.U32(_expected);
    std::vector<uint8_t> header;
    for (const EspStack::RxByte& b : _header)
        header.push_back(b.value);
    w.Bytes(header);
    w.Bool(_result.ok);
    w.U16(_result.status);
    w.Str(_result.error);
    SaveBody(w, _result.body, _refsValid ? _result.refs : std::vector<netstate::Reference>());
    w.Bool(_refsValid);
}

bool ZiFiHttpFetch::Load(ZiFiStateReader& r, const EspStack::ByteSource& bytes)
{
    if (r.U8() != kStateVersion)
        return false;
    _phase = static_cast<Phase>(r.U8());
    _host = r.Str(256), _port = r.U16(), _path = r.Str(512);
    _capacity = r.U32();
    _tls = r.Bool();
    _redirects = r.U8();
    _deadline = r.U64(), _bodyStart = r.U64(), _bodyActivity = r.U64();
    _lengthKnown = r.Bool();
    _expected = r.U32();
    _header.clear();
    for (uint8_t b : r.Bytes(kHeaderSize + 1))
        _header.push_back({b, 0, 0});
    _result = Result();
    _result.ok = r.Bool();
    _result.status = r.U16();
    _result.error = r.Str(128);
    const bool complete = LoadBody(r, bytes, _result.body, _result.refs);
    _refsValid = r.Bool();
    return complete && r.Ok();
}
