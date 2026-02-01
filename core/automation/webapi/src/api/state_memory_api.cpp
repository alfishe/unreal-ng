// WebAPI State Memory Inspection Implementation
// Extracted from emulator_api.cpp - 2026-01-08

#include "../emulator_api.h"

#include <drogon/HttpResponse.h>
#include <emulator/emulator.h>
#include <emulator/emulatormanager.h>
#include <json/json.h>
#include <common/stringhelper.h>

using namespace drogon;
using namespace api::v1;

namespace api
{
namespace v1
{

// Helper function declared in emulator_api.cpp
extern void addCorsHeaders(HttpResponsePtr& resp);

/// @brief GET /api/v1/emulator/{id}/state/memory
/// @brief Get complete memory configuration
void EmulatorAPI::getStateMemory(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback,
                                 const std::string& id) const
{
    auto manager = EmulatorManager::GetInstance();
    auto emulator = manager->GetEmulator(id);

    if (!emulator)
    {
        Json::Value error;
        error["error"] = "Not Found";
        error["message"] = "Emulator with specified ID not found";

        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k404NotFound);
        addCorsHeaders(resp);
        callback(resp);
        return;
    }

    EmulatorContext* context = emulator->GetContext();
    if (!context)
    {
        Json::Value error;
        error["error"] = "Internal Error";
        error["message"] = "Unable to access emulator context";

        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k500InternalServerError);
        addCorsHeaders(resp);
        callback(resp);
        return;
    }

    CONFIG& config = context->config;
    Memory& memory = *context->pMemory;
    EmulatorState& state = context->emulatorState;
    Json::Value ret;

    // Model information
    std::string model = "ZX Spectrum 48K";
    if (config.mem_model == MM_SPECTRUM128)
        model = "ZX Spectrum 128K";
    else if (config.mem_model == MM_PENTAGON)
        model = "Pentagon 128K";
    else if (config.mem_model == MM_PLUS3)
        model = "ZX Spectrum +3";

    ret["model"] = model;

    // ROM configuration
    Json::Value rom;
    rom["active_page"] = static_cast<int>(memory.GetROMPage());
    rom["is_bank0_rom"] = memory.IsBank0ROM();
    ret["rom"] = rom;

    // RAM configuration
    Json::Value ram;
    ram["bank0"] = memory.IsBank0ROM() ? Json::Value::null : static_cast<int>(memory.GetRAMPageForBank0());
    ram["bank1"] = static_cast<int>(memory.GetRAMPageForBank1());
    ram["bank2"] = static_cast<int>(memory.GetRAMPageForBank2());
    ram["bank3"] = static_cast<int>(memory.GetRAMPageForBank3());
    ret["ram"] = ram;

    // Paging state (if applicable)
    if (config.mem_model != MM_SPECTRUM48)
    {
        Json::Value paging;
        paging["port_7ffd"] = static_cast<int>(state.p7FFD);
        paging["ram_bank_3"] = static_cast<int>(state.p7FFD & 0x07);
        paging["screen"] = (state.p7FFD & 0x08) ? 1 : 0;
        paging["rom_select"] = (state.p7FFD & 0x10) ? 1 : 0;
        paging["locked"] = (state.p7FFD & 0x20) ? true : false;
        ret["paging"] = paging;
    }

    auto resp = HttpResponse::newHttpJsonResponse(ret);
    addCorsHeaders(resp);
    callback(resp);
}

