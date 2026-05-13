// WebAPI State Screen Inspection Implementation
// Extracted from emulator_api.cpp - 2026-01-08

#include "../emulator_api.h"

#include <drogon/HttpResponse.h>
#include <emulator/emulator.h>
#include <emulator/emulatormanager.h>
#include <json/json.h>

using namespace drogon;
using namespace api::v1;

namespace api
{
namespace v1
{

// Helper function declared in emulator_api.cpp
extern void addCorsHeaders(HttpResponsePtr& resp);

/// @brief Get screen configuration
void EmulatorAPI::getStateScreen(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback,
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
    Json::Value ret;

    // Check if verbose mode is requested
    bool verbose = false;
    auto params = req->getParameters();
    if (params.find("verbose") != params.end())
    {
        std::string verboseParam = params.at("verbose");
        verbose = (verboseParam == "true" || verboseParam == "1" || verboseParam == "yes");
    }

    bool is128K =
        (config.mem_model == MM_SPECTRUM128 || config.mem_model == MM_PENTAGON || config.mem_model == MM_PLUS3);

    std::string model = "ZX Spectrum 48K";
    if (config.mem_model == MM_SPECTRUM128)
        model = "ZX Spectrum 128K";
    else if (config.mem_model == MM_PENTAGON)
        model = "Pentagon 128K";
    else if (config.mem_model == MM_PLUS3)
        model = "ZX Spectrum +3";

    ret["model"] = model;
    ret["is_128k"] = is128K;
    ret["display_mode"] = "standard";
    ret["border_color"] = static_cast<int>(context->pScreen->GetBorderColor());

    if (is128K)
    {
        uint8_t port7FFD = context->emulatorState.p7FFD;
        bool shadowScreen = (port7FFD & 0x08) != 0;

        ret["active_screen"] = shadowScreen ? 1 : 0;
        ret["active_ram_page"] = shadowScreen ? 7 : 5;
    }
    else
    {
        ret["active_screen"] = 0;
        ret["active_ram_page"] = 5;
    }

    // Only include verbose details if requested
    if (!verbose)
    {
        auto resp = HttpResponse::newHttpJsonResponse(ret);
        addCorsHeaders(resp);
        callback(resp);
        return;
    }

    // Verbose mode - add detailed information
    if (is128K)
    {
        uint8_t port7FFD = context->emulatorState.p7FFD;
        bool shadowScreen = (port7FFD & 0x08) != 0;
        uint8_t ramBank = port7FFD & 0x07;

        ret["active_screen"] = shadowScreen ? 1 : 0;

        // Screen 0 info
        Json::Value screen0;
        screen0["name"] = "Screen 0 (normal)";
        screen0["ram_page"] = 5;
        screen0["physical_offset"] = "0x0000-0x1FFF";
        screen0["pixel_data"] = "0x0000-0x17FF (6144 bytes)";
        screen0["attributes"] = "0x1800-0x1AFF (768 bytes)";
        screen0["z80_access"] = "0x4000-0x7FFF (bank 1 - always accessible)";
        screen0["ula_display"] = !shadowScreen;
        screen0["contention"] = "active";
        ret["screen_0"] = screen0;

        // Screen 1 info
        Json::Value screen1;
        screen1["name"] = "Screen 1 (shadow)";
        screen1["ram_page"] = 7;
        screen1["physical_offset"] = "0x0000-0x1FFF";
        screen1["pixel_data"] = "0x0000-0x17FF (6144 bytes)";
        screen1["attributes"] = "0x1800-0x1AFF (768 bytes)";
        screen1["z80_access"] = (ramBank == 7) ? "0xC000-0xFFFF (bank 3, page 7 mapped)" : "not mapped";
        screen1["ula_display"] = shadowScreen;
        screen1["contention"] = (ramBank == 7) ? "inactive" : "n/a";
        ret["screen_1"] = screen1;

        // Port 0x7FFD info
        Json::Value port7FFD_info;
        char hexStr[5];
        snprintf(hexStr, sizeof(hexStr), "0x%02X", port7FFD);
        port7FFD_info["value_hex"] = hexStr;
        port7FFD_info["value_dec"] = port7FFD;

        std::string binary;
        for (int i = 7; i >= 0; i--)
            binary += (port7FFD >> i) & 1 ? '1' : '0';
        port7FFD_info["value_bin"] = binary;

        port7FFD_info["ram_bank"] = ramBank;
        port7FFD_info["shadow_screen"] = shadowScreen;
        port7FFD_info["rom_select"] = (port7FFD & 0x10) ? "48K BASIC" : "128K Editor";
        port7FFD_info["paging_locked"] = (port7FFD & 0x20) != 0;

        ret["port_0x7FFD"] = port7FFD_info;
    }
    else
    {
        // 48K model - single screen
        Json::Value screen;
        screen["name"] = "Single screen";
        screen["physical_location"] = "RAM page 5, offset 0x0000-0x1FFF";
        screen["pixel_data"] = "0x4000-0x57FF (6144 bytes)";
        screen["attributes"] = "0x5800-0x5AFF (768 bytes)";
        screen["z80_access"] = "0x4000-0x7FFF (always accessible)";
        screen["contention"] = "active";
        ret["screen"] = screen;
    }

    auto resp = HttpResponse::newHttpJsonResponse(ret);
    addCorsHeaders(resp);
    callback(resp);
}

/// @brief GET /api/v1/emulator/{id}/state/screen/mode
/// @brief Get video mode details
void EmulatorAPI::getStateScreenMode(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback,
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
    Json::Value ret;

    std::string model = "ZX Spectrum 48K";
    if (config.mem_model == MM_SPECTRUM128)
        model = "ZX Spectrum 128K";
    else if (config.mem_model == MM_PENTAGON)
        model = "Pentagon 128K";
    else if (config.mem_model == MM_PLUS3)
        model = "ZX Spectrum +3";

    ret["model"] = model;
    ret["video_mode"] = "standard";
    ret["resolution"] = "256×192";
    ret["color_depth"] = "2 colors per attribute block";
    ret["attribute_size"] = "8×8 pixels";

    Json::Value memory;
    memory["pixel_data_bytes"] = 6144;
    memory["attribute_bytes"] = 768;
    memory["total_bytes"] = 6912;
    ret["memory_layout"] = memory;

    if (config.mem_model == MM_SPECTRUM128 || config.mem_model == MM_PENTAGON || config.mem_model == MM_PLUS3)
    {
        uint8_t port7FFD = context->emulatorState.p7FFD;
        bool shadowScreen = (port7FFD & 0x08) != 0;
        ret["active_screen"] = shadowScreen ? 1 : 0;
        ret["active_ram_page"] = shadowScreen ? 7 : 5;
    }

    ret["compatibility"] = "48K/128K/+2/+2A/+3 standard";

    auto resp = HttpResponse::newHttpJsonResponse(ret);
    addCorsHeaders(resp);
    callback(resp);
}

/// @brief GET /api/v1/emulator/{id}/state/screen/flash
/// @brief Get flash state
void EmulatorAPI::getStateScreenFlash(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback,
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

    EmulatorState& state = context->emulatorState;
    Json::Value ret;

    uint8_t flashCounter = (state.frame_counter / 16) & 1;
    uint8_t framesUntilToggle = 16 - (state.frame_counter % 16);

    ret["flash_phase"] = flashCounter ? "inverted" : "normal";
    ret["frames_until_toggle"] = framesUntilToggle;
    ret["flash_cycle_position"] = state.frame_counter % 32;
    ret["flash_cycle_total"] = 32;
    ret["toggle_interval_frames"] = 16;
    ret["toggle_interval_seconds"] = 0.32;  // at 50Hz

    auto resp = HttpResponse::newHttpJsonResponse(ret);
    addCorsHeaders(resp);
    callback(resp);
}

} // namespace v1
} // namespace api
