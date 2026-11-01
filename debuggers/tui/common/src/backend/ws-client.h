// ws-client.h - the WebSocket transport of the WebAPI backend: one connection to the emulator's /api/v1/ws, each
// request a `call` ({"op":"call","rid":N,"method","path","body"}) answered by its `result` ({"op":"result","rid":N,
// "status","body"}), the same route handler as REST (websocket-interface.md section 2).
//
// One socket for the whole session instead of a connection per request; a broken socket is opened again at the next
// request. Pings are answered; messages that are not this call's result (topics the connection never subscribed,
// a late result of an abandoned call) are skipped. Text frames only, client-masked (RFC 6455); no TLS.
#pragma once

#include <cstdint>
#include <random>
#include <string>
#include <vector>

#include "backend/api-transport.h"
#include "backend/net-socket.h"

namespace dbg {

class WebSocketClient : public IApiTransport {
public:
    // "url" is "ws://host[:port][/path]" (also "http://"; port 80, path /api/v1/ws by default)
    explicit WebSocketClient(const std::string& url);
    ~WebSocketClient() override;
    WebSocketClient(const WebSocketClient&) = delete;
    WebSocketClient& operator=(const WebSocketClient&) = delete;

    std::string Name() const override { return "ws"; }
    void SetTimeoutSeconds(int seconds) override { timeoutSeconds_ = seconds; }
    HttpReply Request(const std::string& method, const std::string& pathAndQuery,
                      const std::string& body = "") override;
    bool Subscribe(const std::string& topicsJson) override;
    void TakeEvents(std::vector<std::string>* out, bool* reconnected) override;

    bool IsOpen() const { return fd_ != net::kInvalid; }

private:
    bool Open(const net::Clock::time_point& deadline, std::string* error);
    void Drop();
    bool SendFrame(uint8_t opcode, const std::string& payload, const net::Clock::time_point& deadline,
                   std::string* error);
    // The next text message (pings answered, fragments joined); false on a close, an error or the deadline
    bool ReadMessage(std::string* text, const net::Clock::time_point& deadline, std::string* error);
    bool Fill(size_t need, const net::Clock::time_point& deadline, std::string* error);

    std::string host_;
    std::string path_ = "/api/v1/ws";
    int port_ = 80;
    int timeoutSeconds_ = 5;
    net::Handle fd_ = net::kInvalid;
    std::string in_;  // bytes received and not yet taken as a frame
    std::string topics_;               // Subscribe: sent again on every connection
    std::vector<std::string> events_;  // topic messages not yet taken
    bool reconnected_ = false;         // a connection opened again since TakeEvents
    bool everOpen_ = false;
    int64_t rid_ = 0;
    std::mt19937 random_;
};

// The raw JSON text of the top-level member @p key of the object @p json ("" when absent): the reply body as REST
// would send it
std::string RawJsonMember(const std::string& json, const std::string& key);

}  // namespace dbg