/// @brief GET /api/v1/emulator/{id}/state/memory/ram
/// @brief Get RAM banking details
void EmulatorAPI::getStateMemoryRAM(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback,
                                    const std::string& id) const
{
    auto manager = EmulatorManager::GetInstance();
    auto emulator = manager->GetEmulator(id);

    if (!emulator)
    {
        Json::Value error;
        error["error"] = "Not Found";
        error["message"] = "Emulator with specified ID not found";

        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k404NotFound);
        addCorsHeaders(resp);
        callback(resp);
        return;
    }

    EmulatorContext* context = emulator->GetContext();
    if (!context)
    {
        Json::Value error;
        error["error"] = "Internal Error";
        error["message"] = "Unable to access emulator context";

        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k500InternalServerError);
        addCorsHeaders(resp);
        callback(resp);
        return;
    }

    CONFIG& config = context->config;
    Memory& memory = *context->pMemory;
    EmulatorState& state = context->emulatorState;
    Json::Value ret;

    // Model
    std::string model = "ZX Spectrum 48K";
    if (config.mem_model == MM_SPECTRUM128)
        model = "ZX Spectrum 128K";
    else if (config.mem_model == MM_PENTAGON)
        model = "Pentagon 128K";
    else if (config.mem_model == MM_PLUS3)
        model = "ZX Spectrum +3";

    ret["model"] = model;

    // Bank mapping
    Json::Value banks;

    Json::Value bank0;
    bank0["address_range"] = "0x0000-0x3FFF";
    if (memory.IsBank0ROM())
    {
        bank0["type"] = "ROM";
        bank0["page"] = static_cast<int>(memory.GetROMPage());
        bank0["read_write"] = "read-only";
    }
    else
    {
        bank0["type"] = "RAM";
        bank0["page"] = static_cast<int>(memory.GetRAMPageForBank0());
        bank0["read_write"] = "read/write";
    }
    banks["bank0"] = bank0;

    Json::Value bank1;
    bank1["address_range"] = "0x4000-0x7FFF";
    bank1["type"] = "RAM";
    bank1["page"] = static_cast<int>(memory.GetRAMPageForBank1());
    bank1["read_write"] = "read/write";
    bank1["contended"] = true;
    bank1["note"] = "Screen 0 location";
    banks["bank1"] = bank1;

    Json::Value bank2;
    bank2["address_range"] = "0x8000-0xBFFF";
    bank2["type"] = "RAM";
    bank2["page"] = static_cast<int>(memory.GetRAMPageForBank2());
    bank2["read_write"] = "read/write";
    bank2["contended"] = false;
    banks["bank2"] = bank2;

    Json::Value bank3;
    bank3["address_range"] = "0xC000-0xFFFF";
    bank3["type"] = "RAM";
    bank3["page"] = static_cast<int>(memory.GetRAMPageForBank3());
    bank3["read_write"] = "read/write";
    bank3["contended"] = false;
    banks["bank3"] = bank3;

    ret["banks"] = banks;

    // Paging control (if applicable)
    if (config.mem_model != MM_SPECTRUM48)
    {
        Json::Value paging;
        paging["port_7ffd_hex"] = StringHelper::Format("0x%02X", state.p7FFD);
        paging["port_7ffd_value"] = static_cast<int>(state.p7FFD);
        paging["bits_0_2_ram"] = static_cast<int>(state.p7FFD & 0x07);
        paging["bit_3_screen"] = (state.p7FFD & 0x08) ? 1 : 0;
        paging["bit_4_rom"] = (state.p7FFD & 0x10) ? 1 : 0;
        paging["bit_5_lock"] = (state.p7FFD & 0x20) ? 1 : 0;
        ret["paging_control"] = paging;
    }

    auto resp = HttpResponse::newHttpJsonResponse(ret);
    addCorsHeaders(resp);
    callback(resp);
}

