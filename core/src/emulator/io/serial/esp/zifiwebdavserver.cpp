#include "emulator/io/serial/esp/zifiwebdavserver.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>

#include "emulator/io/serial/esp/espstack.h"
#include "emulator/io/serial/esp/zifibridgehost.h"
#include "emulator/io/serial/esp/zifistate.h"

// Every behavior here is the firmware's: ZiFi-ESP-01S-Native-C-Project 90834e4 src/webdav_server.cpp. The
// firmware handles a request to its end in one call; here the request is a job whose step waits for the VFS or
// for more TCP bytes.

namespace
{
constexpr uint64_t kHeaderTimeoutUs = 10000000;           // kHeaderTimeoutMs
constexpr uint64_t kRequestBodyIdleTimeoutUs = 10000000;  // kRequestBodyIdleTimeoutMs
constexpr uint64_t kPutIdleTimeoutUs = 60000000;          // kPutIdleTimeoutMs
constexpr size_t kHeaderSize = 2048;
constexpr size_t kMaxPath = 255;
constexpr size_t kIoSize = 512;
constexpr size_t kUploadSlotSize = 256;
constexpr size_t kUploadSlotCount = 4;
const char* const kAllow = "Allow: OPTIONS, PROPFIND, GET, HEAD, PUT, DELETE, MKCOL\r\n";

int HexValue(char ch)
{
    if (ch >= '0' && ch <= '9')
        return ch - '0';
    ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    return ch >= 'a' && ch <= 'f' ? ch - 'a' + 10 : -1;
}

bool EqualsNoCase(const std::string& a, const char* b)
{
    size_t i = 0;
    for (; i < a.size() && b[i]; ++i)
    {
        if (std::tolower(static_cast<unsigned char>(a[i])) != std::tolower(static_cast<unsigned char>(b[i])))
            return false;
    }
    return i == a.size() && b[i] == 0;
}

enum : uint8_t
{
    kBegin = 0,
    kStat,
    kOpenDir,
    kReadDir,
    kOpen,
    kRead,
    kWrite,
    kClose,
    kRemove,
    kAnswer,
};
}  // namespace

ZiFiWebDavServer::ZiFiWebDavServer(ZiFiBridgeHost& host) : _host(host)
{
}

uint64_t ZiFiWebDavServer::Now() const
{
    return _host.BridgeNow();
}

uint64_t ZiFiWebDavServer::Us(uint64_t us) const
{
    return _host.BridgeMicros(us);
}

bool ZiFiWebDavServer::ClientConnectedNow() const
{
    return const_cast<ZiFiBridgeHost&>(_host).BridgeStack().Established(kClientSlot);
}

size_t ZiFiWebDavServer::ClientAvailable() const
{
    const EspStack& stack = const_cast<ZiFiBridgeHost&>(_host).BridgeStack();
    return stack.Valid(kClientSlot) ? stack.GetSlot(kClientSlot).rx.size() : 0;
}

bool ZiFiWebDavServer::Start(uint16_t port, std::string& error)
{
    Stop();
    if (port == 0 || !_host.BridgeWifiUp())
    {
        error = port == 0 ? "webdav port zero" : "webdav no wifi";
        return false;
    }
    _port = port;
    EspStack& stack = _host.BridgeStack();
    if (stack.OpenAt(kListenSlot, true) < 0)
    {
        error = "webdav listen failed";
        return false;
    }
    stack.Bind(kListenSlot, _port);
    stack.Listen(kListenSlot);
    _running = true;
    error.clear();
    return true;
}

void ZiFiWebDavServer::Stop()
{
    _job = Job();
    CloseClient();
    _host.BridgeStack().Close(kListenSlot);
    _host.BridgeStack().Close(kRefuseSlot);
    _running = false;
}

void ZiFiWebDavServer::Forget()
{
    _running = false;
    _port = kDefaultPort;
    _clientActive = false;
    _acceptedAt = 0;
    _header.clear();
    _job = Job();
    _result = ZiFiVfsBridge::Result();
    _uploadSlots.clear();
    _uploadActive = _uploadEof = _uploadError = false;
    _uploadReadBytes = _uploadWantedBytes = 0;
    _uploadLastProgress = 0;
}

void ZiFiWebDavServer::CloseClient()
{
    _host.BridgeStack().Close(kClientSlot);
    _clientActive = false;
    _header.clear();
}

