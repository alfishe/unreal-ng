#pragma once

/// @file cpu_selector.h
/// @brief The `cpu` of the register, memory, disassembly and step routes (gs-debugger design §7.1): ?cpu= or the
/// body's "cpu" names main, gs or ms (none: main); a card CPU must be there now (core CpuView::Select).

#include <functional>
#include <memory>
#include <string>

#include <drogon/HttpRequest.h>
#include <drogon/HttpResponse.h>
#include <json/json.h>

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
    auto resp = drogon::HttpResponse::newHttpJsonResponse(body);
    resp->setStatusCode(notThere ? drogon::k404NotFound : drogon::k400BadRequest);
    api::v1::addCorsHeaders(resp);
    callback(resp);
    return false;
}

}  // namespace webapi_cpu
