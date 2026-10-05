// WebAPI State Memory Inspection Implementation
// Extracted from emulator_api.cpp - 2026-01-08

#include "debugger/memory/memoryread.h"
#include "debugger/search/memorysearch.h"
#include "debugger/breakpoints/breakpointmanager.h"
#include "../common/binaryresponse.h"
#include "../common/jsonnumber.h"
#include "../emulator_api.h"
#include "../common/statenode_json.h"

#include <drogon/HttpResponse.h>
#include <debugger/ttd/timetravelmanager.h>  // TimeTravelManager (Item 6 markers)
#include <emulator/config.h>
#include <emulator/emulator.h>
#include <emulator/emulatormanager.h>
#include <emulator/emulatorcontext.h>
#include <emulator/platform.h>
#include <emulator/ports/models/profiboard.h>
#include <emulator/memory/devicememory.h>  // device memory regions (/memory/page/vram/{n})
#include <emulator/memory/memorymap.h>  // TD-3 compact read formats
#include <emulator/memory/rom.h>  // ROM signatures
#include <emulator/cpu/core.h>    // Core::GetROM()
#include <emulator/ports/portdecoder.h>  // Tagged port registry
#include <emulator/state/devicestate.h>  // DeviceState::SprinterPaging
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

/// Shared renderer for TD-3 Phase 1 compact memory reads: builds the
/// format-specific payload of a memory window so /memory/read/{address},
/// /memory/{addr} and /memory/page render identical shapes.
///   format="hexdump" (default): {format, hexdump} - 16B/line + ASCII sidebar
///   format="full":              {format, data[], hex} - legacy JSON array
///   format="sparse":            {format, non_zero, segments[]} - fill runs
/// Callers keep their own address/length field rendering.
Json::Value RenderMemoryWindowFormat(const uint8_t* data, size_t size, uint32_t address, const std::string& format)
{
    Json::Value ret;
    ret["format"] = format;

    if (format == "sparse")
    {
        ret["non_zero"] = CountNonZeroBytes(data, size);
        Json::Value segments(Json::arrayValue);
        for (const MemorySparseSegment& segment : BuildSparseSegments(data, size))
        {
            Json::Value item;
            item["offset"] = segment.offset;
            item["length"] = segment.length;
            item["is_fill"] = segment.isFill;
            if (segment.isFill)
                item["fill"] = StringHelper::Format("0x%02X", segment.fill);
            else
                item["hex"] = segment.hex;
            segments.append(item);
        }
        ret["segments"] = segments;
    }
    else if (format == "full")
    {
        Json::Value dataJson(Json::arrayValue);
        for (size_t i = 0; i < size; i++)
            dataJson.append(data[i]);
        ret["data"] = dataJson;

        std::string hexStr;
        hexStr.reserve(size * 3);
        for (size_t i = 0; i < size; i++)
        {
            if (i > 0) hexStr += ' ';
            hexStr += StringHelper::Format("%02X", data[i]);
        }
        ret["hex"] = hexStr;
    }
    else // "hexdump" (default)
    {
        ret["hexdump"] = FormatHexDump(data, size, address);
    }

    return ret;
}

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
    std::string model = Config::GetModelFullName(config.mem_model);
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

    // Sprinter: #7FFD / #1FFD and the windows live in the PLD (DeviceState::SprinterPaging)
    if (config.mem_model == MM_SPRINTER)
    {
        ret["paging"] = StateNodeToJson(DeviceState::SprinterPaging(context));
    }
    // Paging state (if applicable)
    else if (config.mem_model != MM_SPECTRUM48)
    {
        Json::Value paging;
        paging["port_7ffd"] = static_cast<int>(state.p7FFD);
        paging["port_7ffd_hex"] = StringHelper::Format("0x%02X", state.p7FFD);
        paging["ram_bank_3"] = static_cast<int>(state.p7FFD & 0x07);
        paging["screen"] = (state.p7FFD & 0x08) ? 1 : 0;
        paging["rom_select"] = (state.p7FFD & 0x10) ? 1 : 0;
        paging["locked"] = (state.p7FFD & 0x20) ? true : false;

        // Extended paging ports (model-specific)
        // pEFF7: Pentagon/Scorpion extended features
        paging["port_eff7"] = static_cast<int>(state.pEFF7);
        paging["port_eff7_hex"] = StringHelper::Format("0x%02X", state.pEFF7);

        // EFF7 flag interpretation for Pentagon models
        if (config.mem_model == MM_PENTAGON && state.pEFF7 != 0)
        {
            Json::Value eff7_flags;
            eff7_flags["16col_enabled"] = (state.pEFF7 & EFF7_4BPP) != 0;
            eff7_flags["512_enabled"] = (state.pEFF7 & EFF7_512) != 0;
            eff7_flags["extmem_locked"] = (state.pEFF7 & EFF7_LOCKMEM) != 0;
            eff7_flags["gigascreen_enabled"] = (state.pEFF7 & EFF7_GIGASCREEN) != 0;
            eff7_flags["hwmc_enabled"] = (state.pEFF7 & EFF7_HWMC) != 0;
            eff7_flags["384_enabled"] = (state.pEFF7 & EFF7_384) != 0;
            eff7_flags["cmos_enabled"] = (state.pEFF7 & EFF7_CMOS) != 0;
            paging["eff7_flags"] = eff7_flags;
        }

        // pDFFD: Profi extended paging / video mode latch (both boards)
        if (IsProfiModel(config.mem_model))
        {
            paging["profi_board"] = (config.mem_model == MM_PROFI3) ? "v3" : "v5";
            paging["profi_sync_prom"] = ProfiSyncPromName(
                ProfiResolveSyncProm(static_cast<ProfiSyncProm>(config.profi_sync_prom), config.mem_model));
            // The keyboard on the connector: matrix, xt (the PROFI-XT firmware), xttable (its key table)
            paging["profi_keyboard"] = ProfiKeyboardName(ProfiKeyboardInForce(context));
            // The hi-res clocks (design-hires.md): the CPU clock there (no turbo), the v5's ZQ3 and SB7
            paging["profi_hires_cpu_hz"] = ProfiHiresCpuHz(config.mem_model == MM_PROFI, config.profi_zq3_mhz);
            paging["profi_zq3_mhz"] = static_cast<int>(ProfiClampZq3(config.profi_zq3_mhz));
            paging["profi_ay_clock"] = (config.mem_model == MM_PROFI && config.profi_ay_clock_new) ? "new" : "old";
            paging["port_dffd"] = static_cast<int>(state.pDFFD);
            paging["port_dffd_hex"] = StringHelper::Format("0x%02X", state.pDFFD);

            Json::Value dffdFlags;
            dffdFlags["extended_ram_bank"] = static_cast<int>(state.pDFFD & 0x07);
            dffdFlags["sco"] = (state.pDFFD & 0x08) != 0;
            dffdFlags["worom"] = (state.pDFFD & 0x10) != 0;
            dffdFlags["cpm"] = (state.pDFFD & 0x20) != 0;
            dffdFlags["scr"] = (state.pDFFD & 0x40) != 0;
            dffdFlags["video_512x240"] = (state.pDFFD & 0x80) != 0;
            paging["dffd_flags"] = dffdFlags;
        }

        // pFE: Border/tape/speaker (always available)
        paging["port_fe"] = static_cast<int>(state.pFE);
        paging["port_fe_hex"] = StringHelper::Format("0x%02X", state.pFE);

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
    std::string model = Config::GetModelFullName(config.mem_model);
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
        bank0["read_write"] = memory.IsWindowWritable(0) ? "read/write" : "read-only";
    }
    banks["bank0"] = bank0;

    Json::Value bank1;
    bank1["address_range"] = "0x4000-0x7FFF";
    bank1["type"] = "RAM";
    bank1["page"] = static_cast<int>(memory.GetRAMPageForBank1());
    bank1["read_write"] = memory.IsWindowWritable(1) ? "read/write" : "read-only";
    bank1["note"] = "Screen 0 location";
    banks["bank1"] = bank1;

    Json::Value bank2;
    bank2["address_range"] = "0x8000-0xBFFF";
    bank2["type"] = "RAM";
    bank2["page"] = static_cast<int>(memory.GetRAMPageForBank2());
    bank2["read_write"] = memory.IsWindowWritable(2) ? "read/write" : "read-only";
    banks["bank2"] = bank2;

    Json::Value bank3;
    bank3["address_range"] = "0xC000-0xFFFF";
    bank3["type"] = "RAM";
    bank3["page"] = static_cast<int>(memory.GetRAMPageForBank3());
    bank3["read_write"] = memory.IsWindowWritable(3) ? "read/write" : "read-only";
    banks["bank3"] = bank3;

    // Contended: the CPU waits for the video logic there (Core::IsSlotContended)
    const char* const bankKeys[4] = { "bank0", "bank1", "bank2", "bank3" };
    for (uint8_t slot = 0; slot < 4; slot++)
        banks[bankKeys[slot]]["contended"] = context->pCore && context->pCore->IsSlotContended(slot);

    ret["banks"] = banks;

    // Sprinter: the window kinds (fast RAM, vROM, graphics, ISA) and the PLD latches
    if (config.mem_model == MM_SPRINTER)
    {
        ret["sprinter"] = StateNodeToJson(DeviceState::SprinterPaging(context));
    }
    // Paging control (if applicable)
    else if (config.mem_model != MM_SPECTRUM48)
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
/// @brief Get ROM configuration with signatures
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
    ROM* rom = context->pCore ? context->pCore->GetROM() : nullptr;
    Json::Value ret;

    // Model information
    std::string model = Config::GetModelFullName(config.mem_model);
    int totalROMPages = 1;
    switch (config.mem_model)
    {
        case MM_SPECTRUM128:
        case MM_PLUS2:
            totalROMPages = 2;
            break;
        case MM_PENTAGON:
        case MM_PLUS2A:
        case MM_PLUS3:
        case MM_SCORP:
        case MM_PROFSCORP:
        case MM_ATM3:
        case MM_ATM710:
        case MM_ATM450:
        case MM_PROFI:
        case MM_PROFI3:
            totalROMPages = 4;
            break;
        case MM_SPRINTER:
            totalROMPages = 16;  // the 256 KB flash (Sprinter bios-versions.md §2)
            break;
        default:
            totalROMPages = 1;
            break;
    }

    // Machines whose ROM image holds more pages than the four standard slots
    // (ZX-Evo 512 KB = 32 pages) report what is actually loaded
    if (rom && rom->GetROMBanksLoaded() > totalROMPages)
        totalROMPages = rom->GetROMBanksLoaded();

    ret["model"] = model;
    ret["total_rom_pages"] = totalROMPages;
    ret["active_rom_page"] = static_cast<int>(memory.GetROMPage());
    ret["rom_size_kb"] = totalROMPages * 16;

    // ROM file info
    if (rom)
    {
        ret["rom_file"] = rom->GetROMFilename();
    }

    // Available ROM pages with signatures
    Json::Value pages = Json::arrayValue;

    // Helper lambda to add page info with signature
    auto addPageInfo = [&](int pageNum, const std::string& description, bool isActive) {
        Json::Value pageInfo;
        pageInfo["page"] = pageNum;
        pageInfo["description"] = description;
        pageInfo["active"] = isActive;
        pageInfo["size_kb"] = 16;

        // Calculate signature for this ROM page
        uint8_t* pagePtr = memory.ROMPageHostAddress(pageNum);
        if (pagePtr && rom)
        {
            std::string signature = rom->CalculateSignature(pagePtr, 0x4000);
            pageInfo["signature"] = signature;
            std::string title = rom->GetROMTitle(signature);
            pageInfo["title"] = title.empty() ? "Unknown ROM" : title;
        }

        pages.append(pageInfo);
    };

    uint8_t activeROMPage = memory.GetROMPage();

    // Page roles come from the core single-source layout table
    // (ROM::GetROMPageRole - same names on /state/paging, CLI, Lua, Python)
    for (int i = 0; i < totalROMPages; i++)
    {
        std::string role = rom ? rom->GetROMPageRole(static_cast<uint8_t>(i))
                               : StringHelper::Format("ROM Page %d", i);
        addPageInfo(i, role, activeROMPage == i);
    }

    ret["pages"] = pages;

    // Current mapping
    Json::Value mapping;
    if (memory.IsBank0ROM())
    {
        mapping["bank0_type"] = "ROM";
        mapping["bank0_page"] = static_cast<int>(memory.GetROMPage());
        mapping["bank0_access"] = "read-only";

        // Add title for currently mapped ROM
        uint8_t* activePagePtr = memory.ROMPageHostAddress(memory.GetROMPage());
        if (activePagePtr && rom)
        {
            std::string sig = rom->CalculateSignature(activePagePtr, 0x4000);
            std::string title = rom->GetROMTitle(sig);
            mapping["bank0_rom_title"] = title.empty() ? "Unknown ROM" : title;
            mapping["bank0_rom_signature"] = sig;
        }
    }
    else
    {
        mapping["bank0_type"] = "RAM";
        mapping["bank0_page"] = static_cast<int>(memory.GetRAMPageForBank0());
        mapping["bank0_access"] = memory.IsWindowWritable(0) ? "read/write" : "read-only";
    }
    ret["mapping"] = mapping;

    // Port info (if applicable; the Sprinter's #7FFD is in the PLD: /state/sprinter)
    if (config.mem_model != MM_SPECTRUM48 && config.mem_model != MM_SPRINTER)
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
    // Up to the whole 64K (a 16-bit length read 65536 as 0 before; larger values wrapped)
    uint32_t length = 128;
    auto lengthParam = req->getOptionalParameter<std::string>("length");
    if (lengthParam)
    {
        try { length = static_cast<uint32_t>(std::min<unsigned long>(std::stoul(*lengthParam), MemoryRead::kMaxLength)); }
        catch (...) { length = 128; }
    }

    // TD-3 Phase 1 compact read formats: hexdump (default) | full | sparse
    // (filter=sparse is accepted as an alias for format=sparse)
    std::string format = "hexdump";
    auto formatParam = req->getOptionalParameter<std::string>("format");
    auto filterParam = req->getOptionalParameter<std::string>("filter");
    if (formatParam && !formatParam->empty()) format = *formatParam;
    else if (filterParam && *filterParam == "sparse") format = "sparse";
    if (format == "binary")
    {
        const MemoryRead::Result read = MemoryRead::Bytes(emulator->GetContext(), "cpu", address, std::max<uint32_t>(length, 1));
        if (!read.error.empty())
        {
            Json::Value error;
            error["error"] = "Bad Request";
            error["message"] = read.error;
            auto resp = HttpResponse::newHttpJsonResponse(error);
            resp->setStatusCode(HttpStatusCode::k400BadRequest);
            addCorsHeaders(resp);
            callback(resp);
            return;
        }
        callback(BinaryMemoryResponse(read.space, read.address, read.bytes));
        return;
    }
    if (format != "hexdump" && format != "full" && format != "sparse")
    {
        Json::Value error;
        error["error"] = "Bad Request";
        error["message"] = "Invalid format parameter (expected 'hexdump', 'full', 'sparse' or 'binary')";

        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k400BadRequest);
        addCorsHeaders(resp);
        callback(resp);
        return;
    }

    std::vector<uint8_t> buffer(length);
    for (uint32_t i = 0; i < length; i++)
        buffer[i] = memory->DirectReadFromZ80Memory(static_cast<uint16_t>(address + i));

    Json::Value ret;
    ret["address"] = StringHelper::Format("0x%04X", address);
    ret["length"] = length;
    const Json::Value payload = RenderMemoryWindowFormat(buffer.data(), buffer.size(), address, format);
    for (const std::string& name : payload.getMemberNames())
        ret[name] = payload[name];

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

    // Phase 2 Item 6 — record a debugger-edit marker before the write
    // executes. One marker per API call, regardless of byte count. The
    // marker is a no-op unless a TTD session is Recording.
    EmulatorContext* ctx = emulator->GetContext();

    // Thread safety: DirectWriteToZ80Memory now mirrors MemoryWriteDebug's
    // call to TTDDirtyTracker::MarkDirty when TTD is enabled. The dirty
    // bitmap is documented as emulator-thread-only (ttddirtytracker.h),
    // so we must pause the Z80 thread before writing when recording is
    // active. The cost is one paused frame boundary (~20 ms worst case);
    // a no-op when no session is recording.
    const bool ttdRecording = ctx && ctx->pTimeTravelHooks
                              && ctx->pTimeTravelHooks->IsRecording();
    const bool wasRunning = ttdRecording && emulator->IsRunning() && !emulator->IsPaused();
    if (wasRunning)
    {
        emulator->Pause(false);
        emulator->WaitForPauseConfirmation(1000);
    }

    if (ctx && ctx->pTimeTravelHooks)
        ctx->pTimeTravelHooks->RecordExternalEvent(
            ttd::TTDExternalEventKind::DebuggerEdit, "WebAPI memory write");

    for (Json::ArrayIndex i = 0; i < data.size(); i++)
    {
        memory->DirectWriteToZ80Memory(address + i, static_cast<uint8_t>(data[i].asUInt()));
        bytesWritten++;
    }

    // Resume if we paused. Use broadcast=false to avoid spurious UI flicker —
    // the caller did not ask to pause, and the framebuffer did not change
    // in a way the periodic refresh won't pick up.
    if (wasRunning)
        emulator->Resume(false);

    Json::Value ret;
    ret["success"] = true;
    ret["address"] = StringHelper::Format("0x%04X", address);
    ret["bytes_written"] = static_cast<Json::UInt>(bytesWritten);

    auto resp = HttpResponse::newHttpJsonResponse(ret);
    addCorsHeaders(resp);
    callback(resp);
}