bool ZiFiWebDavServer::SendBytes(const uint8_t* data, size_t length)
{
    EspStack& stack = _host.BridgeStack();
    if (!_clientActive || !stack.Valid(kClientSlot) || stack.GetSlot(kClientSlot).state != EspStack::State::Tcp ||
        stack.GetSlot(kClientSlot).vnetId == 0)
        return false;
    if (length)
        stack.Send(kClientSlot, data, static_cast<uint32_t>(length));
    _bytesSent += length;
    return true;
}

bool ZiFiWebDavServer::SendText(const std::string& text)
{
    return SendBytes(reinterpret_cast<const uint8_t*>(text.data()), text.size());
}

bool ZiFiWebDavServer::SendStatus(uint16_t code, const char* reason, const std::string& extraHeaders,
                                  uint32_t contentLength)
{
    char header[384];
    const bool addBreak = !extraHeaders.empty() && extraHeaders.back() != '\n';
    const int length = std::snprintf(header, sizeof(header), "HTTP/1.1 %u %s\r\nConnection: close\r\nContent-Length: %lu\r\n%s%s\r\n",
                                     code, reason, static_cast<unsigned long>(contentLength), extraHeaders.c_str(),
                                     addBreak ? "\r\n" : "");
    _lastStatus = code;
    return length > 0 && static_cast<size_t>(length) < sizeof(header) &&
           SendBytes(reinterpret_cast<const uint8_t*>(header), static_cast<size_t>(length));
}

void ZiFiWebDavServer::AcceptClient()
{
    EspStack& stack = _host.BridgeStack();
    if (!stack.Valid(kListenSlot) || stack.PendingClients(kListenSlot) == 0)
        return;
    if (_clientActive && (ClientConnectedNow() || ClientAvailable() > 0))
    {
        stack.Close(kRefuseSlot);
        if (stack.AcceptInto(kListenSlot, kRefuseSlot) < 0)
            return;
        static const char kBusy[] = "HTTP/1.1 503 Service Unavailable\r\nConnection: close\r\nContent-Length: 0\r\n\r\n";
        stack.Send(kRefuseSlot, reinterpret_cast<const uint8_t*>(kBusy), sizeof(kBusy) - 1);
        stack.Close(kRefuseSlot);
        return;
    }
    CloseClient();
    if (stack.AcceptInto(kListenSlot, kClientSlot) < 0)
        return;
    _clientActive = true;
    _acceptedAt = Now();
    _header.clear();
}

void ZiFiWebDavServer::ReceiveHeader()
{
    if (!_clientActive)
        return;
    EspStack& stack = _host.BridgeStack();
    while (ClientAvailable() > 0)
    {
        const std::vector<uint8_t> one = stack.Read(kClientSlot, 1);
        _bytesReceived += 1;
        if (_header.size() >= kHeaderSize)
        {
            SendStatus(431, "Request Header Fields Too Large");
            CloseClient();
            return;
        }
        _header.push_back(static_cast<char>(one[0]));
        // Reading stops exactly at the header's end: a PUT body stays in TCP
        const size_t n = _header.size();
        if (n >= 4 && _header.compare(n - 4, 4, "\r\n\r\n") == 0)
            return ProcessRequest(_header.substr(0, n - 4));
        if (n >= 2 && _header[n - 2] == '\n' && _header[n - 1] == '\n')
            return ProcessRequest(_header.substr(0, n - 2));
    }
    if (Now() - _acceptedAt >= Us(kHeaderTimeoutUs))
    {
        SendStatus(408, "Request Timeout");
        CloseClient();
    }
    else if (ClientAvailable() == 0 && !ClientConnectedNow())
        CloseClient();
}

void ZiFiWebDavServer::Poll()
{
    if (!_running)
        return;
    if (JobActive())
    {
        RunJob();
        return;
    }
    AcceptClient();
    ReceiveHeader();
}

bool ZiFiWebDavServer::FindHeader(const std::string& headers, const char* wanted, std::string& value)
{
    size_t line = 0;
    while (line < headers.size())
    {
        size_t end = headers.find("\r\n", line);
        size_t delimiter = 2;
        if (end == std::string::npos)
        {
            end = headers.find('\n', line);
            delimiter = 1;
        }
        const size_t lineEnd = end == std::string::npos ? headers.size() : end;
        const size_t colon = headers.find(':', line);
        if (colon != std::string::npos && colon < lineEnd && EqualsNoCase(headers.substr(line, colon - line), wanted))
        {
            size_t v = colon + 1;
            while (v < lineEnd && (headers[v] == ' ' || headers[v] == '\t'))
                ++v;
            value = headers.substr(v, lineEnd - v);
            return true;
        }
        if (end == std::string::npos)
            break;
        line = end + delimiter;
    }
    return false;
}

