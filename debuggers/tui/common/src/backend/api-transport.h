// api-transport.h - the seam between the WebAPI backend and the wire: a request (method, route, JSON body) in, the
// status and the JSON body out. HttpClient sends each as an HTTP request; WebSocketClient as a `call` on the
// emulator's /api/v1/ws socket (the same route handler answers both).
#pragma once

#include <string>
#include <vector>

namespace dbg {

struct HttpReply {
    bool ok = false;       // transport succeeded (socket + HTTP parse)
    int status = 0;        // HTTP status code (200, 400, 404 ...)
    std::string body;      // decoded entity (Content-Length or chunked)
    std::string error;     // transport/parse error text; empty when ok
    bool IsSuccess() const { return ok && status >= 200 && status < 300; }
};

class IApiTransport {
public:
    virtual ~IApiTransport() = default;
    // "rest" or "ws": the backend's name in the UI
    virtual std::string Name() const = 0;
    virtual void SetTimeoutSeconds(int seconds) = 0;
    // A WebAPI route ("/api/v1/...", a query allowed) with an optional JSON body
    virtual HttpReply Request(const std::string& method, const std::string& pathAndQuery,
                              const std::string& body = "") = 0;
    // Topics (websocket-interface.md §3): a transport that is pushed them (the WebSocket) subscribes to the names in
    // @p topicsJson (a JSON array), again after each reconnect, and returns true; one that is not returns false and
    // the backend polls instead
    virtual bool Subscribe(const std::string& topicsJson) {
        (void)topicsJson;
        return false;
    }
    // The topic messages received so far (whole JSON texts, oldest first), without waiting. A reconnect since the
    // last call is told by @p reconnected: the messages between are lost
    virtual void TakeEvents(std::vector<std::string>* out, bool* reconnected) {
        (void)out;
        *reconnected = false;
    }
};

}  // namespace dbg
