#include "api-websocket.h"

#include <atomic>
#include <memory>
#include <string>

#include <drogon/HttpAppFramework.h>
#include <drogon/utils/Utilities.h>
#include <json/json.h>

#include "common/companion_helper.h"
#include "common/request_gates.h"
#include "common/session_helper.h"
#include "ws-hub.h"

namespace api
{
namespace v1
{
namespace
{
/// What a connection keeps: the handshake's identity, the token, the calls in flight
struct Connection
{
    std::string token;
    std::string userAgent;
    std::string source;  ///< X-Unreal-Source of the handshake ("mcp": an agent, never a companion)
    std::string peer;
    std::string peerHost;
    std::atomic<int> inFlight{0};
};

std::string Compact(const Json::Value& value)
{
    Json::StreamWriterBuilder builder;
    builder["indentation"] = "";
    return Json::writeString(builder, value);
}

void Send(const std::weak_ptr<drogon::WebSocketConnection>& weak, const Json::Value& message)
{
    if (auto connection = weak.lock(); connection && connection->connected())
        connection->send(Compact(message));
}

Json::Value Error(const Json::Value& rid, const std::string& error, const std::string& message)
{
    Json::Value out;
    out["op"] = "error";
    out["t"] = "call";
    if (!rid.isNull())
        out["rid"] = rid;
    out["error"] = error;
    out["message"] = message;
    return out;
}

bool MethodOf(const std::string& name, drogon::HttpMethod& method)
{
    static const std::pair<const char*, drogon::HttpMethod> methods[] = {
        {"GET", drogon::Get},       {"POST", drogon::Post},   {"PUT", drogon::Put},
        {"DELETE", drogon::Delete}, {"PATCH", drogon::Patch}, {"HEAD", drogon::Head}};
    for (const auto& [text, value] : methods)
        if (name == text)
        {
            method = value;
            return true;
        }
    return false;
}

/// The reply as the call's result: the JSON body when it is JSON, else {"text": ...}
Json::Value ResultOf(const Json::Value& rid, const drogon::HttpResponsePtr& resp)
{
    Json::Value out;
    out["op"] = "result";
    out["t"] = "call";
    out["rid"] = rid;
    out["status"] = static_cast<int>(resp->statusCode());
    const std::string_view text = resp->body();
    Json::Value body;
    Json::CharReaderBuilder reader;
    std::string errors;
    const std::unique_ptr<Json::CharReader> parser(reader.newCharReader());
    if (!text.empty() && parser->parse(text.data(), text.data() + text.size(), &body, &errors))
        out["body"] = body;
    else if (!text.empty())
        out["body"]["text"] = std::string(text);
    // The automation gate opened a session for this client: the next calls carry it ("session")
    if (const std::string& session = resp->getHeader(webapi_session::kHeader); !session.empty())
        out["session"] = session;
    return out;
}

/// Routes that are sockets themselves are not calls
bool IsSocketPath(const std::string& path)
{
    return path == "/api/v1/ws" || path == "/api/v1/websocket" || path == "/api/v1/companions/events";
}
}  // namespace

void ApiWebSocket::handleNewConnection(const drogon::HttpRequestPtr& req, const drogon::WebSocketConnectionPtr& wsConnPtr)
{
    auto connection = std::make_shared<Connection>();
    connection->token = webapi_companion::BearerOf(req).value_or("");
    connection->userAgent = req->getHeader("User-Agent");
    connection->source = req->getHeader("X-Unreal-Source");
    connection->peer = req->getPeerAddr().toIpPort();
    connection->peerHost = req->getPeerAddr().toIp();
    wsConnPtr->setContext(connection);

    // The topics and the surface topic (ws-hub.h): the hub sends and closes through these, from its loop
    std::weak_ptr<drogon::WebSocketConnection> weak = wsConnPtr;
    WsHub::Peer peer;
    peer.token = connection->token;
    peer.source = connection->source;
    peer.host = connection->peerHost;
    WsHub::Instance().Open(
        wsConnPtr.get(), std::move(peer),
        [weak](const std::string& text) {
            if (auto socket = weak.lock(); socket && socket->connected())
                socket->send(text);
        },
        [weak] {
            if (auto socket = weak.lock(); socket && socket->connected())
                socket->shutdown(drogon::CloseCode::kNormalClosure);
        });
}

void ApiWebSocket::handleConnectionClosed(const drogon::WebSocketConnectionPtr& wsConnPtr)
{
    WsHub::Instance().Remove(wsConnPtr.get());
    wsConnPtr->clearContext();
}

void ApiWebSocket::handleNewMessage(const drogon::WebSocketConnectionPtr& wsConnPtr, std::string&& message,
                                    const drogon::WebSocketMessageType& type)
{
    if (type != drogon::WebSocketMessageType::Text)
        return;  // pings are answered by drogon
    std::shared_ptr<Connection> connection = wsConnPtr->getContext<Connection>();
    if (!connection)
        return;
    std::weak_ptr<drogon::WebSocketConnection> weak = wsConnPtr;

    Json::Value request;
    Json::CharReaderBuilder reader;
    std::string errors;
    const std::unique_ptr<Json::CharReader> parser(reader.newCharReader());
    if (!parser->parse(message.data(), message.data() + message.size(), &request, &errors) || !request.isObject())
    {
        Send(weak, Error(Json::Value(), "bad_request", "a message is one JSON object with an op"));
        return;
    }
    const std::string op = request.get("op", "").asString();
    const Json::Value rid = request.get("rid", Json::Value());
    if (op == "auth")
    {
        const std::string token = request.get("token", "").asString();
        if (token.empty())
        {
            Send(weak, Error(rid, "bad_request", "auth takes a token"));
            return;
        }
        connection->token = token;
        WsHub::Instance().SetToken(wsConnPtr.get(), token);
        Json::Value ok;
        ok["op"] = "auth";
        ok["t"] = "call";
        ok["ok"] = true;
        Send(weak, ok);
        return;
    }
    if (op != "call")
    {
        // Topics and the surface topic: the hub
        if (!WsHub::Instance().HandleMessage(wsConnPtr.get(), request))
            Send(weak, Error(rid, "bad_request",
                             "unknown op '" + op + "' (call, auth, subscribe, unsubscribe, join, action, bye)"));
        return;
    }

    std::string target = request.get("path", "").asString();
    drogon::HttpMethod method = drogon::Get;
    if (!MethodOf(request.get("method", "GET").asString(), method))
    {
        Send(weak, Error(rid, "bad_request", "method: GET, POST, PUT, DELETE, PATCH or HEAD"));
        return;
    }
    if (target.rfind("/api/v1/", 0) != 0)
    {
        Send(weak, Error(rid, "bad_request", "path: a WebAPI route, /api/v1/..."));
        return;
    }
    std::string query;
    if (const size_t mark = target.find('?'); mark != std::string::npos)
    {
        query = target.substr(mark + 1);
        target.resize(mark);
    }
    if (IsSocketPath(target))
    {
        Send(weak, Error(rid, "bad_request", target + " is a socket, not a call"));
        return;
    }
    if (connection->inFlight.load() >= kMaxCallsInFlight)
    {
        Send(weak, Error(rid, "busy", "too many calls in flight on this connection; wait for results"));
        return;
    }

    const Json::Value& body = request["body"];
    drogon::HttpRequestPtr call =
        body.isNull() ? drogon::HttpRequest::newHttpRequest() : drogon::HttpRequest::newHttpJsonRequest(body);
    call->setMethod(method);
    call->setPath(drogon::utils::urlDecode(target));
    for (size_t start = 0; start < query.size();)
    {
        size_t end = query.find('&', start);
        if (end == std::string::npos)
            end = query.size();
        const std::string pair = query.substr(start, end - start);
        const size_t equals = pair.find('=');
        if (!pair.empty())
            call->setParameter(drogon::utils::urlDecode(pair.substr(0, equals)),
                               equals == std::string::npos ? std::string()
                                                           : drogon::utils::urlDecode(pair.substr(equals + 1)));
        start = end + 1;
    }
    if (!connection->token.empty())
        call->addHeader("Authorization", "Bearer " + connection->token);
    if (!connection->userAgent.empty())
        call->addHeader("User-Agent", connection->userAgent);
    if (!connection->source.empty())
        call->addHeader("X-Unreal-Source", connection->source);
    if (request["session"].isString())
        call->addHeader(webapi_session::kHeader, request["session"].asString());
    call->attributes()->insert(webapi_session::kPeerAttribute, connection->peer);
    call->attributes()->insert(webapi_session::kPeerHostAttribute, connection->peerHost);

    // The checks every HTTP request passes before routing (drogon's forward skips them)
    if (const drogon::HttpResponsePtr refused = webapi_gate::PassRequestGates(call))
    {
        Send(weak, ResultOf(rid, refused));
        return;
    }
    connection->inFlight++;
    drogon::app().forward(call, [weak, rid, connection](const drogon::HttpResponsePtr& resp) {
        connection->inFlight--;
        Send(weak, ResultOf(rid, resp));
    });
}
}  // namespace v1
}  // namespace api
