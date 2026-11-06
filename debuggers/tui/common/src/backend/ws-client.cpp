// ws-client.cpp - see ws-client.h.
#include "backend/ws-client.h"

#include <algorithm>
#include <cstdlib>

#include "backend/json-mini.h"

namespace dbg {
namespace {

constexpr uint8_t kText = 0x1;
constexpr uint8_t kClose = 0x8;
constexpr uint8_t kPing = 0x9;
constexpr uint8_t kPong = 0xA;

std::string Base64(const uint8_t* data, size_t size) {
    static const char kAlphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    for (size_t i = 0; i < size; i += 3) {
        const uint32_t b0 = data[i];
        const uint32_t b1 = i + 1 < size ? data[i + 1] : 0;
        const uint32_t b2 = i + 2 < size ? data[i + 2] : 0;
        const uint32_t v = (b0 << 16) | (b1 << 8) | b2;
        out += kAlphabet[(v >> 18) & 63];
        out += kAlphabet[(v >> 12) & 63];
        out += i + 1 < size ? kAlphabet[(v >> 6) & 63] : '=';
        out += i + 2 < size ? kAlphabet[v & 63] : '=';
    }
    return out;
}

// The end of the JSON value starting at @p pos (past its last character), npos when it does not end
size_t SkipValue(const std::string& s, size_t pos) {
    if (pos >= s.size()) return std::string::npos;
    if (s[pos] == '"') {
        for (size_t i = pos + 1; i < s.size(); ++i) {
            if (s[i] == '\\') ++i;
            else if (s[i] == '"') return i + 1;
        }
        return std::string::npos;
    }
    if (s[pos] == '{' || s[pos] == '[') {
        int depth = 0;
        for (size_t i = pos; i < s.size(); ++i) {
            const char c = s[i];
            if (c == '"') {
                i = SkipValue(s, i);
                if (i == std::string::npos) return i;
                --i;
            } else if (c == '{' || c == '[') {
                ++depth;
            } else if (c == '}' || c == ']') {
                if (--depth == 0) return i + 1;
            }
        }
        return std::string::npos;
    }
    size_t i = pos;  // a number or a literal
    while (i < s.size() && s[i] != ',' && s[i] != '}' && s[i] != ']' && s[i] != ' ' && s[i] != '\n' && s[i] != '\r' &&
           s[i] != '\t')
        ++i;
    return i;
}

size_t SkipSpace(const std::string& s, size_t pos) {
    while (pos < s.size() && (s[pos] == ' ' || s[pos] == '\n' || s[pos] == '\r' || s[pos] == '\t')) ++pos;
    return pos;
}

}  // namespace

std::string RawJsonMember(const std::string& json, const std::string& key) {
    size_t pos = SkipSpace(json, 0);
    if (pos >= json.size() || json[pos] != '{') return std::string();
    pos = SkipSpace(json, pos + 1);
    while (pos < json.size() && json[pos] == '"') {
        const size_t keyEnd = SkipValue(json, pos);
        if (keyEnd == std::string::npos) return std::string();
        const std::string name = json.substr(pos + 1, keyEnd - pos - 2);
        pos = SkipSpace(json, keyEnd);
        if (pos >= json.size() || json[pos] != ':') return std::string();
        pos = SkipSpace(json, pos + 1);
        const size_t end = SkipValue(json, pos);
        if (end == std::string::npos) return std::string();
        if (name == key) return json.substr(pos, end - pos);
        pos = SkipSpace(json, end);
        if (pos < json.size() && json[pos] == ',') pos = SkipSpace(json, pos + 1);
    }
    return std::string();
}

WebSocketClient::WebSocketClient(const std::string& url) : random_(std::random_device{}()) {
    std::string rest = url;
    const size_t scheme = rest.find("://");
    if (scheme != std::string::npos) rest = rest.substr(scheme + 3);
    const size_t slash = rest.find('/');
    if (slash != std::string::npos) {
        if (slash + 1 < rest.size()) path_ = rest.substr(slash);
        rest = rest.substr(0, slash);
    }
    const size_t colon = rest.rfind(':');  // IPv6 literal hosts are out of scope
    if (colon != std::string::npos) {
        host_ = rest.substr(0, colon);
        port_ = std::atoi(rest.substr(colon + 1).c_str());
        if (port_ <= 0) port_ = 80;
    } else {
        host_ = rest;
    }
}

WebSocketClient::~WebSocketClient() {
    if (IsOpen()) {
        std::string ignored;
        SendFrame(kClose, std::string(), net::Clock::now() + std::chrono::milliseconds(200), &ignored);
    }
    Drop();
}

void WebSocketClient::Drop() {
    net::Close(fd_);
    fd_ = net::kInvalid;
    in_.clear();
}

bool WebSocketClient::Open(const net::Clock::time_point& deadline, std::string* error) {
    const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - net::Clock::now()).count();
    fd_ = net::Connect(host_, port_, static_cast<int>(left > 0 ? left : 1), error);
    if (fd_ == net::kInvalid) return false;
    uint8_t nonce[16];
    for (uint8_t& b : nonce) b = static_cast<uint8_t>(random_());
    std::string request = "GET " + path_ + " HTTP/1.1\r\n";
    request += "Host: " + host_ + ":" + std::to_string(port_) + "\r\n";
    request += "Upgrade: websocket\r\nConnection: Upgrade\r\n";
    request += "Sec-WebSocket-Key: " + Base64(nonce, sizeof nonce) + "\r\n";
    request += "Sec-WebSocket-Version: 13\r\n";
    // Every call on the socket is a debugger's: the user's own hands, no run-control session (webapi-interface.md)
    request += "X-Unreal-Source: debugger\r\n\r\n";
    if (!net::SendAll(fd_, request.data(), request.size(), deadline, error)) {
        Drop();
        return false;
    }
    // The answer's headers; what follows them is the first frame's bytes
    size_t end = std::string::npos;
    char buf[4096];
    while ((end = in_.find("\r\n\r\n")) == std::string::npos) {
        const int n = net::Receive(fd_, buf, static_cast<int>(sizeof buf), deadline);
        if (n <= 0) {
            *error = n == net::kTimedOut ? "websocket handshake timeout" : "websocket handshake failed";
            Drop();
            return false;
        }
        in_.append(buf, static_cast<size_t>(n));
    }
    const std::string status = in_.substr(0, in_.find("\r\n"));
    if (status.find(" 101") == std::string::npos) {
        *error = "websocket refused: " + status;
        Drop();
        return false;
    }
    in_.erase(0, end + 4);
    if (everOpen_) reconnected_ = true;
    everOpen_ = true;
    // The subscription lives with the connection: asked again on each, its answer read with the other messages
    if (!topics_.empty() &&
        !SendFrame(kText, "{\"op\":\"subscribe\",\"topics\":" + topics_ + "}", deadline, error)) {
        Drop();
        return false;
    }
    return true;
}

