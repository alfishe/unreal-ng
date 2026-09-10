#pragma once

// Minimal HTTP/1.1 client for the MCP bridge — std + raw sockets, zero deps
//
// Exactly one use case: POST one JSON body to the emulator's /mcp endpoint,
// then read one response. Completion comes from the wire framing (terminal
// chunk for SSE streams, Content-Length otherwise), not from a server close —
// the request advertises keep-alive so SSE answers can stream. The socket is
// still discarded after one request/response, keeping the daemon stateless
// and trivially restartable.

#include <string>

namespace bridge
{

/// region <HttpResult>

struct HttpResult
{
    bool ok = false;    // transport + HTTP response received
    int status = 0;     // HTTP status code (0 on transport failure)
    std::string body;   // response body (may be empty, e.g. 202 notifications)
    std::string contentType; // lowercased response Content-Type ("" when absent)
    std::string error;  // human-readable transport error when !ok
};

/// endregion </HttpResult>

/// Parses "http://host[:port][/path]" (default port 80, default path "/")
bool ParseUrl(const std::string& url, std::string& host, int& port, std::string& path);

/// Blocking POST with Content-Type: application/json. Handles both
/// Content-Length and chunked response bodies (SSE answers are chunked).
HttpResult Post(const std::string& host, int port, const std::string& path, const std::string& body);

} // namespace bridge
