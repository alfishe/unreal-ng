// Minimal blocking HTTP/1.1 client for the local unreal-ng WebAPI (plain TCP, Connection: close, chunked aware).
#pragma once
#include <string>
#include <utility>
#include <vector>

namespace sdb
{

struct HttpResponse
{
    int status = 0;  // 0 = transport error (see error)
    std::string body;
    std::vector<std::pair<std::string, std::string>> headers;
    std::string error;
    double ms = 0;  // round trip
    [[nodiscard]] bool Ok() const
    {
        return status >= 200 && status < 300;
    }
    [[nodiscard]] std::string Header(const std::string& name) const;
};

// url = "http://host:port" (no path). body is sent as application/json when non-empty.
HttpResponse HttpRequest(const std::string& url, const std::string& method, const std::string& path,
                         const std::string& body = {},
                         const std::vector<std::pair<std::string, std::string>>& headers = {}, int timeoutMs = 4000);

}  // namespace sdb
