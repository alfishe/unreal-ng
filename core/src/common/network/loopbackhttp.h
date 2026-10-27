#pragma once

/// @file loopbackhttp.h
/// @brief A small blocking HTTP/1.1 client for 127.0.0.1 (a companion's automation port: the Stream Deck daemon's
/// `/api/v1/health`, `/decks`, `/pair`). Plain TCP over netsock, `Connection: close`, a bounded time for the whole
/// exchange; chunked answers are joined. Call it from a worker thread, never the GUI's or the emulation's.

#include <string>

namespace netsock
{

struct HttpReply
{
    int status = 0;     ///< 0 = no answer (error says why)
    std::string body;
    std::string error;
    bool Ok() const { return status >= 200 && status < 300; }
};

/// `method` `path` on 127.0.0.1:`port`, a JSON `body` when not empty, the whole exchange within `timeoutMs`
HttpReply LoopbackHttp(int port, const std::string& method, const std::string& path, const std::string& body = {},
                       int timeoutMs = 1000);

} // namespace netsock