bool WebSocketClient::SendFrame(uint8_t opcode, const std::string& payload, const net::Clock::time_point& deadline,
                                std::string* error) {
    std::string frame;
    frame += static_cast<char>(0x80 | opcode);
    const size_t n = payload.size();
    if (n < 126) {
        frame += static_cast<char>(0x80 | n);
    } else if (n <= 0xFFFF) {
        frame += static_cast<char>(0x80 | 126);
        frame += static_cast<char>((n >> 8) & 0xFF);
        frame += static_cast<char>(n & 0xFF);
    } else {
        frame += static_cast<char>(0x80 | 127);
        for (int shift = 56; shift >= 0; shift -= 8) frame += static_cast<char>((static_cast<uint64_t>(n) >> shift) & 0xFF);
    }
    uint8_t mask[4];
    for (uint8_t& b : mask) b = static_cast<uint8_t>(random_());
    frame.append(reinterpret_cast<const char*>(mask), 4);
    for (size_t i = 0; i < n; ++i) frame += static_cast<char>(static_cast<uint8_t>(payload[i]) ^ mask[i & 3]);
    return net::SendAll(fd_, frame.data(), frame.size(), deadline, error);
}

bool WebSocketClient::Fill(size_t need, const net::Clock::time_point& deadline, std::string* error) {
    char buf[16384];
    while (in_.size() < need) {
        const int n = net::Receive(fd_, buf, static_cast<int>(sizeof buf), deadline);
        if (n <= 0) {
            *error = n == net::kTimedOut ? "receive timeout" : n == 0 ? "websocket closed" : "receive failed";
            return false;
        }
        in_.append(buf, static_cast<size_t>(n));
    }
    return true;
}