bool ZiFiWebDavServer::ParseContentLength(const std::string& headers, uint32_t& contentLength, bool& present)
{
    contentLength = 0;
    present = false;
    size_t line = 0;
    while (line < headers.size())
    {
        size_t end = headers.find("\r\n", line);
        size_t delimiter = 2;
        if (end == std::string::npos)
        {
            end = headers.find('\n', line);
            delimiter = 1;
        }
        const size_t lineEnd = end == std::string::npos ? headers.size() : end;
        if (lineEnd == line)
            break;
        const size_t colon = headers.find(':', line);
        if (colon != std::string::npos && colon < lineEnd && EqualsNoCase(headers.substr(line, colon - line), "Content-Length"))
        {
            // Several Content-Length headers make the body's end ambiguous
            if (present)
                return false;
            size_t v = colon + 1;
            size_t ve = lineEnd;
            while (v < ve && (headers[v] == ' ' || headers[v] == '\t'))
                ++v;
            while (ve > v && (headers[ve - 1] == ' ' || headers[ve - 1] == '\t'))
                --ve;
            if (v == ve)
                return false;
            uint32_t parsed = 0;
            for (size_t p = v; p < ve; ++p)
            {
                if (headers[p] < '0' || headers[p] > '9')
                    return false;
                const uint32_t digit = static_cast<uint32_t>(headers[p] - '0');
                if (parsed > (0xFFFFFFFFu - digit) / 10u)
                    return false;
                parsed = parsed * 10u + digit;
            }
            contentLength = parsed;
            present = true;
        }
        if (end == std::string::npos)
            break;
        line = end + delimiter;
    }
    return true;
}

bool ZiFiWebDavServer::DecodePath(const std::string& target, std::string& output)
{
    if (target == "*")
    {
        output = "/";
        return true;
    }
    // An absolute-form URI: the scheme and host are dropped
    size_t source = 0;
    const size_t scheme = target.find("://");
    std::string text = target;
    if (scheme != std::string::npos)
    {
        const size_t slash = target.find('/', scheme + 3);
        text = slash == std::string::npos ? std::string("/") : target.substr(slash);
    }
    std::string decoded;
    while (source < text.size() && text[source] != '?' && text[source] != '#')
    {
        uint8_t byte = static_cast<uint8_t>(text[source++]);
        if (byte == '%')
        {
            const int high = source < text.size() ? HexValue(text[source]) : -1;
            const int low = source + 1 < text.size() ? HexValue(text[source + 1]) : -1;
            if (high < 0 || low < 0)
                return false;
            byte = static_cast<uint8_t>((high << 4) | low);
            source += 2;
        }
        if (byte == 0 || byte == '\r' || byte == '\n')
            return false;
        if (decoded.size() >= kMaxPath)
            return false;
        decoded.push_back(byte == '\\' ? '/' : static_cast<char>(byte));
    }
    if (decoded.empty() || decoded[0] != '/')
        return false;
    // No /../.. above the VFS root
    output = "/";
    size_t cursor = 0;
    while (cursor < decoded.size())
    {
        while (cursor < decoded.size() && decoded[cursor] == '/')
            ++cursor;
        if (cursor >= decoded.size())
            break;
        size_t end = decoded.find('/', cursor);
        if (end == std::string::npos)
            end = decoded.size();
        const std::string part = decoded.substr(cursor, end - cursor);
        if (part == ".")
        {
            cursor = end;
            continue;
        }
        if (part == "..")
        {
            size_t length = output.size();
            while (length > 1 && output[length - 1] != '/')
                --length;
            if (length > 1)
                --length;
            output.resize(length);
        }
        else
        {
            const size_t slash = output.size() > 1 ? 1 : 0;
            if (output.size() + slash + part.size() > kMaxPath)
                return false;
            if (slash)
                output.push_back('/');
            output += part;
        }
        cursor = end;
    }
    return true;
}

