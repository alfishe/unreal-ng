#include "sdb/http.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstring>

#include "sdb/eventloop.h"

namespace sdb
{

std::string HttpResponse::Header(const std::string& name) const
{
    for (const auto& [k, v] : headers)
    {
        if (k.size() == name.size() && std::equal(k.begin(), k.end(), name.begin(),
                                                  [](char a, char b) { return std::tolower(a) == std::tolower(b); }))
            return v;
    }
    return {};
}

HttpResponse HttpRequest(const std::string& url, const std::string& method, const std::string& path,
                         const std::string& body, const std::vector<std::pair<std::string, std::string>>& headers,
                         int timeoutMs)
{
    HttpResponse r;
    const auto t0 = std::chrono::steady_clock::now();
    std::string hostport = url.rfind("http://", 0) == 0 ? url.substr(7) : url;
    if (auto slash = hostport.find('/'); slash != std::string::npos)
        hostport.resize(slash);
    std::string host = hostport, port = "80";
    if (auto colon = hostport.rfind(':'); colon != std::string::npos)
    {
        host = hostport.substr(0, colon);
        port = hostport.substr(colon + 1);
    }

    int portNumber = port.empty() || port.size() > 5 ? -1 : 0;
    for (char c : port)
        if (portNumber >= 0)
            portNumber = c >= '0' && c <= '9' ? portNumber * 10 + (c - '0') : -1;
    if (portNumber <= 0 || portNumber > 65535)
    {
        r.error = "bad port " + port;
        return r;
    }
    const auto deadline = t0 + std::chrono::milliseconds(timeoutMs);
    auto left = [&] {
        return static_cast<int>(std::max<long long>(
            0, std::chrono::duration_cast<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now())
                   .count()));
    };
    const net::SocketHandle fd = net::Connect(host, portNumber, timeoutMs, nullptr);
    if (fd == net::kInvalidSocket)
    {
        r.error = "connect " + hostport + " failed";
        return r;
    }

    std::string req = method + " " + path + " HTTP/1.1\r\nHost: " + hostport +
                      "\r\nConnection: close\r\nAccept: application/json\r\n";
    for (const auto& [k, v] : headers)
        req += k + ": " + v + "\r\n";
    if (!body.empty() || method == "POST" || method == "PUT")
        req += "Content-Type: application/json\r\nContent-Length: " + std::to_string(body.size()) + "\r\n";
    req += "\r\n" + body;
    if (!net::SendAll(fd, req.data(), req.size(), left()))
    {
        net::CloseSocket(fd);
        r.error = "send failed";
        return r;
    }
    // Connection: close: the answer ends where the server closes the stream.
    std::string raw;
    char buf[16384];
    while (net::WaitSocket(fd, true, false, left()) > 0)
    {
        const long n = net::Recv(fd, buf, sizeof buf);
        if (n < 0)
            break;
        raw.append(buf, static_cast<size_t>(n));
    }
    net::CloseSocket(fd);

    const auto hdrEnd = raw.find("\r\n\r\n");
    if (hdrEnd == std::string::npos)
    {
        r.error = raw.empty() ? "no response" : "bad response";
        return r;
    }
    const std::string head = raw.substr(0, hdrEnd);
    std::string rest = raw.substr(hdrEnd + 4);
    if (auto sp = head.find(' '); sp != std::string::npos)
        r.status = std::atoi(head.c_str() + sp + 1);
    size_t pos = head.find("\r\n");
    while (pos != std::string::npos && pos + 2 < head.size())
    {
        const size_t next = head.find("\r\n", pos + 2);
        const std::string line = head.substr(pos + 2, next == std::string::npos ? std::string::npos : next - pos - 2);
        if (auto c = line.find(':'); c != std::string::npos)
        {
            std::string v = line.substr(c + 1);
            v.erase(0, v.find_first_not_of(' '));
            r.headers.emplace_back(line.substr(0, c), v);
        }
        pos = next;
    }
    if (r.Header("Transfer-Encoding").find("chunked") != std::string::npos)
    {
        std::string out;
        size_t i = 0;
        while (i < rest.size())
        {
            const size_t eol = rest.find("\r\n", i);
            if (eol == std::string::npos)
                break;
            const size_t len = std::strtoul(rest.substr(i, eol - i).c_str(), nullptr, 16);
            if (len == 0)
                break;
            out.append(rest, eol + 2, len);
            i = eol + 2 + len + 2;
        }
        rest = std::move(out);
    }
    r.body = std::move(rest);
    r.ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    return r;
}

}  // namespace sdb