/// @brief GET /api/v1/emulator/{id}/state/memory/rom
/// @brief Get ROM configuration
void EmulatorAPI::getStateMemoryROM(const HttpRequestPtr& req,
                                    std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                                    const std::string& id) const
{
    auto manager = EmulatorManager::GetInstance();
    auto emulator = manager->GetEmulator(id);

    if (!emulator)
    {
        Json::Value error;
        error["error"] = "Not Found";
        error["message"] = "Emulator with specified ID not found";

        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k404NotFound);
        addCorsHeaders(resp);
        callback(resp);
        return;
    }

    EmulatorContext* context = emulator->GetContext();
    if (!context)
    {
        Json::Value error;
        error["error"] = "Internal Error";
        error["message"] = "Unable to access emulator context";

        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k500InternalServerError);
        addCorsHeaders(resp);
        callback(resp);
        return;
    }

    CONFIG& config = context->config;
    Memory& memory = *context->pMemory;
    EmulatorState& state = context->emulatorState;
    Json::Value ret;

    // Model information
    std::string model = "ZX Spectrum 48K";
    int totalROMPages = 1;
    if (config.mem_model == MM_SPECTRUM128)
    {
        model = "ZX Spectrum 128K";
        totalROMPages = 2;
    }
    else if (config.mem_model == MM_PENTAGON)
    {
        model = "Pentagon 128K";
        totalROMPages = 4;
    }
    else if (config.mem_model == MM_PLUS3)
    {
        model = "ZX Spectrum +3";
        totalROMPages = 4;
    }

    ret["model"] = model;
    ret["total_rom_pages"] = totalROMPages;
    ret["active_rom_page"] = static_cast<int>(memory.GetROMPage());
    ret["rom_size_kb"] = totalROMPages * 16;

    // Available ROM pages
    Json::Value pages = Json::arrayValue;

    if (config.mem_model == MM_SPECTRUM48)
    {
        Json::Value page;
        page["page"] = 0;
        page["description"] = "48K BASIC ROM";
        page["active"] = true;
        pages.append(page);
    }
    else if (config.mem_model == MM_SPECTRUM128)
    {
        Json::Value page0;
        page0["page"] = 0;
        page0["description"] = "128K Editor/Menu ROM";
        page0["active"] = (memory.GetROMPage() == 0);
        pages.append(page0);

        Json::Value page1;
        page1["page"] = 1;
        page1["description"] = "48K BASIC ROM";
        page1["active"] = (memory.GetROMPage() == 1);
        pages.append(page1);
    }
    else if (config.mem_model == MM_PENTAGON)
    {
        Json::Value page0;
        page0["page"] = 0;
        page0["description"] = "Service ROM";
        page0["active"] = (memory.GetROMPage() == 0);
        pages.append(page0);

        Json::Value page1;
        page1["page"] = 1;
        page1["description"] = "TR-DOS ROM";
        page1["active"] = (memory.GetROMPage() == 1);
        pages.append(page1);

        Json::Value page2;
        page2["page"] = 2;
        page2["description"] = "128K Editor/Menu ROM";
        page2["active"] = (memory.GetROMPage() == 2);
        pages.append(page2);

        Json::Value page3;
        page3["page"] = 3;
        page3["description"] = "48K BASIC ROM";
        page3["active"] = (memory.GetROMPage() == 3);
        pages.append(page3);
    }
    else if (config.mem_model == MM_PLUS3)
    {
        Json::Value page0;
        page0["page"] = 0;
        page0["description"] = "+3 Editor ROM";
        page0["active"] = (memory.GetROMPage() == 0);
        pages.append(page0);

        Json::Value page1;
        page1["page"] = 1;
        page1["description"] = "48K BASIC ROM";
        page1["active"] = (memory.GetROMPage() == 1);
        pages.append(page1);

        Json::Value page2;
        page2["page"] = 2;
        page2["description"] = "+3DOS ROM";
        page2["active"] = (memory.GetROMPage() == 2);
        pages.append(page2);

        Json::Value page3;
        page3["page"] = 3;
        page3["description"] = "48K BASIC ROM (copy)";
        page3["active"] = (memory.GetROMPage() == 3);
        pages.append(page3);
    }

    ret["pages"] = pages;

    // Current mapping
    Json::Value mapping;
    if (memory.IsBank0ROM())
    {
        mapping["bank0_type"] = "ROM";
        mapping["bank0_page"] = static_cast<int>(memory.GetROMPage());
        mapping["bank0_access"] = "read-only";
    }
    else
    {
        mapping["bank0_type"] = "RAM";
        mapping["bank0_page"] = static_cast<int>(memory.GetRAMPageForBank0());
        mapping["bank0_access"] = "read/write";
    }
    ret["mapping"] = mapping;

    // Port info (if applicable)
    if (config.mem_model != MM_SPECTRUM48)
    {
        ret["port_7ffd_bit4_rom_select"] = (state.p7FFD & 0x10) ? 1 : 0;
    }

    auto resp = HttpResponse::newHttpJsonResponse(ret);
    addCorsHeaders(resp);
    callback(resp);
}