void ZiFiWebDavServer::ProcessRequest(const std::string& header)
{
    ++_requests;
    size_t firstEnd = header.find("\r\n");
    size_t delimiter = 2;
    if (firstEnd == std::string::npos)
    {
        firstEnd = header.find('\n');
        delimiter = 1;
    }
    const auto finish = [this]() { CloseClient(); };
    // The final CRLF CRLF is cut off: a request line without any header line has no break left (400, as the
    // firmware: strstr finds none)
    _lastRequest = header.substr(0, std::min<size_t>(firstEnd, 120));
    if (firstEnd == std::string::npos)
    {
        SendStatus(400, "Bad Request");
        return finish();
    }
    const std::string first = header.substr(0, firstEnd);
    const size_t space = first.find(' ');
    if (space == std::string::npos)
    {
        SendStatus(400, "Bad Request");
        return finish();
    }
    std::string method = first.substr(0, space);
    size_t t = space + 1;
    while (t < first.size() && first[t] == ' ')
        ++t;
    const size_t versionSpace = first.find(' ', t);
    if (versionSpace == std::string::npos)
    {
        SendStatus(400, "Bad Request");
        return finish();
    }
    const std::string target = first.substr(t, versionSpace - t);
    const std::string version = first.substr(versionSpace + 1);
    if (version.compare(0, 7, "HTTP/1.") != 0)
    {
        SendStatus(505, "HTTP Version Not Supported");
        return finish();
    }
    for (char& c : method)
        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    std::string path;
    if (!DecodePath(target, path))
    {
        SendStatus(400, "Bad Path");
        return finish();
    }
    const std::string headers = firstEnd + delimiter <= header.size() ? header.substr(firstEnd + delimiter) : std::string();
    uint32_t contentLength = 0;
    bool hasContentLength = false;
    if (!ParseContentLength(headers, contentLength, hasContentLength))
    {
        SendStatus(400, "Bad Content-Length");
        return finish();
    }
    const bool isPut = method == "PUT";
    Job::Kind kind = Job::None;
    if (method == "OPTIONS")
        kind = Job::None;
    else if (method == "PROPFIND")
        kind = Job::Propfind;
    else if (method == "GET")
        kind = Job::Get;
    else if (method == "HEAD")
        kind = Job::Head;
    else if (isPut)
        kind = Job::Put;
    else if (method == "DELETE")
        kind = Job::Delete;
    else if (method == "MKCOL")
        kind = Job::Mkcol;

    _job = Job();
    _job.path = path;
    std::string depth;
    _job.depth = FindHeader(headers, "Depth", depth) ? depth : std::string("1");
    _job.contentLength = contentLength;
    // WinSCP sends an XML body with PROPFIND: it is read to the end before the answer (a close with unread bytes
    // would be a TCP RST)
    if (!isPut && hasContentLength && contentLength != 0)
    {
        _job.kind = Job::Discard;
        _job.remaining = contentLength;
        _job.idleSince = Now();
        _job.after = kind;
        _job.afterStatus = method == "OPTIONS" ? 200 : kind == Job::None ? 405 : 0;
        return RunJob();
    }
    if (method == "OPTIONS")
    {
        SendStatus(200, "OK", std::string(kAllow) + "DAV: 1\r\n");
        return finish();
    }
    if (kind == Job::None)
    {
        SendStatus(405, "Method Not Allowed", kAllow);
        return finish();
    }
    if (isPut && !hasContentLength)
    {
        SendStatus(411, "Length Required");
        return finish();
    }
    StartJob(kind, path);
}

void ZiFiWebDavServer::StartJob(Job::Kind kind, const std::string& requestPath)
{
    const std::string path = requestPath;   // it may be the job's own field, cleared below
    const std::string depth = _job.depth;
    const uint32_t contentLength = _job.contentLength;
    _job = Job();
    _job.kind = kind;
    _job.path = path;
    _job.depth = depth;
    _job.contentLength = contentLength;
    RunJob();
}

void ZiFiWebDavServer::RunJob()
{
    bool done = false;
    switch (_job.kind)
    {
        case Job::None: return;
        case Job::Discard: done = StepDiscard(_job); break;
        case Job::Propfind: done = StepPropfind(_job); break;
        case Job::Get: done = StepGet(_job, false); break;
        case Job::Head: done = StepGet(_job, true); break;
        case Job::Put: done = StepPut(_job); break;
        case Job::Delete:
        case Job::Mkcol: done = StepSimple(_job); break;
    }
    if (done && _job.kind != Job::None)
    {
        _job = Job();
        CloseClient();
    }
}