namespace
{


} // namespace

/// @brief POST /api/v1/emulator/{id}/memory/find
/// @brief Search Z80 memory for a byte pattern
/// @brief Request body: {"pattern_hex": "AF 32 0E" | "pattern": [175, 50, 14],
/// @brief                  "start": 0, "end": 65535, "max": 64, "alignment": 1}
void EmulatorAPI::findMemory(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback,
                             const std::string& id) const
{
    auto badRequest = [&callback](const std::string& message) {
        Json::Value error;
        error["error"] = "Bad Request";
        error["message"] = message;
        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k400BadRequest);
        addCorsHeaders(resp);
        callback(resp);
    };

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

    auto body = req->getJsonObject();
    if (!body || (!body->isMember("pattern_hex") && !body->isMember("pattern")))
    {
        badRequest("Request must contain 'pattern_hex' (hex string, ?? for any byte) or 'pattern' (byte array)");
        return;
    }

    // MemorySearch (core): the pattern and its mask, the space, the range
    MemorySearchRequest request;
    std::string error;
    if (body->isMember("pattern_hex"))
    {
        if (!MemorySearch::ParsePattern((*body)["pattern_hex"].asString(), request.pattern, request.mask, error))
        {
            badRequest("Invalid 'pattern_hex': " + error);
            return;
        }
    }
    else
    {
        const Json::Value& array = (*body)["pattern"];
        if (!array.isArray() || array.size() == 0)
        {
            badRequest("'pattern' must be a non-empty byte array");
            return;
        }
        for (Json::ArrayIndex i = 0; i < array.size(); i++)
            request.pattern.push_back(static_cast<uint8_t>(array[i].asUInt()));
    }
    // An explicit mask (1 bits must match) replaces the one the wildcards made
    if (body->isMember("mask_hex") || body->isMember("mask"))
    {
        std::vector<uint8_t> mask, unused;
        if (body->isMember("mask_hex"))
        {
            if (!MemorySearch::ParsePattern((*body)["mask_hex"].asString(), mask, unused, error))
            {
                badRequest("Invalid 'mask_hex': " + error);
                return;
            }
        }
        else
            for (const Json::Value& v : (*body)["mask"])
                mask.push_back(static_cast<uint8_t>(v.asUInt()));
        if (mask.size() != request.pattern.size())
        {
            badRequest("the mask must be as long as the pattern");
            return;
        }
        request.mask = mask;
    }
    if (body->isMember("space") && !MemorySearch::ParseSpace((*body)["space"].asString(), request, error))
    {
        badRequest(error);
        return;
    }
    uint32_t start = 0, end = 0xFFFFFFFF;
    if ((body->isMember("start") && !ParseJsonUInt((*body)["start"], 0xFFFFFFFFu, start)) ||
        (body->isMember("end") && !ParseJsonUInt((*body)["end"], 0xFFFFFFFFu, end)))
    {
        badRequest("'start' / 'end' must be numbers (decimal, \"0x..\", \"#..\" or \"$..\")");
        return;
    }
    if (start > end)
    {
        badRequest("Invalid 'start'/'end' range");
        return;
    }
    request.start = start;
    request.end = end;
    request.max = body->isMember("max") ? (*body)["max"].asUInt() : 64u;
    request.alignment = body->isMember("alignment") ? (*body)["alignment"].asUInt() : 1u;

    const MemorySearchResult result = MemorySearch::Search(emulator->GetContext(), request);
    if (!result.error.empty())
    {
        badRequest(result.error);
        return;
    }

    Json::Value matches(Json::arrayValue);
    for (const MemorySearchMatch& m : result.matches)
    {
        Json::Value match;
        if (m.page < 0)
            match["address"] = StringHelper::Format("0x%04X", m.address);
        else
        {
            match["page"]["kind"] = BreakpointManager::PageKindName(static_cast<MemoryBankModeEnum>(m.pageType));
            match["page"]["page"] = m.page;
            match["offset"] = StringHelper::Format("0x%04X", m.address);
        }
        match["context_start"] = StringHelper::Format("0x%04X", m.contextStart);   // 4 bytes before, the match, 4 after
        Json::Value context(Json::arrayValue);
        for (uint8_t byte : m.bytes)
            context.append(byte);
        match["context"] = context;
        matches.append(match);
    }

    Json::Value ret;
    ret["success"] = true;
    ret["space"] = MemorySearch::SpaceName(request);
    ret["count"] = static_cast<Json::UInt>(result.matches.size());
    ret["matches"] = matches;
    ret["truncated"] = result.truncated;
    ret["range"] = StringHelper::Format("0x%04X-0x%04X", start, end == 0xFFFFFFFF ? (request.space == MemorySearchRequest::Space::Cpu ? 0xFFFFu : 0xFFFFFFFFu) : end);

    auto resp = HttpResponse::newHttpJsonResponse(ret);
    addCorsHeaders(resp);
    callback(resp);
}

