/// Device memory regions (core emulator/memory/devicememory.h; Sprinter automation audit G5):
/// memory a device owns outside the CPU's RAM / ROM pages - the Sprinter's 256 KB video RAM -
/// listed, read, written, saved and loaded by name. Every interface calls the same core
/// functions (DeviceMemory::*, DeviceState::MemoryRegions / MemoryRegionRead).

#include <drogon/HttpResponse.h>
#include <emulator/emulator.h>
#include <emulator/emulatormanager.h>
#include <emulator/memory/devicememory.h>
#include <emulator/state/devicestate.h>
#include <json/json.h>

#include "../common/statenode_json.h"
#include "../emulator_api.h"

using namespace drogon;

namespace api
{
namespace v1
{

void addCorsHeaders(HttpResponsePtr& resp);

namespace
{
void Reply(const Json::Value& body, std::function<void(const HttpResponsePtr&)>& callback,
           HttpStatusCode code = HttpStatusCode::k200OK)
{
    auto resp = HttpResponse::newHttpJsonResponse(body);
    resp->setStatusCode(code);
    addCorsHeaders(resp);
    callback(resp);
}

void ReplyError(const std::string& message, std::function<void(const HttpResponsePtr&)>& callback,
                HttpStatusCode code = HttpStatusCode::k400BadRequest)
{
    Json::Value error;
    error["error"] = code == HttpStatusCode::k404NotFound ? "Not Found" : "Bad Request";
    error["message"] = message;
    Reply(error, callback, code);
}

/// A JSON number or a string ("0x17F0", "#17F0", "6128")
bool NumberField(const Json::Value& value, uint32_t& out)
{
    if (value.isUInt())
    {
        out = value.asUInt();
        return true;
    }
    return value.isString() && DeviceMemory::ParseNumber(value.asString(), out);
}

/// Read [offset, offset + length) and answer as JSON (hex / data / sparse) or raw bytes (binary)
void ReplyRead(EmulatorContext* context, const std::string& name, uint32_t offset, uint32_t length,
               const std::string& format, std::function<void(const HttpResponsePtr&)>& callback)
{
    if (format == "binary")
    {
        std::vector<uint8_t> bytes;
        std::string error;
        if (!DeviceMemory::Read(context, name, offset, length, bytes, error))
            return ReplyError(error, callback);
        auto resp = HttpResponse::newHttpResponse();
        resp->setContentTypeCode(CT_APPLICATION_OCTET_STREAM);
        resp->setBody(std::string(bytes.begin(), bytes.end()));
        resp->addHeader("X-Region", name);
        resp->addHeader("X-Offset", std::to_string(offset));
        addCorsHeaders(resp);
        callback(resp);
        return;
    }
    const StateNode node = DeviceState::MemoryRegionRead(context, name, offset, length, format);
    const StateNode* available = node.find("available");
    if (available && !available->b)
    {
        const StateNode* description = node.find("description");
        return ReplyError(description ? description->s : "not available", callback);
    }
    Reply(StateNodeToJson(node), callback);
}
}  // namespace

/// @brief GET /api/v1/emulator/{id}/memory/regions - the machine's device memory regions
void EmulatorAPI::getMemoryRegions(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback,
                                   const std::string& id) const
{
    (void)req;
    auto emulator = getEmulatorByIdOrIndex(id);
    if (!emulator)
        return ReplyError("Emulator not found with ID: " + id, callback, HttpStatusCode::k404NotFound);
    Reply(StateNodeToJson(DeviceState::MemoryRegions(emulator->GetContext())), callback);
}

/// @brief GET /api/v1/emulator/{id}/memory/region/{name}?offset=&length=&format=hex|data|sparse|binary
void EmulatorAPI::getMemoryRegion(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback,
                                  const std::string& id, const std::string& name) const
{
    auto emulator = getEmulatorByIdOrIndex(id);
    if (!emulator)
        return ReplyError("Emulator not found with ID: " + id, callback, HttpStatusCode::k404NotFound);
    EmulatorContext* context = emulator->GetContext();
    std::string error;
    IDeviceMemoryRegion* region = DeviceMemory::Find(context, name, &error);
    if (!region)
        return ReplyError(error, callback, HttpStatusCode::k404NotFound);

    uint32_t offset = 0;
    uint32_t length = 256;
    const std::string offsetText = req->getParameter("offset");
    const std::string lengthText = req->getParameter("length");
    if (!offsetText.empty() && !DeviceMemory::ParseNumber(offsetText, offset))
        return ReplyError("offset: a number (decimal, 0x or # hex)", callback);
    if (!lengthText.empty() && !DeviceMemory::ParseNumber(lengthText, length))
        return ReplyError("length: a number (decimal, 0x or # hex)", callback);
    const std::string format = req->getParameter("format").empty() ? std::string("hex") : req->getParameter("format");
    if (format != "hex" && format != "data" && format != "sparse" && format != "binary")
        return ReplyError("format must be hex, data, sparse or binary", callback);
    if (format != "binary" && length > 65536)
        return ReplyError("length: at most 65536 bytes as JSON (format=binary for more)", callback);
    ReplyRead(context, region->Name(), offset, length, format, callback);
}

/// @brief POST /api/v1/emulator/{id}/memory/region/{name} - write {"offset", "hex" | "data"}, or
/// {"action": "save", "path", "offset", "length"} / {"action": "load", "path", "offset"}
void EmulatorAPI::postMemoryRegion(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback,
                                   const std::string& id, const std::string& name) const
{
    auto emulator = getEmulatorByIdOrIndex(id);
    if (!emulator)
        return ReplyError("Emulator not found with ID: " + id, callback, HttpStatusCode::k404NotFound);
    EmulatorContext* context = emulator->GetContext();
    std::string error;
    IDeviceMemoryRegion* region = DeviceMemory::Find(context, name, &error);
    if (!region)
        return ReplyError(error, callback, HttpStatusCode::k404NotFound);
    auto body = req->getJsonObject();
    if (!body || !body->isObject())
        return ReplyError("JSON body required: {\"offset\", \"hex\" | \"data\"} or {\"action\": \"save\" | \"load\", \"path\"}",
                          callback);

    uint32_t offset = 0;
    if (body->isMember("offset") && !NumberField((*body)["offset"], offset))
        return ReplyError("offset: a number or a string (decimal, 0x or # hex)", callback);
    const std::string action = (*body)["action"].asString();

    Json::Value ret;
    ret["region"] = region->Name();
    ret["offset"] = offset;
    if (action == "save")
    {
        uint32_t length = 0;
        if (body->isMember("length") && !NumberField((*body)["length"], length))
            return ReplyError("length: a number", callback);
        const std::string path = (*body)["path"].asString();
        if (!DeviceMemory::Save(context, region->Name(), path, offset, length, error))
            return ReplyError(error, callback);
        ret["success"] = true;
        ret["path"] = path;
        ret["length"] = length ? length : region->Size() - offset;
        return Reply(ret, callback);
    }
    if (action == "load")
    {
        size_t written = 0;
        const std::string path = (*body)["path"].asString();
        if (!DeviceMemory::Load(context, region->Name(), path, offset, written, error))
            return ReplyError(error, callback);
        ret["success"] = true;
        ret["path"] = path;
        ret["bytes_written"] = static_cast<Json::UInt64>(written);
        return Reply(ret, callback);
    }
    if (!action.empty() && action != "write")
        return ReplyError("action must be write (the default), save or load", callback);

    std::vector<uint8_t> bytes;
    if (body->isMember("hex"))
    {
        if (!DeviceMemory::ParseHexBytes((*body)["hex"].asString(), bytes))
            return ReplyError("hex: pairs of hex digits (\"00 00 A8\" or \"0000A8\")", callback);
    }
    else if (body->isMember("data") && (*body)["data"].isArray())
    {
        for (const Json::Value& v : (*body)["data"])
        {
            if (!v.isUInt() || v.asUInt() > 0xFF)
                return ReplyError("data: an array of bytes 0-255", callback);
            bytes.push_back(static_cast<uint8_t>(v.asUInt()));
        }
    }
    else
        return ReplyError("a write needs hex or data", callback);
    if (!DeviceMemory::Write(context, region->Name(), offset, bytes, "WebAPI region write", error))
        return ReplyError(error, callback);
    ret["success"] = true;
    ret["bytes_written"] = static_cast<Json::UInt64>(bytes.size());
    Reply(ret, callback);
}

}  // namespace v1
}  // namespace api