bool ZiFiWebDavServer::Request(ZiFiVfsBridge::Op op, const std::string& path, uint32_t value)
{
    return _host.BridgeVfs().Submit(op, path, value);
}

int ZiFiWebDavServer::Await(ZiFiVfsBridge::Result& result)
{
    if (!_host.BridgeVfs().TakeResult(result))
        return 0;
    return result.success ? 1 : -1;
}

bool ZiFiWebDavServer::StepDiscard(Job& j)
{
    // discardRequestBody: up to 512 at a time, 10 s without bytes is the end
    EspStack& stack = _host.BridgeStack();
    while (j.remaining != 0)
    {
        const size_t available = ClientAvailable();
        if (available > 0)
        {
            const size_t wanted = std::min<size_t>(std::min(available, kIoSize), j.remaining);
            const std::vector<uint8_t> bytes = stack.Read(kClientSlot, static_cast<uint32_t>(wanted));
            _bytesReceived += bytes.size();
            j.remaining -= static_cast<uint32_t>(bytes.size());
            j.idleSince = Now();
            continue;
        }
        if (!ClientConnectedNow() || Now() - j.idleSince >= Us(kRequestBodyIdleTimeoutUs))
        {
            SendStatus(408, "Request Timeout");
            return true;
        }
        return false;
    }
    if (j.afterStatus == 200)
    {
        SendStatus(200, "OK", std::string(kAllow) + "DAV: 1\r\n");
        return true;
    }
    if (j.afterStatus == 405)
    {
        SendStatus(405, "Method Not Allowed", kAllow);
        return true;
    }
    StartJob(j.after, j.path);
    return false;
}

bool ZiFiWebDavServer::SendEncodedHref(const std::string& path)
{
    static const char kHex[] = "0123456789ABCDEF";
    std::string text = "<d:href>";
    for (unsigned char byte : path)
    {
        const bool plain = (byte < 0x80 && std::isalnum(byte)) || byte == '/' || byte == '-' || byte == '_' ||
                           byte == '.' || byte == '~';
        if (plain)
            text.push_back(static_cast<char>(byte));
        else
        {
            text.push_back('%');
            text.push_back(kHex[byte >> 4]);
            text.push_back(kHex[byte & 0x0F]);
        }
    }
    text += "</d:href>\n";
    return SendText(text);
}

bool ZiFiWebDavServer::SendProp(const std::string& path, const ZiFiVfsBridge::Result& entry)
{
    if (!SendText("<d:response>\n") || !SendEncodedHref(path) || !SendText("<d:propstat><d:prop>\n"))
        return false;
    if (entry.isDirectory)
    {
        if (!SendText("<d:resourcetype><d:collection/></d:resourcetype>\n"))
            return false;
    }
    else
    {
        char line[80];
        const int length = std::snprintf(line, sizeof(line), "<d:resourcetype/><d:getcontentlength>%lu</d:getcontentlength>\n",
                                         static_cast<unsigned long>(entry.size));
        if (length <= 0 || static_cast<size_t>(length) >= sizeof(line) || !SendText(std::string(line, static_cast<size_t>(length))))
            return false;
    }
    return SendText("</d:prop><d:status>HTTP/1.1 200 OK</d:status></d:propstat></d:response>\n");
}

bool ZiFiWebDavServer::StepPropfind(Job& j)
{
    for (;;)
    {
        switch (j.step)
        {
            case kBegin:
                if (!Request(ZiFiVfsBridge::Op::Stat, j.path))
                {
                    SendStatus(404, "Not Found");
                    return true;
                }
                j.step = kStat;
                break;
            case kStat:
            {
                const int a = Await(j.root);
                if (a == 0)
                    return false;
                if (a < 0)
                {
                    SendStatus(404, "Not Found");
                    return true;
                }
                _lastStatus = 207;
                if (!SendText("HTTP/1.1 207 Multi-Status\r\nContent-Type: application/xml; charset=utf-8\r\n"
                              "Connection: close\r\n\r\n<?xml version=\"1.0\" encoding=\"utf-8\"?>\n"
                              "<d:multistatus xmlns:d=\"DAV:\">\n") ||
                    !SendProp(j.path, j.root))
                    return true;
                // Depth: infinity is one level on this module (the answer streams; recursion would need a path stack)
                if (j.depth != "0" && j.root.isDirectory && Request(ZiFiVfsBridge::Op::OpenDirectory, j.path))
                {
                    j.step = kOpenDir;
                    break;
                }
                SendText("</d:multistatus>\n");
                return true;
            }
            case kOpenDir:
            {
                const int a = Await(_result);
                if (a == 0)
                    return false;
                if (a < 0 || !Request(ZiFiVfsBridge::Op::ReadDirectory))
                {
                    SendText("</d:multistatus>\n");
                    return true;
                }
                j.step = kReadDir;
                break;
            }
            case kReadDir:
            {
                const int a = Await(_result);
                if (a == 0)
                    return false;
                if (a < 0 || _result.atEnd)
                {
                    SendText("</d:multistatus>\n");
                    return true;
                }
                const size_t slash = j.path.size() > 1 ? 1 : 0;
                if (j.path.size() + slash + _result.name.size() <= kMaxPath)
                {
                    const std::string child = j.path + (slash ? "/" : "") + _result.name;
                    if (!SendProp(child, _result))
                        return true;
                }
                if (!Request(ZiFiVfsBridge::Op::ReadDirectory))
                {
                    SendText("</d:multistatus>\n");
                    return true;
                }
                break;
            }
            default: return true;
        }
    }
}

