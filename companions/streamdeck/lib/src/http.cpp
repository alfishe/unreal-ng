#include "sdb/http.h"

#include <netdb.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstring>

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

    addrinfo hints{}, *res = nullptr;
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    if (getaddrinfo(host.c_str(), port.c_str(), &hints, &res) != 0 || !res)
    {
        r.error = "resolve " + host;
        return r;
    }
    int fd = -1;
    for (auto* p = res; p; p = p->ai_next)
    {
        fd = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
        if (fd < 0)
            continue;
        timeval tv{timeoutMs / 1000, (timeoutMs % 1000) * 1000};
        setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
        setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof tv);
        if (connect(fd, p->ai_addr, p->ai_addrlen) == 0)
            break;
        close(fd);
        fd = -1;
    }
    freeaddrinfo(res);
    if (fd < 0)
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
    for (size_t sent = 0; sent < req.size();)
    {
        const ssize_t n = send(fd, req.data() + sent, req.size() - sent, 0);
        if (n <= 0)
        {
            close(fd);
            r.error = "send failed";
            return r;
        }
        sent += static_cast<size_t>(n);
    }
    std::string raw;
    char buf[16384];
    for (;;)
    {
        const ssize_t n = recv(fd, buf, sizeof buf, 0);
        if (n <= 0)
            break;
        raw.append(buf, static_cast<size_t>(n));
    }
    close(fd);

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
