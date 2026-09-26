// http-client.h - blocking HTTP/1.1 client for the WebAPI REST transport.
//
// One TCP connection per request ("Connection: close"): the debugger talks
// to localhost, where a fresh connect costs microseconds, and never having
// keep-alive state removes an entire failure class. Replies framed by
// Content-Length / chunked terminator are returned as soon as they are
// complete, so a server that ignores Connection: close still gets served.
// Timeouts apply to connect, send and receive; a request that would block
// longer aborts with ok=false so the UI can degrade instead of freezing.
// Only what the REST backend needs is implemented: no redirects, no TLS,
// no auth.
#pragma once

#include <string>

namespace dbg {

struct HttpReply {
    bool ok = false;       // transport succeeded (socket + HTTP parse)
    int status = 0;        // HTTP status code (200, 400, 404 ...)
    std::string body;      // decoded entity (Content-Length or chunked)
    std::string error;     // transport/parse error text; empty when ok
    bool IsSuccess() const { return ok && status >= 200 && status < 300; }
};

class HttpClient {
public:
    // "baseUrl" is "http://host[:port]" (port defaults to 80). Anything
    // after a trailing '/' is ignored.
    explicit HttpClient(const std::string& baseUrl);

    void SetTimeoutSeconds(int seconds) { timeoutSeconds_ = seconds; }

    // Sends "method SP pathAndQuery" with an optional JSON body.
    HttpReply Request(const std::string& method, const std::string& pathAndQuery,
                      const std::string& body = "");

private:
    std::string host_;
    std::string hostHeader_;  // "host:port" for the Host header
    int port_ = 80;
    int timeoutSeconds_ = 5;
};

}  // namespace dbg