// Static flag for ROM write protection (default: protected)
static bool s_romWriteProtected = true;

/// @brief GET /api/v1/emulator/{id}/memory/read/{address}
/// @brief Read memory at Z80 address
void EmulatorAPI::readMemory(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback,
                             const std::string& id, const std::string& addressStr) const
{
    auto manager = EmulatorManager::GetInstance();
    auto emulator = manager->GetEmulator(id);

    if (!emulator)
    {
        Json::Value error;
        error["error"] = "Not Found";
        error["message"] = "Emulator with specified ID not found";

        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k404NotFound);
        addCorsHeaders(resp);
        callback(resp);
        return;
    }

    Memory* memory = emulator->GetMemory();
    if (!memory)
    {
        Json::Value error;
        error["error"] = "Internal Error";
        error["message"] = "Memory not available";

        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k500InternalServerError);
        addCorsHeaders(resp);
        callback(resp);
        return;
    }

    // Parse address
    uint16_t address = 0;
    try
    {
        if (addressStr.substr(0, 2) == "0x" || addressStr.substr(0, 2) == "0X")
            address = static_cast<uint16_t>(std::stoul(addressStr, nullptr, 16));
        else
            address = static_cast<uint16_t>(std::stoul(addressStr));
    }
    catch (...)
    {
        Json::Value error;
        error["error"] = "Bad Request";
        error["message"] = "Invalid address format";

        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k400BadRequest);
        addCorsHeaders(resp);
        callback(resp);
        return;
    }

    // Get length from query parameter (default 128)
    uint16_t length = 128;
    auto lengthParam = req->getOptionalParameter<std::string>("length");
    if (lengthParam)
    {
        try { length = static_cast<uint16_t>(std::stoul(*lengthParam)); }
        catch (...) { length = 128; }
    }

    Json::Value ret;
    ret["address"] = StringHelper::Format("0x%04X", address);
    ret["length"] = length;
    
    Json::Value data = Json::arrayValue;
    for (uint16_t i = 0; i < length; i++)
    {
        data.append(memory->DirectReadFromZ80Memory(address + i));
    }
    ret["data"] = data;

    auto resp = HttpResponse::newHttpJsonResponse(ret);
    addCorsHeaders(resp);
    callback(resp);
}

/// @brief POST /api/v1/emulator/{id}/memory/write
/// @brief Write memory at Z80 address (body: {"address": "0x5000", "data": [255, 0, 195]})
void EmulatorAPI::writeMemory(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback,
                              const std::string& id) const
{
    auto manager = EmulatorManager::GetInstance();
    auto emulator = manager->GetEmulator(id);

    if (!emulator)
    {
        Json::Value error;
        error["error"] = "Not Found";
        error["message"] = "Emulator with specified ID not found";

        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k404NotFound);
        addCorsHeaders(resp);
        callback(resp);
        return;
    }

    Memory* memory = emulator->GetMemory();
    if (!memory)
    {
        Json::Value error;
        error["error"] = "Internal Error";
        error["message"] = "Memory not available";

        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k500InternalServerError);
        addCorsHeaders(resp);
        callback(resp);
        return;
    }

    auto body = req->getJsonObject();
    if (!body || !body->isMember("address") || !body->isMember("data"))
    {
        Json::Value error;
        error["error"] = "Bad Request";
        error["message"] = "Request must contain 'address' and 'data' fields";

        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k400BadRequest);
        addCorsHeaders(resp);
        callback(resp);
        return;
    }

    // Parse address
    uint16_t address = 0;
    std::string addressStr = (*body)["address"].asString();
    try
    {
        if (addressStr.substr(0, 2) == "0x" || addressStr.substr(0, 2) == "0X")
            address = static_cast<uint16_t>(std::stoul(addressStr, nullptr, 16));
        else
            address = static_cast<uint16_t>(std::stoul(addressStr));
    }
    catch (...)
    {
        Json::Value error;
        error["error"] = "Bad Request";
        error["message"] = "Invalid address format";

        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k400BadRequest);
        addCorsHeaders(resp);
        callback(resp);
        return;
    }

    // Write data
    Json::Value& data = (*body)["data"];
    size_t bytesWritten = 0;
    for (Json::ArrayIndex i = 0; i < data.size(); i++)
    {
        memory->DirectWriteToZ80Memory(address + i, static_cast<uint8_t>(data[i].asUInt()));
        bytesWritten++;
    }

    Json::Value ret;
    ret["success"] = true;
    ret["address"] = StringHelper::Format("0x%04X", address);
    ret["bytes_written"] = static_cast<Json::UInt>(bytesWritten);

    auto resp = HttpResponse::newHttpJsonResponse(ret);
    addCorsHeaders(resp);
    callback(resp);
}