bool WebSocketClient::ReadMessage(std::string* text, const net::Clock::time_point& deadline, std::string* error) {
    text->clear();
    while (true) {
        if (!Fill(2, deadline, error)) return false;
        const uint8_t b0 = static_cast<uint8_t>(in_[0]);
        const uint8_t b1 = static_cast<uint8_t>(in_[1]);
        const bool fin = (b0 & 0x80) != 0;
        const uint8_t opcode = b0 & 0x0F;
        const bool masked = (b1 & 0x80) != 0;
        size_t header = 2;
        uint64_t length = b1 & 0x7F;
        if (length == 126) {
            if (!Fill(4, deadline, error)) return false;
            length = (static_cast<uint64_t>(static_cast<uint8_t>(in_[2])) << 8) | static_cast<uint8_t>(in_[3]);
            header = 4;
        } else if (length == 127) {
            if (!Fill(10, deadline, error)) return false;
            length = 0;
            for (int i = 2; i < 10; ++i) length = (length << 8) | static_cast<uint8_t>(in_[static_cast<size_t>(i)]);
            header = 10;
        }
        const size_t maskAt = header;
        if (masked) header += 4;
        if (!Fill(header + static_cast<size_t>(length), deadline, error)) return false;
        std::string payload = in_.substr(header, static_cast<size_t>(length));
        if (masked)
            for (size_t i = 0; i < payload.size(); ++i)
                payload[i] = static_cast<char>(static_cast<uint8_t>(payload[i]) ^ static_cast<uint8_t>(in_[maskAt + (i & 3)]));
        in_.erase(0, header + static_cast<size_t>(length));

        if (opcode == kPing) {
            // A poll's deadline is a millisecond away: the answer gets a second of its own
            const net::Clock::time_point pongBy = std::max(deadline, net::Clock::now() + std::chrono::seconds(1));
            if (!SendFrame(kPong, payload, pongBy, error)) return false;
            continue;
        }
        if (opcode == kPong) continue;
        if (opcode == kClose) {
            *error = "websocket closed by the emulator";
            return false;
        }
        text->append(payload);  // a text frame or its continuation
        if (fin) return true;
    }
}

HttpReply WebSocketClient::Request(const std::string& method, const std::string& pathAndQuery,
                                   const std::string& body) {
    HttpReply reply;
    const net::Clock::time_point deadline = net::Clock::now() + std::chrono::seconds(timeoutSeconds_);
    // A socket that broke is opened again: the next call goes on a fresh connection
    for (int attempt = 0; attempt < 2; ++attempt) {
        if (!IsOpen() && !Open(deadline, &reply.error)) return reply;
        const int64_t rid = ++rid_;
        std::string call = "{\"op\":\"call\",\"rid\":" + std::to_string(rid) + ",\"method\":\"" + JsonEscape(method) +
                           "\",\"path\":\"" + JsonEscape(pathAndQuery) + "\"";
        if (!body.empty()) call += ",\"body\":" + body;
        call += "}";
        if (!SendFrame(kText, call, deadline, &reply.error)) {
            Drop();
            continue;
        }
        std::string text;
        while (ReadMessage(&text, deadline, &reply.error)) {
            const Json message = Json::Parse(text, nullptr);
            std::string op;
            int64_t answered = -1;
            message.Get("op", &op);
            message.Get("rid", &answered);
            if (answered != rid) {  // a topic (kept for TakeEvents), or a result the caller stopped waiting for
                std::string topic;
                if (message.Get("t", &topic) && topic != "ws") events_.push_back(text);
                continue;
            }
            if (op == "error") {
                std::string what;
                message.Get("message", &what);
                reply.error = "websocket call refused: " + what;
                return reply;
            }
            if (op != "result") continue;
            int64_t status = 0;
            message.Get("status", &status);
            reply.status = static_cast<int>(status);
            reply.body = RawJsonMember(text, "body");
            reply.ok = true;
            reply.error.clear();
            return reply;
        }
        // Timed out or the socket broke: the answer may still come on it, so it is not reused
        Drop();
        if (reply.error == "receive timeout") return reply;
    }
    return reply;
}

bool WebSocketClient::Subscribe(const std::string& topicsJson) {
    topics_ = topicsJson;
    if (!IsOpen()) return true;  // sent when the connection opens
    std::string error;
    const net::Clock::time_point deadline = net::Clock::now() + std::chrono::seconds(timeoutSeconds_);
    if (!SendFrame(kText, "{\"op\":\"subscribe\",\"topics\":" + topics_ + "}", deadline, &error)) Drop();
    return true;
}

void WebSocketClient::TakeEvents(std::vector<std::string>* out, bool* reconnected) {
    // Whatever has arrived, without waiting (a frame cut short stays in in_ for the next call)
    if (IsOpen()) {
        std::string text;
        std::string error;
        const net::Clock::time_point deadline = net::Clock::now() + std::chrono::milliseconds(1);
        while (ReadMessage(&text, deadline, &error)) {
            const Json message = Json::Parse(text, nullptr);
            std::string topic;
            if (message.Get("t", &topic) && topic != "ws") events_.push_back(text);
        }
        if (error != "receive timeout") Drop();  // closed or broken: the next request opens it again
    }
    out->insert(out->end(), events_.begin(), events_.end());
    events_.clear();
    *reconnected = reconnected_;
    reconnected_ = false;
}

}  // namespace dbg
