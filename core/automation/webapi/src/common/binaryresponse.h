#pragma once

/// The answer of a memory read with format=binary (docs/inprogress/2026-10-04-debugger-snapshot/tdd.md §3): the bytes
/// as application/octet-stream and three headers naming what was read, exposed to browser clients too.

#include <drogon/HttpResponse.h>

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace api
{
namespace v1
{
void addCorsHeaders(drogon::HttpResponsePtr& resp);

/// `space` is the space or region name, `address` the start, the body `bytes`
inline drogon::HttpResponsePtr BinaryMemoryResponse(const std::string& space, uint32_t address,
                                                    const std::vector<uint8_t>& bytes)
{
    auto resp = drogon::HttpResponse::newHttpResponse();
    resp->setContentTypeCode(drogon::CT_APPLICATION_OCTET_STREAM);
    resp->setBody(std::string(bytes.begin(), bytes.end()));
    char hex[16];
    std::snprintf(hex, sizeof(hex), "0x%04X", address);
    resp->addHeader("X-Unreal-Space", space);
    resp->addHeader("X-Unreal-Address", hex);
    resp->addHeader("X-Unreal-Length", std::to_string(bytes.size()));
    addCorsHeaders(resp);
    resp->addHeader("Access-Control-Expose-Headers", "X-Unreal-Space, X-Unreal-Address, X-Unreal-Length, X-Region, X-Offset");
    return resp;
}
}  // namespace v1
}  // namespace api