/// @brief GET /api/v1/emulator/{id}/memory/page/{type}/{page}
/// @brief Read from specific RAM/ROM page
void EmulatorAPI::readPage(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback,
                           const std::string& id, const std::string& type, const std::string& pageStr) const
{
    auto manager = EmulatorManager::GetInstance();
    auto emulator = manager->GetEmulator(id);

    if (!emulator)
    {
        Json::Value error;
        error["error"] = "Not Found";
        error["message"] = "Emulator with specified ID not found";

        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k404NotFound);
        addCorsHeaders(resp);
        callback(resp);
        return;
    }

    Memory* memory = emulator->GetMemory();
    if (!memory)
    {
        Json::Value error;
        error["error"] = "Internal Error";
        error["message"] = "Memory not available";

        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k500InternalServerError);
        addCorsHeaders(resp);
        callback(resp);
        return;
    }

    bool isROM = (type == "rom");
    bool isRAM = (type == "ram");
    if (!isROM && !isRAM)
    {
        Json::Value error;
        error["error"] = "Bad Request";
        error["message"] = "Type must be 'ram' or 'rom'";

        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k400BadRequest);
        addCorsHeaders(resp);
        callback(resp);
        return;
    }

    uint8_t page = static_cast<uint8_t>(std::stoul(pageStr));
    uint16_t offset = 0;
    uint16_t length = 128;

    auto offsetParam = req->getOptionalParameter<std::string>("offset");
    if (offsetParam) try { offset = static_cast<uint16_t>(std::stoul(*offsetParam)); } catch (...) {}
    
    auto lengthParam = req->getOptionalParameter<std::string>("length");
    if (lengthParam) try { length = static_cast<uint16_t>(std::stoul(*lengthParam)); } catch (...) {}

    uint8_t* pagePtr = isRAM ? memory->RAMPageAddress(page) : memory->ROMPageHostAddress(page);
    if (!pagePtr)
    {
        Json::Value error;
        error["error"] = "Bad Request";
        error["message"] = "Invalid page number";

        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k400BadRequest);
        addCorsHeaders(resp);
        callback(resp);
        return;
    }

    Json::Value ret;
    ret["type"] = type;
    ret["page"] = page;
    ret["offset"] = StringHelper::Format("0x%04X", offset);
    ret["length"] = length;
    
    Json::Value data = Json::arrayValue;
    for (uint16_t i = 0; i < length && (offset + i) < PAGE_SIZE; i++)
    {
        data.append(pagePtr[offset + i]);
    }
    ret["data"] = data;

    auto resp = HttpResponse::newHttpJsonResponse(ret);
    addCorsHeaders(resp);
    callback(resp);
}

