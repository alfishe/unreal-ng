// http-client.cpp - see http-client.h.
#include "backend/http-client.h"

#include <algorithm>
#include <chrono>
#include <cstdlib>

#include "backend/net-socket.h"

namespace dbg {
namespace {

using Clock = net::Clock;

std::string ToLowerAscii(const std::string& s) {
    std::string out = s;
    std::transform(out.begin(), out.end(), out.begin(),
                   [](unsigned char c) { return static_cast<char>(c >= 'A' && c <= 'Z' ? c + 32 : c); });
    return out;
}

}  // namespace

HttpClient::HttpClient(const std::string& baseUrl) {
    std::string rest = baseUrl;
    const size_t scheme = rest.find("://");
    if (scheme != std::string::npos) rest = rest.substr(scheme + 3);
    const size_t slash = rest.find('/');
    if (slash != std::string::npos) rest = rest.substr(0, slash);
    const size_t colon = rest.rfind(':');  // IPv6 literal hosts are out of scope
    if (colon != std::string::npos) {
        host_ = rest.substr(0, colon);
        port_ = std::atoi(rest.substr(colon + 1).c_str());
        if (port_ <= 0) port_ = 80;
    } else {
        host_ = rest;
        port_ = 80;
    }
    hostHeader_ = host_ + ":" + std::to_string(port_);
}

// -- reply parsing (file-local) -------------------------------------------

namespace {

bool ParseHexSize(const std::string& line, size_t* out) {
    size_t value = 0;
    size_t i = 0;
    while (i < line.size()) {
        const char c = line[i];
        int digit = -1;
        if (c >= '0' && c <= '9') digit = c - '0';
        else if (c >= 'a' && c <= 'f') digit = c - 'a' + 10;
        else if (c >= 'A' && c <= 'F') digit = c - 'A' + 10;
        if (digit < 0) break;  // chunk extensions (";name=val") stop the size
        value = value * 16 + static_cast<size_t>(digit);
        ++i;
    }
    if (i == 0) return false;
    *out = value;
    return true;
}

bool DecodeChunked(const std::string& body, std::string* out) {
    size_t pos = 0;
    out->clear();
    while (true) {
        const size_t lineEnd = body.find("\r\n", pos);
        if (lineEnd == std::string::npos) return false;
        size_t size = 0;
        if (!ParseHexSize(body.substr(pos, lineEnd - pos), &size)) return false;
        pos = lineEnd + 2;
        if (size == 0) return true;  // final chunk (trailers ignored)
        if (pos + size + 2 > body.size()) return false;
        out->append(body, pos, size);
        pos += size + 2;  // data + CRLF
    }
}

// True once raw holds a fully framed reply: headers complete and the body
// delimited by the chunked terminator or Content-Length. Lets the receive
// loop return without waiting for EOF - keep-alive servers never close.
bool ReplyComplete(const std::string& raw) {
    size_t headerEnd = raw.find("\r\n\r\n");
    size_t sep = 4;
    if (headerEnd == std::string::npos) {
        headerEnd = raw.find("\n\n");
        sep = 2;
        if (headerEnd == std::string::npos) return false;
    }
    const std::string headers = ToLowerAscii(raw.substr(0, headerEnd));
    const size_t bodyStart = headerEnd + sep;
    if (headers.find("transfer-encoding: chunked") != std::string::npos) {
        std::string sink;
        return DecodeChunked(raw.substr(bodyStart), &sink);
    }
    const size_t at = headers.find("content-length:");
    if (at == std::string::npos) return false;  // only EOF can delimit
    const size_t lineEnd = headers.find('\n', at + 15);
    const size_t length = static_cast<size_t>(
        std::atoi(headers.substr(at + 15, lineEnd - at - 15).c_str()));
    return raw.size() >= bodyStart + length;
}

bool ParseReply(const std::string& raw, HttpReply* reply) {
    // status line: "HTTP/1.x NNN reason"
    const size_t lineEnd = raw.find("\r\n");
    if (lineEnd == std::string::npos || raw.compare(0, 5, "HTTP/") != 0) {
        reply->error = "malformed status line";
        return false;
    }
    const size_t codeStart = raw.find(' ');
    if (codeStart == std::string::npos || codeStart > lineEnd) {
        reply->error = "malformed status line";
        return false;
    }
    const size_t codeEnd = raw.find(' ', codeStart + 1);
    if (codeEnd == std::string::npos || codeEnd > lineEnd) {
        reply->error = "malformed status line";
        return false;
    }
    reply->status = std::atoi(raw.substr(codeStart + 1, codeEnd - codeStart - 1).c_str());
    if (reply->status <= 0) {
        reply->error = "bad status code";
        return false;
    }
    // headers end at CRLFCRLF (or a bare LFLF from sloppy servers)
    size_t headerEnd = raw.find("\r\n\r\n");
    size_t sep = 4;
    if (headerEnd == std::string::npos) {
        headerEnd = raw.find("\n\n");
        sep = 2;
    }
    if (headerEnd == std::string::npos) {
        reply->error = "truncated headers";
        return false;
    }
    const std::string headers = ToLowerAscii(raw.substr(lineEnd + 2, headerEnd - lineEnd - 2));
    const size_t bodyStart = headerEnd + sep;
    const size_t contentLengthAt = headers.find("content-length:");
    const bool chunked = headers.find("transfer-encoding: chunked") != std::string::npos;
    if (chunked) {
        if (!DecodeChunked(raw.substr(bodyStart), &reply->body)) {
            reply->error = "truncated chunked body";
            return false;
        }
    } else if (contentLengthAt != std::string::npos) {
        const size_t valueStart = contentLengthAt + 15;
        const size_t lineEnd2 = headers.find('\n', valueStart);
        const size_t length = static_cast<size_t>(
            std::atoi(headers.substr(valueStart, lineEnd2 - valueStart).c_str()));
        if (bodyStart + length > raw.size()) {
            reply->error = "truncated body";
            return false;
        }
        reply->body.assign(raw, bodyStart, length);
    } else {
        reply->body = raw.substr(bodyStart);  // Connection: close -> EOF body
    }
    reply->ok = true;
    reply->error.clear();
    return true;
}

}  // namespace

HttpReply HttpClient::Request(const std::string& method, const std::string& pathAndQuery,
                              const std::string& body) {
    HttpReply reply;
    const int timeoutMs = timeoutSeconds_ * 1000;
    const Clock::time_point deadline = Clock::now() + std::chrono::milliseconds(timeoutMs);
    const net::Handle fd = net::Connect(host_, port_, timeoutMs, &reply.error);
    if (fd == net::kInvalid) return reply;
    net::Scope guard(fd);

    std::string request;
    request.reserve(body.size() + 256);
    request += method + " " + pathAndQuery + " HTTP/1.1\r\n";
    request += "Host: " + hostHeader_ + "\r\n";
    request += "Accept: application/json\r\n";
    request += "Content-Type: application/json\r\n";
    request += "Content-Length: " + std::to_string(body.size()) + "\r\n";
    // A debugger's: the user's own hands, no run-control session (the WebAPI gate, webapi-interface.md)
    request += "X-Unreal-Source: debugger\r\n";
    request += "Connection: close\r\n\r\n";
    request += body;
    if (!net::SendAll(guard.Get(), request.data(), request.size(), deadline, &reply.error)) return reply;

    std::string raw;
    char buf[8192];
    while (true) {
        const int n = net::Receive(guard.Get(), buf, static_cast<int>(sizeof buf), deadline);
        if (n == 0) break;  // EOF: Connection close delimits the body
        if (n == net::kTimedOut) {
            reply.error = raw.empty() ? "receive timeout" : "connection closed mid-reply";
            return reply;
        }
        if (n < 0) {
            reply.error = "receive failed";
            return reply;
        }
        raw.append(buf, static_cast<size_t>(n));
        if (ReplyComplete(raw)) break;  // framed: no need to wait for EOF
    }
    ParseReply(raw, &reply);
    return reply;
}

}  // namespace dbg
