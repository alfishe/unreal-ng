#include "common/network/loopbackhttp.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <vector>

#include "common/network/netsockets.h"

namespace netsock
{

namespace
{
using Clock = std::chrono::steady_clock;

int Left(Clock::time_point deadline)
{
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - Clock::now()).count();
    return ms > 0 ? static_cast<int>(ms) : 0;
}

bool Wait(Handle h, bool write, Clock::time_point deadline)
{
    std::vector<PollItem> items(1);
    items[0].handle = h;
    items[0].wantRead = !write;
    items[0].wantWrite = write;
    const int left = Left(deadline);
    if (left <= 0 || Poll(items, left) <= 0)
        return false;
    return write ? (items[0].writable || items[0].failed) : (items[0].readable || items[0].failed);
}

/// The chunked body joined (RFC 9112 §7.1); the text as it is when it is not chunked well
std::string Unchunk(const std::string& text)
{
    std::string out;
    size_t at = 0;
    while (at < text.size())
    {
        const size_t eol = text.find("\r\n", at);
        if (eol == std::string::npos)
            return text;
        const unsigned long size = std::strtoul(text.substr(at, eol - at).c_str(), nullptr, 16);
        if (size == 0)
            return out;
        if (eol + 2 + size > text.size())
            return text;
        out.append(text, eol + 2, size);
        at = eol + 2 + size + 2;
    }
    return out;
}

std::string Lower(std::string s)
{
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}
} // namespace

HttpReply LoopbackHttp(int port, const std::string& method, const std::string& path, const std::string& body,
                       int timeoutMs)
{
    HttpReply reply;
    if (port <= 0 || port > 65535)
    {
        reply.error = "bad port " + std::to_string(port);
        return reply;
    }
    const Clock::time_point deadline = Clock::now() + std::chrono::milliseconds(std::max(1, timeoutMs));
    const Handle h = OpenTcp();
    if (h == kInvalid)
    {
        reply.error = "no socket";
        return reply;
    }
    struct Closer
    {
        Handle h;
        ~Closer() { Close(h); }
    } closer{h};

    NetEndpoint to;
    to.addr = NetIp(127, 0, 0, 1);
    to.port = static_cast<uint16_t>(port);
    Result r = Connect(h, to);
    if (r == Result::WouldBlock)
    {
        if (!Wait(h, true, deadline))
        {
            reply.error = "connect timed out";
            return reply;
        }
        r = ConnectResult(h);
    }
    if (r != Result::Ok)
    {
        reply.error = r == Result::Refused ? "connection refused" : "connect failed";
        return reply;
    }

    std::string request = method + " " + path + " HTTP/1.1\r\nHost: 127.0.0.1:" + std::to_string(port) +
                          "\r\nConnection: close\r\nAccept: application/json\r\n";
    if (!body.empty() || method == "POST" || method == "PUT")
        request += "Content-Type: application/json\r\nContent-Length: " + std::to_string(body.size()) + "\r\n";
    request += "\r\n" + body;
    size_t sent = 0;
    while (sent < request.size())
    {
        size_t done = 0;
        r = Send(h, reinterpret_cast<const uint8_t*>(request.data()) + sent, request.size() - sent, done);
        if (r == Result::Ok)
            sent += done;
        else if (r != Result::WouldBlock || !Wait(h, true, deadline))
        {
            reply.error = "send failed";
            return reply;
        }
    }

    std::string text;
    uint8_t buffer[4096];
    for (;;)
    {
        size_t done = 0;
        r = Recv(h, buffer, sizeof(buffer), done);
        if (r == Result::Ok && done > 0)
        {
            text.append(reinterpret_cast<const char*>(buffer), done);
            continue;
        }
        if (r == Result::Closed || (r == Result::Ok && done == 0))
            break;
        if (r != Result::WouldBlock)
        {
            if (!text.empty())
                break;  // a reset after the answer
            reply.error = "receive failed";
            return reply;
        }
        if (!Wait(h, false, deadline))
        {
            reply.error = "no answer within " + std::to_string(timeoutMs) + " ms";
            return reply;
        }
    }

    const size_t headEnd = text.find("\r\n\r\n");
    if (text.compare(0, 5, "HTTP/") != 0 || headEnd == std::string::npos)
    {
        reply.error = "not an HTTP answer";
        return reply;
    }
    const size_t space = text.find(' ');
    reply.status = std::atoi(text.c_str() + space + 1);
    const std::string head = Lower(text.substr(0, headEnd));
    reply.body = text.substr(headEnd + 4);
    if (head.find("transfer-encoding: chunked") != std::string::npos)
        reply.body = Unchunk(reply.body);
    return reply;
}

} // namespace netsock
