#pragma once

/// @file cpu_selector.h
/// @brief The `cpu` of the register, memory, disassembly and step routes (gs-debugger design §7.1): ?cpu= or the
/// body's "cpu" names main, gs or ms (none: main); a card CPU must be there now (core CpuView::Select).

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include <drogon/HttpRequest.h>
#include <drogon/HttpResponse.h>
#include <json/json.h>

#include "debugger/debugerror.h"
#include "debugger/debugmanager.h"
#include "debugger/targets/cpuview.h"
#include "emulator/emulator.h"

namespace api::v1
{
void addCorsHeaders(drogon::HttpResponsePtr& resp);
}

namespace webapi_cpu
{

/// The CPU the request names into `cpu`; false after the reply: 400 for an unknown name, 404 for a card CPU that is
/// not there now (a card without a CPU, an empty slot)
inline bool CpuOrError(const std::shared_ptr<Emulator>& emulator, const drogon::HttpRequestPtr& req,
                       std::function<void(const drogon::HttpResponsePtr&)>& callback, DebugCpuId& cpu)
{
    std::string name = req->getParameter("cpu");
    if (name.empty())
        if (auto json = req->getJsonObject(); json && json->isObject() && (*json)["cpu"].isString())
            name = (*json)["cpu"].asString();
    cpu = DebugCpuId::Main;
    const std::string error = CpuView::Select(emulator->GetContext(), name, cpu);
    if (error.empty())
        return true;
    DebugCpuId known = DebugCpuId::Main;
    const bool notThere = DebugManager::ParseCpuName(name, known);
    Json::Value body;
    body["error"] = notThere ? "Not Found" : "Bad Request";
    body["message"] = error;
    body["code"] = CpuView::SelectCode(name);
    auto resp = drogon::HttpResponse::newHttpJsonResponse(body);
    resp->setStatusCode(notThere ? drogon::k404NotFound : drogon::k400BadRequest);
    api::v1::addCorsHeaders(resp);
    callback(resp);
    return false;
}

/// The request's CPU for a capability only the main CPU has (`what`: "the PC history", ...): true for main; a card
/// CPU is refused after the reply, 400 with code not_supported (CpuView::MainOnly), as is a bad or absent one
/// (CpuOrError)
inline bool MainOnlyOrError(const std::shared_ptr<Emulator>& emulator, const drogon::HttpRequestPtr& req,
                            std::function<void(const drogon::HttpResponsePtr&)>& callback, const std::string& what)
{
    DebugCpuId cpu = DebugCpuId::Main;
    if (!CpuOrError(emulator, req, callback, cpu))
        return false;
    const std::string refused = CpuView::MainOnly(cpu, what);
    if (refused.empty())
        return true;
    Json::Value body;
    body["error"] = "Bad Request";
    body["message"] = refused;
    body["code"] = DebugError::kNotSupported;
    auto resp = drogon::HttpResponse::newHttpJsonResponse(body);
    resp->setStatusCode(drogon::k400BadRequest);
    api::v1::addCorsHeaders(resp);
    callback(resp);
    return false;
}

/// The page routes' `cpu` (/memory/page/{type}/{page}, /memory/{type}/{page}/{offset}): true when answered here - a
/// bad or absent CPU (CpuOrError), a card CPU's page read ({cpu, type, page, offset, length, data, hex};
/// CpuView::PageRead, 400 invalid_argument when refused) or its page write (400 not_supported:
/// CpuView::kCardPageWriteRefusal). False for the main CPU: the route reads the machine's page
inline bool CardPageReply(const std::shared_ptr<Emulator>& emulator, const drogon::HttpRequestPtr& req,
                          std::function<void(const drogon::HttpResponsePtr&)>& callback, bool write,
                          const std::string& type, const std::string& pageText, uint64_t offset, uint64_t length)
{
    DebugCpuId cpu = DebugCpuId::Main;
    if (!CpuOrError(emulator, req, callback, cpu))
        return true;
    if (!IsCardCpu(cpu))
        return false;
    Json::Value body;
    drogon::HttpStatusCode status = drogon::k200OK;
    std::vector<uint8_t> bytes;
    std::string error;
    uint64_t page = 0;
    if (write)
    {
        error = CpuView::kCardPageWriteRefusal;
        body["code"] = DebugError::kNotSupported;
    }
    else if (pageText.empty() || pageText.find_first_not_of("0123456789") != std::string::npos ||
             (page = std::stoull(pageText.substr(0, 9))) > 0xFFFF || offset > 0xFFFF)
    {
        error = "page and offset are unsigned numbers";
        body["code"] = DebugError::kInvalidArgument;
    }
    else if (error = CpuView::PageRead(emulator->GetContext(), cpu, type, static_cast<uint32_t>(page),
                                       static_cast<uint32_t>(offset), static_cast<uint32_t>(length), bytes);
             !error.empty())
        body["code"] = DebugError::kInvalidArgument;
    if (!error.empty())
    {
        status = drogon::k400BadRequest;
        body["error"] = "Bad Request";
        body["message"] = error;
    }
    else
    {
        body["cpu"] = DebugCpuName(cpu);
        body["type"] = type;
        body["page"] = static_cast<Json::UInt64>(page);
        body["offset"] = static_cast<Json::UInt64>(offset);
        body["length"] = static_cast<Json::UInt64>(bytes.size());
        Json::Value data(Json::arrayValue);
        std::string hex;
        static const char* kDigits = "0123456789ABCDEF";
        for (size_t i = 0; i < bytes.size(); i++)
        {
            data.append(bytes[i]);
            if (i)
                hex += ' ';
            hex += kDigits[bytes[i] >> 4];
            hex += kDigits[bytes[i] & 15];
        }
        body["data"] = data;
        body["hex"] = hex;
    }
    auto resp = drogon::HttpResponse::newHttpJsonResponse(body);
    resp->setStatusCode(status);
    api::v1::addCorsHeaders(resp);
    callback(resp);
    return true;
}

}  // namespace webapi_cpu