/// @brief POST /api/v1/emulator/{id}/memory/page/{type}/{page}
/// @brief Write to specific RAM/ROM page (body: {"offset": "0x0000", "data": [255, 0]})
void EmulatorAPI::writePage(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback,
                            const std::string& id, const std::string& type, const std::string& pageStr) const
{
    auto manager = EmulatorManager::GetInstance();
    auto emulator = manager->GetEmulator(id);

    if (!emulator)
    {
        Json::Value error;
        error["error"] = "Not Found";
        error["message"] = "Emulator with specified ID not found";

        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k404NotFound);
        addCorsHeaders(resp);
        callback(resp);
        return;
    }

    Memory* memory = emulator->GetMemory();
    bool isROM = (type == "rom");
    bool isRAM = (type == "ram");

    if (isROM && s_romWriteProtected)
    {
        Json::Value error;
        error["error"] = "Forbidden";
        error["message"] = "ROM write protected. Use PUT /memory/rom/protect with {\"protected\": false} to enable writes.";

        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k403Forbidden);
        addCorsHeaders(resp);
        callback(resp);
        return;
    }

    auto body = req->getJsonObject();
    if (!body || !body->isMember("offset") || !body->isMember("data"))
    {
        Json::Value error;
        error["error"] = "Bad Request";
        error["message"] = "Request must contain 'offset' and 'data' fields";

        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k400BadRequest);
        addCorsHeaders(resp);
        callback(resp);
        return;
    }

    uint8_t page = static_cast<uint8_t>(std::stoul(pageStr));
    uint8_t* pagePtr = isRAM ? memory->RAMPageAddress(page) : memory->ROMPageHostAddress(page);

    uint16_t offset = 0;
    std::string offsetStr = (*body)["offset"].asString();
    try
    {
        if (offsetStr.substr(0, 2) == "0x") offset = static_cast<uint16_t>(std::stoul(offsetStr, nullptr, 16));
        else offset = static_cast<uint16_t>(std::stoul(offsetStr));
    } catch (...) {}

    Json::Value& data = (*body)["data"];
    size_t bytesWritten = 0;
    for (Json::ArrayIndex i = 0; i < data.size() && (offset + i) < PAGE_SIZE; i++)
    {
        pagePtr[offset + i] = static_cast<uint8_t>(data[i].asUInt());
        bytesWritten++;
    }

    Json::Value ret;
    ret["success"] = true;
    ret["type"] = type;
    ret["page"] = page;
    ret["offset"] = StringHelper::Format("0x%04X", offset);
    ret["bytes_written"] = static_cast<Json::UInt>(bytesWritten);

    auto resp = HttpResponse::newHttpJsonResponse(ret);
    addCorsHeaders(resp);
    callback(resp);
}

/// @brief GET /api/v1/emulator/{id}/memory/rom/protect
/// @brief Get ROM write protection status
void EmulatorAPI::getROMProtect(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback,
                                const std::string& id) const
{
    Json::Value ret;
    ret["protected"] = s_romWriteProtected;
    ret["message"] = s_romWriteProtected ? "ROM pages are write-protected" : "ROM pages are writable";

    auto resp = HttpResponse::newHttpJsonResponse(ret);
    addCorsHeaders(resp);
    callback(resp);
}

/// @brief PUT/POST /api/v1/emulator/{id}/memory/rom/protect
/// @brief Set ROM write protection (body: {"protected": true/false})
void EmulatorAPI::setROMProtect(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback,
                                const std::string& id) const
{
    auto body = req->getJsonObject();
    if (!body || !body->isMember("protected"))
    {
        Json::Value error;
        error["error"] = "Bad Request";
        error["message"] = "Request must contain 'protected' field (true/false)";

        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k400BadRequest);
        addCorsHeaders(resp);
        callback(resp);
        return;
    }

    s_romWriteProtected = (*body)["protected"].asBool();

    Json::Value ret;
    ret["success"] = true;
    ret["protected"] = s_romWriteProtected;
    ret["message"] = s_romWriteProtected ? "ROM write protection enabled" : "ROM write protection disabled - ROM pages are now writable";

    auto resp = HttpResponse::newHttpJsonResponse(ret);
    addCorsHeaders(resp);
    callback(resp);
}

} // namespace v1
} // namespace api