bool ZiFiWebDavServer::StepGet(Job& j, bool headersOnly)
{
    ZiFiVfsBridge& vfs = _host.BridgeVfs();
    for (;;)
    {
        switch (j.step)
        {
            case kBegin:
                if (!Request(ZiFiVfsBridge::Op::Stat, j.path))
                {
                    SendStatus(404, "Not Found");
                    return true;
                }
                j.step = kStat;
                break;
            case kStat:
            {
                const int a = Await(j.root);
                if (a == 0)
                    return false;
                if (a < 0 || j.root.isDirectory)
                {
                    SendStatus(404, "Not Found");
                    return true;
                }
                if (headersOnly)
                {
                    SendStatus(200, "OK", "Content-Type: application/octet-stream\r\n", j.root.size);
                    return true;
                }
                if (!Request(ZiFiVfsBridge::Op::OpenRead, j.path))
                {
                    SendStatus(404, "Not Found");
                    return true;
                }
                j.step = kOpen;
                break;
            }
            case kOpen:
            {
                const int a = Await(_result);
                if (a == 0)
                    return false;
                if (a < 0)
                {
                    SendStatus(404, "Not Found");
                    return true;
                }
                if (!SendStatus(200, "OK", "Content-Type: application/octet-stream\r\n", j.root.size))
                {
                    j.step = kClose;
                    Request(ZiFiVfsBridge::Op::CloseCommit);
                    break;
                }
                j.step = kRead;
                if (j.sent >= j.root.size || !Request(ZiFiVfsBridge::Op::Read, {}, std::min<uint32_t>(j.root.size - j.sent, kIoSize)))
                {
                    j.step = kClose;
                    Request(ZiFiVfsBridge::Op::CloseCommit);
                }
                break;
            }
            case kRead:
            {
                const int a = Await(_result);
                if (a == 0)
                    return false;
                uint8_t buffer[kIoSize];
                const size_t received = a > 0 ? vfs.ReadForNetwork(buffer, sizeof(buffer)) : 0;
                // A short body: Content-Length lets the client see it
                if (a < 0 || received == 0 || !SendBytes(buffer, received))
                {
                    j.step = kClose;
                    Request(ZiFiVfsBridge::Op::CloseCommit);
                    break;
                }
                j.sent += static_cast<uint32_t>(received);
                if (j.sent >= j.root.size || !Request(ZiFiVfsBridge::Op::Read, {}, std::min<uint32_t>(j.root.size - j.sent, kIoSize)))
                {
                    j.step = kClose;
                    Request(ZiFiVfsBridge::Op::CloseCommit);
                }
                break;
            }
            case kClose:
                if (Await(_result) == 0)
                    return false;
                return true;
            default: return true;
        }
    }
}

void ZiFiWebDavServer::ResetUploadRing()
{
    _uploadSlots.clear();
    _uploadActive = false;
    _uploadEof = _uploadReadBytes >= _uploadWantedBytes;
    _uploadError = false;
    _uploadLastProgress = Now();
}

