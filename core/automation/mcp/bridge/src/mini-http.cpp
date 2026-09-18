// Minimal HTTP/1.1 client implementation — see mini-http.h

#include "mini-http.h"

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>

// Cross-platform socket layer (winsock / POSIX)
#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
using SocketHandle = SOCKET;
constexpr SocketHandle kInvalidSocket = INVALID_SOCKET;
#define BRIDGE_CLOSE closesocket
#define BRIDGE_GET_LAST_ERROR WSAGetLastError()
#else
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
using SocketHandle = int;
constexpr SocketHandle kInvalidSocket = -1;
#define BRIDGE_CLOSE close
#define BRIDGE_GET_LAST_ERROR errno
#endif

namespace
{

/// region <Platform bootstrap>

#ifdef _WIN32
struct WinsockGuard
{
    WinsockGuard()
    {
        WSADATA data;
        _ok = WSAStartup(MAKEWORD(2, 2), &data) == 0;
    }

    ~WinsockGuard()
    {
        if (_ok)
        {
            WSACleanup();
        }
    }

    bool _ok = false;
};
#endif

bool EnsureSocketLibrary()
{
#ifdef _WIN32
    static WinsockGuard guard; // Initialized once per process
    return guard._ok;
#else
    return true;
#endif
}

/// endregion </Platform bootstrap>

/// region <Helpers>

bool SendAll(SocketHandle sock, const char* data, size_t length)
{
    size_t sent = 0;
    while (sent < length)
    {
        // Chunk to int range; MCP requests are single lines, never near 2 GiB
        size_t remaining = length - sent;
        int chunk = static_cast<int>(send(sock, data + sent, remaining > 1u << 30 ? 1u << 30 : remaining, 0));
        if (chunk <= 0)
        {
            return false;
        }
        sent += static_cast<size_t>(chunk);
    }
    return true;
}

std::string ToLower(const std::string& text)
{
    std::string lower;
    lower.reserve(text.size());
    for (char c : text)
    {
        lower += static_cast<char>(c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c);
    }
    return lower;
}

/// Reassembles a chunked transfer-encoded body (hex sizes, CRLF framed)
std::string Dechunk(const std::string& chunked)
{
    std::string out;
    size_t pos = 0;
    while (pos < chunked.size())
    {
        size_t lineEnd = chunked.find("\r\n", pos);
        if (lineEnd == std::string::npos)
        {
            break;
        }

        std::string sizeText = chunked.substr(pos, lineEnd - pos);
        // Chunk extensions after ';' are legal but unused by drogon
        size_t semicolon = sizeText.find(';');
        if (semicolon != std::string::npos)
        {
            sizeText = sizeText.substr(0, semicolon);
        }

        char* end = nullptr;
        unsigned long long size = std::strtoull(sizeText.c_str(), &end, 16);
        if (end == sizeText.c_str())
        {
            break; // Malformed chunk header
        }
        if (size == 0)
        {
            break; // Terminal chunk
        }

        pos = lineEnd + 2;
        if (pos + size > chunked.size())
        {
            out.append(chunked, pos, std::string::npos); // Truncated tail
            break;
        }
        out.append(chunked, pos, size);
        pos += size + 2; // Skip chunk data + trailing CRLF
    }
    return out;
}

/// endregion </Helpers>

} // namespace

namespace bridge
{

/// region <Public API>

bool ParseUrl(const std::string& url, std::string& host, int& port, std::string& path)
{
    const std::string scheme = "http://";
    if (url.compare(0, scheme.size(), scheme) != 0)
    {
        return false;
    }

    std::string rest = url.substr(scheme.size());
    size_t slash = rest.find('/');
    std::string authority = (slash == std::string::npos) ? rest : rest.substr(0, slash);
    path = (slash == std::string::npos) ? "/" : rest.substr(slash);
    if (authority.empty())
    {
        return false;
    }

    port = 80;
    size_t colon = authority.rfind(':');
    if (colon != std::string::npos)
    {
        std::string portText = authority.substr(colon + 1);
        authority = authority.substr(0, colon);
        if (portText.empty())
        {
            return false;
        }
        int value = 0;
        for (char c : portText)
        {
            if (c < '0' || c > '9')
            {
                return false;
            }
            value = value * 10 + (c - '0');
            if (value > 65535)
            {
                return false;
            }
        }
        port = value;
    }

    if (authority.empty())
    {
        return false;
    }
    host = authority;
    return true;
}

HttpResult Post(const std::string& host, int port, const std::string& path, const std::string& body)
{
    HttpResult result;

    if (!EnsureSocketLibrary())
    {
        result.error = "Winsock initialization failed";
        return result;
    }

    struct addrinfo hints;
    std::memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;

    struct addrinfo* info = nullptr;
    if (getaddrinfo(host.c_str(), std::to_string(port).c_str(), &hints, &info) != 0 || info == nullptr)
    {
        result.error = "cannot resolve host '" + host + "'";
        return result;
    }

    // New connection per request — see mini-http.h
    SocketHandle sock = kInvalidSocket;
    for (struct addrinfo* it = info; it != nullptr; it = it->ai_next)
    {
        sock = socket(it->ai_family, it->ai_socktype, it->ai_protocol);
        if (sock == kInvalidSocket)
        {
            continue;
        }
        if (connect(sock, it->ai_addr, static_cast<int>(it->ai_addrlen)) == 0)
        {
            break;
        }
        BRIDGE_CLOSE(sock);
        sock = kInvalidSocket;
    }
    freeaddrinfo(info);

    if (sock == kInvalidSocket)
    {
        result.error = "connection refused";
        return result;
    }

    // Bound a wedged server so the daemon never hangs forever. Generous:
    // MCP tool calls (run_frames, recordings, searches) can take minutes.
#ifdef _WIN32
    DWORD receiveTimeoutMs = 300000;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (const char*)&receiveTimeoutMs, sizeof(receiveTimeoutMs));
#else
    struct timeval timeout;
    timeout.tv_sec = 300;
    timeout.tv_usec = 0;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
#endif

