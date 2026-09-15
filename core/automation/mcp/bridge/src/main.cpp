// unreal-mcp-bridge — stdio MCP bridge daemon
//
// Speaks newline-delimited JSON-RPC 2.0 on stdin/stdout and forwards each
// line as one HTTP POST to the Unreal-NG MCP endpoint (Streamable HTTP).
// The bridge never interprets the protocol: it is a pure byte pipe plus
// connection handling, so any MCP client can adopt it as its command.
// See bridge/README.md for usage and the .mcp.json snippet.

#include "mini-http.h"

#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

namespace
{

constexpr const char* kDefaultUrl = "http://127.0.0.1:8092/mcp";

std::string ResolveUrl(int argc, char** argv)
{
    for (int i = 1; i < argc - 1; ++i)
    {
        if (std::strcmp(argv[i], "--url") == 0)
        {
            return argv[i + 1];
        }
    }

    const char* env = std::getenv("UNREAL_MCP_URL");
    if (env != nullptr && *env != '\0')
    {
        return env;
    }

    return kDefaultUrl;
}

/// Minimal JSON string escaping (the bridge otherwise never touches JSON)
std::string EscapeJson(const std::string& text)
{
    std::string out;
    out.reserve(text.size() + 8);
    for (char c : text)
    {
        switch (c)
        {
            case '"':
                out += "\\\"";
                break;
            case '\\':
                out += "\\\\";
                break;
            case '\n':
                out += "\\n";
                break;
            case '\r':
                out += "\\r";
                break;
            case '\t':
                out += "\\t";
                break;
            default:
                if (static_cast<unsigned char>(c) < 0x20)
                {
                    char buffer[8];
                    std::snprintf(buffer, sizeof(buffer), "\\u%04x", static_cast<unsigned char>(c));
                    out += buffer;
                }
                else
                {
                    out += c;
                }
                break;
        }
    }
    return out;
}

/// Transport-level failure surfaced to the MCP client as JSON-RPC -32603
std::string BridgeError(const std::string& url, const std::string& detail)
{
    return std::string("{\"jsonrpc\":\"2.0\",\"id\":null,\"error\":{\"code\":-32603,\"message\":\"")
           + "bridge: cannot reach " + EscapeJson(url) + " (" + EscapeJson(detail) + ")\"}}";
}

/// Extracts every "data:" payload from an SSE body, one element per event.
/// Pure line-level translation: "event:"/"id:"/"retry:" lines and ":" comment
/// frames (keepalives) are dropped, the JSON payloads pass through verbatim —
/// the bridge still never parses JSON.
std::vector<std::string> ExtractSseDataLines(const std::string& sseBody)
{
    std::vector<std::string> payloads;
    size_t pos = 0;
    while (pos < sseBody.size())
    {
        size_t lineEnd = sseBody.find('\n', pos);
        const size_t next = (lineEnd == std::string::npos) ? sseBody.size() : lineEnd + 1;
        std::string line = sseBody.substr(pos, (lineEnd == std::string::npos ? sseBody.size() : lineEnd) - pos);
        if (!line.empty() && line.back() == '\r')
        {
            line.pop_back();
        }
        if (line.compare(0, 5, "data:") == 0)
        {
            size_t start = 5;
            if (start < line.size() && line[start] == ' ')
            {
                ++start; // one optional leading space per the SSE spec
            }
            if (start < line.size())
            {
                payloads.push_back(line.substr(start));
            }
        }
        pos = next;
    }
    return payloads;
}

} // namespace

int main(int argc, char** argv)
{
#ifndef _WIN32
    // Writing to a closed stdout pipe must not kill the daemon
    std::signal(SIGPIPE, SIG_IGN);
#endif

    const std::string url = ResolveUrl(argc, argv);

    std::string host;
    int port = 0;
    std::string path;
    if (!bridge::ParseUrl(url, host, port, path))
    {
        std::cerr << "unreal-mcp-bridge: invalid URL '" << url << "' (expected http://host[:port]/path)" << std::endl;
        return 2;
    }

    std::string line;
    while (std::getline(std::cin, line))
    {
        if (line.empty())
        {
            continue;
        }

        const bridge::HttpResult result = bridge::Post(host, port, path, line);
        if (!result.ok)
        {
            std::cout << BridgeError(url, result.error) << std::endl; // std::endl: line + flush
            continue;
        }

        // SSE answer (progressToken request): each event's data payload becomes
        // exactly one stdout line — notifications/progress arrive before the
        // final JSON-RPC response, in server order
        if (result.contentType.find("text/event-stream") != std::string::npos)
        {
            for (const std::string& payload : ExtractSseDataLines(result.body))
            {
                std::cout << payload << std::endl; // std::endl: line + flush
            }
            continue;
        }

        // Notifications (and any empty body, e.g. 202) stay silent so the
        // client-side line protocol never sees a non-JSON-RPC response
        if (!result.body.empty())
        {
            std::cout << result.body << std::endl;
        }
    }

    // stdin closed (client exited) → clean exit
    return 0;
}
