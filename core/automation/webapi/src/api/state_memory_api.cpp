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

} // namespace v1
} // namespace api