namespace
{
/// /memory/page/{region}/{n}: page n of a device memory region (DeviceMemory, page size from the region).
/// False when `type` names no region (the caller answers with its own error); true once answered
bool ReadRegionPage(const HttpRequestPtr& req, EmulatorContext* context, const std::string& type,
                    const std::string& pageStr, std::function<void(const HttpResponsePtr&)>& callback)
{
    IDeviceMemoryRegion* region = DeviceMemory::Find(context, type);
    if (!region)
        return false;
    auto reply = [&](const Json::Value& body, HttpStatusCode code) {
        auto resp = HttpResponse::newHttpJsonResponse(body);
        resp->setStatusCode(code);
        addCorsHeaders(resp);
        callback(resp);
    };
    uint64_t page = 0, offset = 0, length = 128;
    const uint32_t pages = region->Size() / region->PageSize();
    auto offsetParam = req->getOptionalParameter<std::string>("offset");
    auto lengthParam = req->getOptionalParameter<std::string>("length");
    if (!StringHelper::TryParseUInt64(pageStr, page) || page >= pages ||
        (offsetParam && (!StringHelper::TryParseUInt64(*offsetParam, offset) || offset >= region->PageSize())) ||
        (lengthParam && (!StringHelper::TryParseUInt64(*lengthParam, length) || length < 1)))
    {
        Json::Value error;
        error["error"] = "Bad Request";
        error["message"] = "page 0-" + std::to_string(pages - 1) + ", offset 0-" + std::to_string(region->PageSize() - 1) +
                           ", length >= 1";
        reply(error, HttpStatusCode::k400BadRequest);
        return true;
    }
    if (offset + length > region->PageSize())
        length = region->PageSize() - offset;
    const bool sparse = req->getOptionalParameter<std::string>("filter").value_or("") == "sparse";
    const uint32_t start = static_cast<uint32_t>(page * region->PageSize() + offset);
    if (req->getOptionalParameter<std::string>("format").value_or("") == "binary")
    {
        std::vector<uint8_t> bytes;
        std::string error;
        if (!DeviceMemory::Read(context, region->Name(), start, static_cast<uint32_t>(length), bytes, error))
        {
            Json::Value body;
            body["error"] = "Bad Request";
            body["message"] = error;
            reply(body, HttpStatusCode::k400BadRequest);
            return true;
        }
        callback(BinaryMemoryResponse(type + std::to_string(page), static_cast<uint32_t>(offset), bytes));
        return true;
    }
    Json::Value ret = StateNodeToJson(
        DeviceState::MemoryRegionRead(context, region->Name(), start, static_cast<uint32_t>(length), sparse ? "sparse" : "data"));
    ret["type"] = type;
    ret["page"] = static_cast<Json::UInt64>(page);
    ret["offset"] = StringHelper::Format("0x%04X", static_cast<unsigned>(offset));
    ret["region_offset"] = StringHelper::Format("0x%05X", start);
    ret.removeMember("available");
    reply(ret, HttpStatusCode::k200OK);
    return true;
}

bool WriteRegionPage(const HttpRequestPtr& req, EmulatorContext* context, const std::string& type,
                     const std::string& pageStr, std::function<void(const HttpResponsePtr&)>& callback)
{
    IDeviceMemoryRegion* region = DeviceMemory::Find(context, type);
    if (!region)
        return false;
    auto reply = [&](const Json::Value& body, HttpStatusCode code) {
        auto resp = HttpResponse::newHttpJsonResponse(body);
        resp->setStatusCode(code);
        addCorsHeaders(resp);
        callback(resp);
    };
    auto fail = [&](const std::string& message) {
        Json::Value error;
        error["error"] = "Bad Request";
        error["message"] = message;
        reply(error, HttpStatusCode::k400BadRequest);
        return true;
    };
    auto body = req->getJsonObject();
    uint64_t page = 0;
    uint32_t offset = 0;
    if (!body || !body->isMember("offset") || !body->isMember("data") || !(*body)["data"].isArray())
        return fail("Request must contain 'offset' and 'data' fields");
    if (!StringHelper::TryParseUInt64(pageStr, page) || page >= region->Size() / region->PageSize())
        return fail("Invalid page number '" + pageStr + "'");
    const Json::Value& offsetValue = (*body)["offset"];
    if (!(offsetValue.isUInt() ? (offset = offsetValue.asUInt(), true) : DeviceMemory::ParseNumber(offsetValue.asString(), offset)) ||
        offset >= region->PageSize())
        return fail("Invalid offset (0-" + std::to_string(region->PageSize() - 1) + ")");
    std::vector<uint8_t> bytes;
    for (const Json::Value& v : (*body)["data"])
    {
        if (offset + bytes.size() >= region->PageSize())
            break;
        bytes.push_back(static_cast<uint8_t>(v.asUInt()));
    }
    std::string error;
    if (!DeviceMemory::Write(context, region->Name(), static_cast<uint32_t>(page * region->PageSize() + offset), bytes,
                             "WebAPI page write", error))
        return fail(error);
    Json::Value ret;
    ret["success"] = true;
    ret["type"] = type;
    ret["page"] = static_cast<Json::UInt64>(page);
    ret["offset"] = StringHelper::Format("0x%04X", offset);
    ret["bytes_written"] = static_cast<Json::UInt64>(bytes.size());
    reply(ret, HttpStatusCode::k200OK);
    return true;
}
}  // namespace