    std::string request;
    request.reserve(body.size() + 256);
    request += "POST " + path + " HTTP/1.1\r\n";
    request += "Host: " + host + "\r\n";
    request += "Content-Type: application/json\r\n";
    // Streamable HTTP Accept per the MCP spec: the server answers SSE when the
    // request carries a _meta.progressToken, plain JSON otherwise
    request += "Accept: application/json, text/event-stream\r\n";
    request += "Content-Length: " + std::to_string(body.size()) + "\r\n";
    request += "Connection: keep-alive\r\n";
    request += "\r\n";
    request += body;

    if (!SendAll(sock, request.data(), request.size()))
    {
        result.error = "send failed";
        BRIDGE_CLOSE(sock);
        return result;
    }

    // Keep-alive framing: the server does not close after the response, so
    // completion is detected from the wire framing instead of EOF — chunked
    // responses (SSE streams) end with the terminal chunk, sized responses
    // when Content-Length body bytes have arrived. (Asking for
    // `Connection: close` makes drogon shut SSE streams down before the first
    // frame is written — keep-alive plus framing detection keeps both modes
    // working.) The socket is still closed after one request/response.
    std::string raw;
    char buffer[8192];
    bool complete = false;
    while (!complete)
    {
        int received = static_cast<int>(recv(sock, buffer, sizeof(buffer), 0));
        if (received < 0)
        {
#ifdef _WIN32
            result.error = (BRIDGE_GET_LAST_ERROR == WSAETIMEDOUT) ? "receive timeout" : "receive failed";
#else
            result.error = (BRIDGE_GET_LAST_ERROR == EAGAIN || BRIDGE_GET_LAST_ERROR == EWOULDBLOCK) ? "receive timeout"
                                                                                                 : "receive failed";
#endif
            BRIDGE_CLOSE(sock);
            return result;
        }
        if (received == 0)
        {
            break; // Orderly EOF — complete enough for unframed responses
        }
        raw.append(buffer, static_cast<size_t>(received));

        const size_t headerEnd = raw.find("\r\n\r\n");
        if (headerEnd == std::string::npos)
        {
            continue; // headers not fully received yet
        }

        const std::string framing = ToLower(raw.substr(0, headerEnd));
        if (framing.find("transfer-encoding: chunked") != std::string::npos)
        {
            // Terminal chunk "0\r\n\r\n" (drogon sends no trailers); a recv()
            // boundary may split it — the compare simply fails and the loop
            // waits for the remainder
            const std::string chunked = raw.substr(headerEnd + 4);
            if (chunked.size() >= 5 && chunked.compare(chunked.size() - 5, 5, "0\r\n\r\n") == 0)
            {
                complete = true;
            }
        }
        else
        {
            const size_t lengthPos = framing.find("content-length:");
            if (lengthPos != std::string::npos)
            {
                const long long length = std::strtoll(framing.c_str() + lengthPos + 15, nullptr, 10);
                if (length >= 0 && raw.size() >= headerEnd + 4 + static_cast<size_t>(length))
                {
                    complete = true;
                }
            }
            // No framing header at all: only EOF ends the read (legacy path)
        }
    }
    BRIDGE_CLOSE(sock);

    // Status line: "HTTP/1.1 200 OK"
    size_t lineEnd = raw.find("\r\n");
    if (raw.compare(0, 5, "HTTP/") != 0 || lineEnd == std::string::npos)
    {
        result.error = "malformed response";
        return result;
    }
    {
        size_t space = raw.find(' ');
        if (space == std::string::npos || space + 1 >= lineEnd)
        {
            result.error = "malformed status line";
            return result;
        }
        result.status = std::atoi(raw.c_str() + space + 1);
    }

    size_t headerEnd = raw.find("\r\n\r\n", lineEnd);
    if (headerEnd == std::string::npos)
    {
        result.error = "malformed response";
        return result;
    }

    // Header names are case-insensitive; normalize once
    std::string headers = ToLower(raw.substr(lineEnd + 2, headerEnd - (lineEnd + 2)));
    std::string responseBody = raw.substr(headerEnd + 4);

    // Content-Type out of the lowercased header block (trimmed) — the caller
    // needs it to recognize SSE answers
    {
        const size_t typePos = headers.find("content-type:");
        if (typePos != std::string::npos)
        {
            const size_t valueStart = headers.find_first_not_of(" \t", typePos + 13);
            if (valueStart != std::string::npos)
            {
                size_t valueEnd = headers.find("\r\n", valueStart);
                if (valueEnd == std::string::npos)
                {
                    valueEnd = headers.size();
                }
                result.contentType = headers.substr(valueStart, valueEnd - valueStart);
            }
        }
    }

    if (headers.find("transfer-encoding: chunked") != std::string::npos)
    {
        result.body = Dechunk(responseBody);
    }
    else
    {
        size_t lengthPos = headers.find("content-length:");
        if (lengthPos != std::string::npos)
        {
            long long length = std::strtoll(headers.c_str() + lengthPos + 15, nullptr, 10);
            if (length >= 0 && static_cast<unsigned long long>(length) < responseBody.size())
            {
                responseBody.resize(static_cast<size_t>(length));
            }
        }
        result.body = std::move(responseBody);
    }

    result.ok = true;
    return result;
}

/// endregion </Public API>

} // namespace bridge
