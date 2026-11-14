// unreal-mcp-bridge — stdio MCP bridge daemon
//
// Speaks newline-delimited JSON-RPC 2.0 on stdin/stdout and forwards each
// line as one HTTP POST to the Unreal-NG MCP endpoint (Streamable HTTP).
// The bridge never interprets the protocol: it is a pure byte pipe plus
// connection handling, so any MCP client can adopt it as its command.
// See bridge/README.md for usage and the .mcp.json snippet.

#include "mini-http.h"

#include <algorithm>
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

/// The raw "id" member of a JSON-RPC request line (a number, a string or null, verbatim), "" when the line has
/// none (a notification). A line-level scan of the top-level object - strings and nesting are skipped - not a parser
std::string RequestId(const std::string& line)
{
    int depth = 0;
    size_t i = 0;
    auto skipString = [&](size_t from) {
        size_t j = from + 1;
        while (j < line.size() && line[j] != '"')
            j += (line[j] == '\\') ? 2 : 1;
        return j + 1;  // past the closing quote
    };
    while (i < line.size())
    {
        const char c = line[i];
        if (c == '"')
        {
            const size_t end = skipString(i);
            if (depth == 1 && end <= line.size() && line.compare(i, end - i, "\"id\"") == 0)
            {
                size_t v = end;
                while (v < line.size() && (line[v] == ' ' || line[v] == '\t' || line[v] == ':'))
                    v++;
                if (v >= line.size())
                    return "";
                if (line[v] == '"')
                    return line.substr(v, std::min(skipString(v), line.size()) - v);
                size_t e = v;
                while (e < line.size() && line[e] != ',' && line[e] != '}' && line[e] != ' ' && line[e] != '\t')
                    e++;
                return line.substr(v, e - v);
            }
            i = end;
            continue;
        }
        if (c == '{' || c == '[')
            depth++;
        else if (c == '}' || c == ']')
            depth--;
        i++;
    }
    return "";
}

/// Transport-level failure surfaced to the MCP client as JSON-RPC -32603, answered under the request's own id so the
/// client ends the call at once instead of waiting for its timeout. Two cases: nothing listens at the URL (unreal-qt
/// not running, exited or crashed before the call), or the connection broke during the call (the process went away
/// while working on it). data.bridge names the case for scripts
std::string BridgeError(const std::string& id, const std::string& url, const std::string& detail, bool connected)
{
    const std::string kind = connected ? "connection_lost" : "unreachable";
    const std::string message = connected
        ? "bridge: the connection to the emulator at " + url + " broke during the call (" + detail +
              "): the unreal-qt process may have exited or crashed"
        : "bridge: no emulator answers at " + url + " (" + detail +
              "): unreal-qt is not running, or it has exited or crashed";
    return std::string("{\"jsonrpc\":\"2.0\",\"id\":") + (id.empty() ? "null" : id) +
           ",\"error\":{\"code\":-32603,\"message\":\"" + EscapeJson(message) + "\",\"data\":{\"bridge\":\"" + kind +
           "\",\"url\":\"" + EscapeJson(url) + "\",\"detail\":\"" + EscapeJson(detail) + "\"}}}";
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
            // A notification gets no answer, not even an error (JSON-RPC 2.0, section 4.1)
            const std::string id = RequestId(line);
            if (!id.empty())
                std::cout << BridgeError(id, url, result.error, result.connected) << std::endl; // std::endl: line + flush
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