/// @brief GET /api/v1/emulator/{id}/memory/page/{type}/{page}
/// @brief Read from specific RAM/ROM page (or a page of a device memory region: /memory/page/vram/{n})
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
    if (!isROM && !isRAM && ReadRegionPage(req, emulator->GetContext(), type, pageStr, callback))
        return;  // a device memory region by name (/memory/page/vram/{n}: the Sprinter's video RAM)
    if (!isROM && !isRAM)
    {
        Json::Value error;
        error["error"] = "Bad Request";
        error["message"] = "Type must be 'ram', 'rom' or a device memory region (GET /memory/regions)";

        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k400BadRequest);
        addCorsHeaders(resp);
        callback(resp);
        return;
    }

    // Strict index validation: std::stoul() here threw on garbage input and
    // silently truncated page numbers above 255 (page "300" became page 44)
    uint64_t page = 0;
    uint64_t offset = 0;
    uint64_t length = 128;

    if (!StringHelper::TryParseUInt64(pageStr, page))
    {
        Json::Value error;
        error["error"] = "Bad Request";
        error["message"] = "Invalid page number '" + pageStr + "' (expected unsigned decimal)";

        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k400BadRequest);
        addCorsHeaders(resp);
        callback(resp);
        return;
    }

    auto offsetParam = req->getOptionalParameter<std::string>("offset");
    if (offsetParam && (!StringHelper::TryParseUInt64(*offsetParam, offset) || offset >= PAGE_SIZE))
    {
        Json::Value error;
        error["error"] = "Bad Request";
        error["message"] = "Invalid offset '" + *offsetParam + "' (expected 0-" + std::to_string(PAGE_SIZE - 1) + ")";

        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k400BadRequest);
        addCorsHeaders(resp);
        callback(resp);
        return;
    }

    auto lengthParam = req->getOptionalParameter<std::string>("length");
    if (lengthParam && (!StringHelper::TryParseUInt64(*lengthParam, length) || length < 1))
    {
        Json::Value error;
        error["error"] = "Bad Request";
        error["message"] = "Invalid length '" + *lengthParam + "' (expected unsigned decimal >= 1)";

        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k400BadRequest);
        addCorsHeaders(resp);
        callback(resp);
        return;
    }

    const uint64_t maxPage = isRAM ? MAX_RAM_PAGES - 1 : MAX_ROM_PAGES - 1;
    if (page > maxPage)
    {
        Json::Value error;
        error["error"] = "Bad Request";
        error["message"] = "Invalid page number '" + pageStr + "' (expected 0-" + std::to_string(maxPage) + ")";

        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k400BadRequest);
        addCorsHeaders(resp);
        callback(resp);
        return;
    }

    uint8_t* pagePtr = isRAM ? memory->RAMPageAddress(static_cast<uint16_t>(page)) : memory->ROMPageHostAddress(static_cast<uint8_t>(page));
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

    // TD-3 Phase 1: filter=sparse compresses 0x00/0xFF runs into fill
    // segments (a full 16K page as a JSON array is the G-6 token-poisoning
    // case); the default response keeps the legacy data array
    const bool sparse = req->getOptionalParameter<std::string>("filter").value_or("") == "sparse";

    uint32_t windowSize = static_cast<uint32_t>(length);
    if (offset + length > PAGE_SIZE)
        windowSize = PAGE_SIZE - offset;
    const uint8_t* window = pagePtr + offset;

    // format=binary: the window raw (without it the JSON answer stays as it was)
    if (req->getOptionalParameter<std::string>("format").value_or("") == "binary")
    {
        callback(BinaryMemoryResponse(type + std::to_string(page), static_cast<uint32_t>(offset),
                                      std::vector<uint8_t>(window, window + windowSize)));
        return;
    }

    Json::Value ret;
    ret["type"] = type;
    ret["page"] = page;
    ret["offset"] = StringHelper::Format("0x%04X", static_cast<unsigned>(offset));
    ret["length"] = length;

    if (sparse)
    {
        ret["format"] = "sparse";
        ret["non_zero"] = CountNonZeroBytes(window, windowSize);
        Json::Value segments(Json::arrayValue);
        for (const MemorySparseSegment& segment : BuildSparseSegments(window, windowSize))
        {
            Json::Value item;
            item["offset"] = segment.offset;
            item["length"] = segment.length;
            item["is_fill"] = segment.isFill;
            if (segment.isFill)
                item["fill"] = StringHelper::Format("0x%02X", segment.fill);
            else
                item["hex"] = segment.hex;
            segments.append(item);
        }
        ret["segments"] = segments;
    }
    else
    {
        Json::Value data = Json::arrayValue;
        for (uint32_t i = 0; i < windowSize; i++)
        {
            data.append(pagePtr[offset + i]);
        }
        ret["data"] = data;
    }

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
    if (!isROM && !isRAM && WriteRegionPage(req, emulator->GetContext(), type, pageStr, callback))
        return;  // a device memory region by name (DeviceMemory::Write: the device's own write path)

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

    // Strict index validation: an invalid page left pagePtr null and the
    // write loop below dereferenced it (crash); bad offset strings were
    // silently swallowed by catch(...) and truncated
    uint64_t page = 0;
    if (!StringHelper::TryParseUInt64(pageStr, page))
    {
        Json::Value error;
        error["error"] = "Bad Request";
        error["message"] = "Invalid page number '" + pageStr + "' (expected unsigned decimal)";

        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k400BadRequest);
        addCorsHeaders(resp);
        callback(resp);
        return;
    }
    const uint64_t maxWritePage = isRAM ? MAX_RAM_PAGES - 1 : MAX_ROM_PAGES - 1;
    if (page > maxWritePage)
    {
        Json::Value error;
        error["error"] = "Bad Request";
        error["message"] = "Invalid page number '" + pageStr + "' (expected 0-" + std::to_string(maxWritePage) + ")";

        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k400BadRequest);
        addCorsHeaders(resp);
        callback(resp);
        return;
    }

    uint64_t offset = 0;
    std::string offsetStr = (*body)["offset"].asString();
    const bool hexOffset = offsetStr.size() > 2 && (offsetStr.substr(0, 2) == "0x" || offsetStr.substr(0, 2) == "0X");
    const bool offsetOk = hexOffset ? StringHelper::TryParseUInt64(offsetStr, offset, 16)
                                    : StringHelper::TryParseUInt64(offsetStr, offset);
    if (!offsetOk || offset >= PAGE_SIZE)
    {
        Json::Value error;
        error["error"] = "Bad Request";
        error["message"] = "Invalid offset '" + offsetStr + "' (expected 0-" + std::to_string(PAGE_SIZE - 1) + " or 0x-prefixed hex)";

        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k400BadRequest);
        addCorsHeaders(resp);
        callback(resp);
        return;
    }

    uint8_t* pagePtr = isRAM ? memory->RAMPageAddress(static_cast<uint16_t>(page)) : memory->ROMPageHostAddress(static_cast<uint8_t>(page));
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

    Json::Value& data = (*body)["data"];
    size_t bytesWritten = 0;
    // A page edit behind the CPU's back: TTD must see it like any other tool write
    emulator->EditMemoryFromTool("WebAPI page write", [&] {
        for (Json::ArrayIndex i = 0; i < data.size() && (offset + i) < PAGE_SIZE; i++)
        {
            pagePtr[offset + i] = static_cast<uint8_t>(data[i].asUInt());
            bytesWritten++;
        }
        if (isRAM)
            memory->MarkRamPageEdited(page);
    });

    Json::Value ret;
    ret["success"] = true;
    ret["type"] = type;
    ret["page"] = page;
    ret["offset"] = StringHelper::Format("0x%04X", static_cast<unsigned>(offset));
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