bool ZiFiWebDavServer::PrefetchUpload()
{
    if (_uploadEof || _uploadError)
        return false;
    if (_uploadReadBytes >= _uploadWantedBytes)
    {
        _uploadEof = true;
        return false;
    }
    if (_uploadSlots.size() >= kUploadSlotCount || (_uploadActive && _uploadSlots.size() >= kUploadSlotCount - 1))
        return false;
    const size_t available = ClientAvailable();
    if (available == 0)
    {
        if (!ClientConnectedNow())
            _uploadEof = true;   // the early EOF shows in the count
        return false;
    }
    size_t wanted = std::min(available, kUploadSlotSize);
    wanted = std::min<size_t>(wanted, _uploadWantedBytes - _uploadReadBytes);
    std::vector<uint8_t> bytes = _host.BridgeStack().Read(kClientSlot, static_cast<uint32_t>(wanted));
    _uploadReadBytes += static_cast<uint32_t>(bytes.size());
    _bytesReceived += bytes.size();
    _uploadLastProgress = Now();
    _uploadSlots.push_back(std::move(bytes));
    if (_uploadReadBytes >= _uploadWantedBytes)
        _uploadEof = true;
    return true;
}

bool ZiFiWebDavServer::StepPut(Job& j)
{
    ZiFiVfsBridge& vfs = _host.BridgeVfs();
    for (;;)
    {
        switch (j.step)
        {
            case kBegin:
                if (!Request(ZiFiVfsBridge::Op::OpenWrite, j.path))
                {
                    SendStatus(409, "Conflict");
                    return true;
                }
                j.step = kOpen;
                break;
            case kOpen:
            {
                const int a = Await(_result);
                if (a == 0)
                    return false;
                if (a < 0)
                {
                    SendStatus(409, "Conflict");
                    return true;
                }
                _uploadWantedBytes = j.contentLength;
                _uploadReadBytes = 0;   // the header read stops at its end: no body bytes in it
                ResetUploadRing();
                j.stored = 0;
                while (_uploadSlots.size() < kUploadSlotCount - 1 && !_uploadEof && !_uploadError && PrefetchUpload())
                {
                }
                j.step = kWrite;
                break;
            }
            case kWrite:
                if (j.failed || j.stored >= j.contentLength)
                {
                    j.step = kClose;
                    Request(j.failed ? ZiFiVfsBridge::Op::CloseAbort : ZiFiVfsBridge::Op::CloseCommit);
                    break;
                }
                if (!_uploadSlots.empty())
                {
                    std::vector<uint8_t> bytes = std::move(_uploadSlots.front());
                    _uploadSlots.pop_front();
                    _uploadActive = true;
                    vfs.WriteFromNetwork(bytes.data(), bytes.size());
                    j.sent = static_cast<uint32_t>(bytes.size());
                    Request(ZiFiVfsBridge::Op::Write, {}, j.sent);
                    j.step = kAnswer;
                    break;
                }
                if (_uploadError || (_uploadEof && _uploadReadBytes < j.contentLength))
                {
                    j.failed = true;
                    break;
                }
                if (!PrefetchUpload())
                {
                    if (Now() - _uploadLastProgress >= Us(kPutIdleTimeoutUs))
                    {
                        j.failed = true;
                        break;
                    }
                    return false;
                }
                break;
            case kAnswer:
            {
                PrefetchUpload();   // the idle hook during the BLOCK exchanges
                const int a = Await(_result);
                if (a == 0)
                    return false;
                if (a < 0)
                    j.failed = true;
                j.stored += j.sent;
                _uploadActive = false;
                _uploadLastProgress = Now();
                j.step = kWrite;
                break;
            }
            case kClose:
            {
                const int a = Await(_result);
                if (a == 0)
                    return false;
                if (a < 0)
                    j.failed = true;
                if (j.failed || j.stored != j.contentLength)
                {
                    Request(ZiFiVfsBridge::Op::Delete, j.path);
                    j.step = kRemove;
                    break;
                }
                SendStatus(201, "Created");
                return true;
            }
            case kRemove:
                if (Await(_result) == 0)
                    return false;
                SendStatus(507, "Insufficient Storage");
                return true;
            default: return true;
        }
    }
}

bool ZiFiWebDavServer::StepSimple(Job& j)
{
    const bool remove = j.kind == Job::Delete;
    if (j.step == kBegin)
    {
        if (!Request(remove ? ZiFiVfsBridge::Op::Delete : ZiFiVfsBridge::Op::Mkdir, j.path))
        {
            remove ? SendStatus(404, "Not Found") : SendStatus(409, "Conflict");
            return true;
        }
        j.step = kAnswer;
    }
    const int a = Await(_result);
    if (a == 0)
        return false;
    if (remove)
        a > 0 ? SendStatus(204, "No Content") : SendStatus(404, "Not Found");
    else
        a > 0 ? SendStatus(201, "Created") : SendStatus(409, "Conflict");
    return true;
}

