/// @file mcp-sse_test.cpp
/// @brief Unit tests for SSE framing and progress notification builders.
///
/// mcp-sse.h is drogon-free by design — these tests pin the exact wire
/// format (event/data frames, no raw newlines inside data payloads) and
/// the notifications/progress params shape from the 2025-03-26 spec.

#include <gtest/gtest.h>

#include <json/json.h>

#include <algorithm>
#include <memory>
#include <string>

#include "mcp-protocol.h"
#include "mcp-sse.h"

namespace
{

/// Parses compact JSON back into a Value (for round-trip assertions)
Json::Value ParseJson(const std::string& text)
{
    Json::Value value;
    Json::CharReaderBuilder builder;
    std::unique_ptr<Json::CharReader> reader(builder.newCharReader());
    std::string errors;
    reader->parse(text.data(), text.data() + text.size(), &value, &errors);
    return value;
}

/// Extracts the data payload from one SSE frame ("data: <json>\n" line)
std::string DataPayload(const std::string& frame)
{
    const size_t start = frame.find("data: ") + std::string("data: ").size();
    return frame.substr(start, frame.find('\n', start) - start);
}

} // namespace

// ===========================================================================
// SSE framing
// ===========================================================================

TEST(McpSse_Test, EncodeSseEvent_FramesDefaultMessageEvent)
{
    Json::Value message;
    message["jsonrpc"] = "2.0";
    message["id"] = 1;
    message["result"] = "ok";

    const std::string frame = mcp::EncodeSseEvent(message);

    // jsoncpp writes object members sorted; compact (no indentation)
    EXPECT_EQ(frame, "event: message\ndata: {\"id\":1,\"jsonrpc\":\"2.0\",\"result\":\"ok\"}\n\n");
}

TEST(McpSse_Test, EncodeSseEvent_SupportsCustomEventName)
{
    Json::Value message;
    message["jsonrpc"] = "2.0";

    const std::string frame = mcp::EncodeSseEvent(message, "ping");

    EXPECT_EQ(frame, "event: ping\ndata: {\"jsonrpc\":\"2.0\"}\n\n");
}

TEST(McpSse_Test, EncodeSseEvent_EscapesPayloadNewlines)
{
    // A literal newline inside a data payload would split the SSE frame —
    // jsoncpp must keep it escaped
    Json::Value message;
    message["jsonrpc"] = "2.0";
    message["result"]["text"] = "line one\nline two";

    const std::string frame = mcp::EncodeSseEvent(message);

    EXPECT_EQ(std::count(frame.begin(), frame.end(), '\n'), 3u); // two framing newlines + none inside data
    EXPECT_NE(frame.find("\\n"), std::string::npos);

    // And the payload still round-trips
    const Json::Value parsed = ParseJson(DataPayload(frame));
    EXPECT_EQ(parsed["result"]["text"].asString(), "line one\nline two");
}

TEST(McpSse_Test, EncodeSseEvent_RoundTripsThroughJsonParser)
{
    Json::Value message;
    message["jsonrpc"] = "2.0";
    message["id"] = "abc";
    message["result"]["items"].append(1);
    message["result"]["items"].append(2);

    const Json::Value parsed = ParseJson(DataPayload(mcp::EncodeSseEvent(message)));

    EXPECT_EQ(parsed["id"].asString(), "abc");
    ASSERT_TRUE(parsed["result"]["items"].isArray());
    EXPECT_EQ(parsed["result"]["items"].size(), 2u);
}

TEST(McpSse_Test, Keepalive_IsAnSseCommentFrame)
{
    EXPECT_EQ(std::string(mcp::kSseKeepalive), ": keepalive\n\n");
}

// ===========================================================================
// notifications/progress builder
// ===========================================================================

TEST(McpSse_Test, MakeProgressNotification_FullShape)
{
    Json::Value token(42);
    const Json::Value notification = mcp::MakeProgressNotification(token, 2.0, 5.0, "aspect registers");

    EXPECT_EQ(notification["jsonrpc"].asString(), "2.0");
    EXPECT_EQ(notification["method"].asString(), "notifications/progress");
    EXPECT_FALSE(notification.isMember("id")); // notifications carry no id

    EXPECT_EQ(notification["params"]["progressToken"].asInt(), 42);
    EXPECT_EQ(notification["params"]["progress"].asDouble(), 2.0);
    EXPECT_EQ(notification["params"]["total"].asDouble(), 5.0);
    EXPECT_EQ(notification["params"]["message"].asString(), "aspect registers");
}

TEST(McpSse_Test, MakeProgressNotification_EchoesStringToken)
{
    Json::Value token("tok-7");
    const Json::Value notification = mcp::MakeProgressNotification(token, 1.0, 0.0, "");

    EXPECT_EQ(notification["params"]["progressToken"].asString(), "tok-7");
    EXPECT_EQ(notification["params"]["progress"].asDouble(), 1.0);
    EXPECT_FALSE(notification["params"].isMember("total"));   // 0 → omitted
    EXPECT_FALSE(notification["params"].isMember("message")); // empty → omitted
}

TEST(McpSse_Test, MakeProgressNotification_IsSseSafe)
{
    // The notification must survive SSE framing unchanged
    Json::Value token(1);
    const Json::Value notification = mcp::MakeProgressNotification(token, 1.0, 2.0, "step");

    const Json::Value parsed = ParseJson(DataPayload(mcp::EncodeSseEvent(notification)));

    EXPECT_EQ(parsed["method"].asString(), "notifications/progress");
    EXPECT_EQ(parsed["params"]["progressToken"].asInt(), 1);
}