namespace
{

// Thin Json glue over the core single-source serializers (portdecoder.cpp) -
// the tag/latch/decode dictionaries exist exactly once there and every
// automation surface (MCP, CLI, Lua, Python) renders the same names
Json::Value PortTagSetToJsonArray(PortTagSet tags)
{
    Json::Value arr = Json::arrayValue;
    for (const std::string& tagName : PortTagSetToStrings(tags))
        arr.append(tagName);
    return arr;
}

const char* LatchEnumToString(PagingLatch latch)
{
    // Core single-source name ("p7FFD", "pFFF7_w2", ...); nullptr for None
    return PagingLatchToString(latch);
}

Json::Value DecodeLatchValue(PagingLatch latch, uint32_t value, MEM_MODEL model, uint32_t ramSizeKB)
{
    // Core single-source §5.1 dictionary (DecodePagingLatch) with native types:
    // ints and bools, so the JSON shape matches Lua/Python bindings verbatim
    Json::Value decoded;
    for (const DecodedLatchField& field : DecodePagingLatch(latch, value, model, ramSizeKB))
    {
        if (field.isBool)
            decoded[field.key] = field.boolValue;
        else
            decoded[field.key] = field.intValue;
    }
    return decoded;
}

} // anonymous namespace

/// @brief GET /api/v1/emulator/{id}/state/paging
/// @brief Get unified paging state assembled from the tagged port registry (P1-2)
void EmulatorAPI::getStatePaging(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback,
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
    PortDecoder* portDecoder = context->pPortDecoder;
    ROM* rom = context->pCore ? context->pCore->GetROM() : nullptr;
    Json::Value ret;

    ret["model"] = Config::GetModelFullName(config.mem_model);
    // The Profi board, its sync PROM and the keyboard on its connector (as GET /state/memory reports them)
    if (IsProfiModel(config.mem_model))
    {
        ret["profi_board"] = (config.mem_model == MM_PROFI3) ? "v3" : "v5";
        ret["profi_sync_prom"] =
            ProfiSyncPromName(ProfiResolveSyncProm(static_cast<ProfiSyncProm>(config.profi_sync_prom), config.mem_model));
        ret["profi_keyboard"] = ProfiKeyboardName(ProfiKeyboardInForce(context));
        ret["profi_hires_cpu_hz"] = ProfiHiresCpuHz(config.mem_model == MM_PROFI, config.profi_zq3_mhz);
        ret["profi_zq3_mhz"] = static_cast<int>(ProfiClampZq3(config.profi_zq3_mhz));
        ret["profi_ay_clock"] = (config.mem_model == MM_PROFI && config.profi_ay_clock_new) ? "new" : "old";
    }

    // Latches array - from tagged port registry
    Json::Value latches = Json::arrayValue;
    if (portDecoder)
    {
        std::vector<PortMapEntry> latchEntries = portDecoder->GetPagingLatches(Tags(PortTag::Memory));

        for (const PortMapEntry& entry : latchEntries)
        {
            Json::Value latchObj;
            latchObj["port"] = StringHelper::Format("0x%04X", entry.port);
            latchObj["tags"] = PortTagSetToJsonArray(entry.tags);
            latchObj["device"] = entry.device ? entry.device : "";
            latchObj["gate"] = entry.gate ? Json::Value(entry.gate) : Json::Value::null;

            const char* latchName = LatchEnumToString(entry.latch);
            latchObj["latch"] = latchName ? latchName : Json::Value::null;

            uint32_t value = PortDecoder::ReadPagingLatch(entry.latch, state);
            latchObj["value"] = StringHelper::Format("0x%02X", value);

            Json::Value decoded = DecodeLatchValue(entry.latch, value, config.mem_model, config.ramsize);
            if (!decoded.empty())
                latchObj["decoded"] = decoded;

            latches.append(latchObj);
        }
    }
    ret["latches"] = latches;

    // Banks array - from Memory manager (the single source of window truth)
    Json::Value banks = Json::arrayValue;

    // Bank 0: 0x0000-0x3FFF
    {
        Json::Value bank;
        bank["bank"] = 0;
        bank["address_range"] = "0x0000-0x3FFF";
        if (memory.IsBank0ROM())
        {
            bank["type"] = "ROM";
            uint8_t romPage = memory.GetROMPage();
            bank["page"] = static_cast<int>(romPage);
            bank["read_write"] = "read-only";

            // ROM identification (§5.2)
            if (rom)
            {
                uint8_t* pagePtr = memory.ROMPageHostAddress(romPage);
                if (pagePtr)
                {
                    std::string signature = rom->CalculateSignature(pagePtr, 0x4000);
                    // GetROMTitle already carries the "Unknown ROM, <digest>"
                    // fallback - identical wording on every surface
                    bank["name"] = rom->GetROMTitle(signature);
                    bank["signature"] = signature;
                }
                // Role: what the model layout says this slot is (core single
                // source - ROM::GetROMPageRole, §5.2)
                bank["role"] = rom->GetROMPageRole(romPage);
            }
        }
        else
        {
            bank["type"] = "RAM";
            bank["page"] = static_cast<int>(memory.GetRAMPageForBank0());
        }
        banks.append(bank);
    }

    // Bank 1: 0x4000-0x7FFF (always RAM page 5)
    {
        Json::Value bank;
        bank["bank"] = 1;
        bank["address_range"] = "0x4000-0x7FFF";
        bank["type"] = "RAM";
        bank["page"] = static_cast<int>(memory.GetRAMPageForBank1());
        bank["note"] = "Screen 0 location";
        banks.append(bank);
    }

    // Bank 2: 0x8000-0xBFFF (always RAM page 2)
    {
        Json::Value bank;
        bank["bank"] = 2;
        bank["address_range"] = "0x8000-0xBFFF";
        bank["type"] = "RAM";
        bank["page"] = static_cast<int>(memory.GetRAMPageForBank2());
        banks.append(bank);
    }

    // Bank 3: 0xC000-0xFFFF (switchable RAM)
    {
        Json::Value bank;
        bank["bank"] = 3;
        bank["address_range"] = "0xC000-0xFFFF";
        bank["type"] = "RAM";
        bank["page"] = static_cast<int>(memory.GetRAMPageForBank3());
        banks.append(bank);
    }

    // Contended: the CPU waits for the video logic there (Core::IsSlotContended). Writable: a CPU write reaches the
    // mapped page (the mapper's view: ROM, TS-Conf W0_WE, ... - Memory::IsWindowWritable)
    for (Json::ArrayIndex slot = 0; slot < banks.size(); slot++)
    {
        banks[slot]["contended"] = context->pCore && context->pCore->IsSlotContended(static_cast<uint8_t>(slot));
        const bool writable = memory.IsWindowWritable(static_cast<uint8_t>(slot));
        banks[slot]["writable"] = writable;
        banks[slot]["read_write"] = writable ? "read/write" : "read-only";
    }

    // Sprinter: the PLD maps the windows (fast RAM, vROM, graphics, ISA): kind and physical page
    // from DeviceState::SprinterPaging, the same view every interface shows
    if (config.mem_model == MM_SPRINTER)
    {
        Json::Value sprinter = StateNodeToJson(DeviceState::SprinterPaging(context));
        for (Json::ArrayIndex slot = 0; slot < banks.size() && slot < sprinter["windows"].size(); slot++)
        {
            const Json::Value& window = sprinter["windows"][slot];
            banks[slot]["type"] = window["kind"];
            banks[slot]["page"] = window["page"];
            banks[slot].removeMember("note");
            if (window.isMember("note"))
                banks[slot]["note"] = window["note"];
        }
        ret["sprinter"] = sprinter;
    }

    ret["banks"] = banks;

    // Top-level flags
    ret["paging_locked"] = (state.p7FFD & PORT_7FFD_LOCK) != 0;
    ret["trdos_active"] = (state.flags & (CF_TRDOS | CF_DOSPORTS)) != 0;

    auto resp = HttpResponse::newHttpJsonResponse(ret);
    addCorsHeaders(resp);
    callback(resp);
}

} // namespace v1
} // namespace api