std::string ZiFiWebDavServer::JobText() const
{
    static const char* const kKinds[] = {"", "body", "PROPFIND", "GET", "HEAD", "PUT", "DELETE", "MKCOL"};
    if (_job.kind == Job::None)
        return {};
    std::string text = std::string(kKinds[_job.kind % 8]) + " " + _job.path;
    if (_job.kind == Job::Get)
        text += " " + std::to_string(_job.sent) + "/" + std::to_string(_job.root.size);
    if (_job.kind == Job::Put)
        text += " " + std::to_string(_job.stored) + "/" + std::to_string(_job.contentLength);
    return text;
}

// --- TTD -----------------------------------------------------------------------------------------------------------

namespace
{
constexpr uint8_t kStateVersion = 1;
}

void ZiFiWebDavServer::Save(ZiFiStateWriter& w) const
{
    w.U8(kStateVersion);
    w.Bool(_running);
    w.U16(_port);
    w.Bool(_clientActive);
    w.U64(_acceptedAt);
    w.Str(_header);
    const Job& j = _job;
    w.U8(j.kind), w.U8(j.after), w.U8(j.step);
    w.U16(j.afterStatus);
    w.Str(j.path), w.Str(j.depth);
    w.U32(j.contentLength), w.U32(j.remaining);
    w.U64(j.idleSince);
    w.Bool(j.root.success), w.Bool(j.root.isDirectory);
    w.U32(j.root.size);
    w.U32(j.sent), w.U32(j.stored);
    w.Bool(j.failed);
    w.Bool(_result.success), w.Bool(_result.atEnd), w.Bool(_result.isDirectory);
    w.U32(_result.size);
    w.Str(_result.name);
    w.U32(static_cast<uint32_t>(_uploadSlots.size()));
    for (const std::vector<uint8_t>& slot : _uploadSlots)
        w.Bytes(slot);
    w.Bool(_uploadActive), w.Bool(_uploadEof), w.Bool(_uploadError);
    w.U32(_uploadReadBytes), w.U32(_uploadWantedBytes);
    w.U64(_uploadLastProgress);
    w.Str(_lastRequest);
    w.U16(_lastStatus);
    w.U64(_bytesSent), w.U64(_bytesReceived);
    w.U32(_requests);
}

bool ZiFiWebDavServer::Load(ZiFiStateReader& r)
{
    if (r.U8() != kStateVersion)
        return false;
    _running = r.Bool();
    _port = r.U16();
    _clientActive = r.Bool();
    _acceptedAt = r.U64();
    _header = r.Str(kHeaderSize + 1);
    Job& j = _job;
    const uint8_t kind = r.U8(), after = r.U8();
    j.kind = kind <= Job::Mkcol ? static_cast<Job::Kind>(kind) : Job::None;
    j.after = after <= Job::Mkcol ? static_cast<Job::Kind>(after) : Job::None;
    j.step = r.U8();
    j.afterStatus = r.U16();
    j.path = r.Str(512), j.depth = r.Str(256);
    j.contentLength = r.U32(), j.remaining = r.U32();
    j.idleSince = r.U64();
    j.root.success = r.Bool(), j.root.isDirectory = r.Bool();
    j.root.size = r.U32();
    j.sent = r.U32(), j.stored = r.U32();
    j.failed = r.Bool();
    _result.success = r.Bool(), _result.atEnd = r.Bool(), _result.isDirectory = r.Bool();
    _result.size = r.U32();
    _result.name = r.Str(256);
    _uploadSlots.clear();
    const uint32_t slots = r.U32();
    for (uint32_t i = 0; i < slots && i < kUploadSlotCount && r.Ok(); ++i)
        _uploadSlots.push_back(r.Bytes(kUploadSlotSize));
    _uploadActive = r.Bool(), _uploadEof = r.Bool(), _uploadError = r.Bool();
    _uploadReadBytes = r.U32(), _uploadWantedBytes = r.U32();
    _uploadLastProgress = r.U64();
    _lastRequest = r.Str(256);
    _lastStatus = r.U16();
    _bytesSent = r.U64(), _bytesReceived = r.U64();
    _requests = r.U32();
    return r.Ok();
}
