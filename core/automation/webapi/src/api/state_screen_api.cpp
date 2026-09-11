// WebAPI State Screen Inspection Implementation
// Extracted from emulator_api.cpp - 2026-01-08

#include "../emulator_api.h"

#include <drogon/HttpResponse.h>
#include <emulator/config.h>
#include <emulator/cpu/z80.h>
#include <emulator/emulator.h>
#include <emulator/emulatormanager.h>
#include <emulator/video/screendigest.h>
#include <json/json.h>

#include <sstream>

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

    std::string model = Config::GetModelFullName(config.mem_model);
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

    std::string model = Config::GetModelFullName(config.mem_model);
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
    ret["flash_cycle_position"] = static_cast<Json::UInt64>(state.frame_counter % 32);
    ret["flash_cycle_total"] = 32;
    ret["toggle_interval_frames"] = 16;
    ret["toggle_interval_seconds"] = 0.32;  // at 50Hz

    auto resp = HttpResponse::newHttpJsonResponse(ret);
    addCorsHeaders(resp);
    callback(resp);
}

/// @brief GET /api/v1/emulator/{id}/video/beam
/// @brief Current raster beam position — t-state, line, dot, zone — plus
///        frame timing derived from the machine model. Zone boundaries follow
///        the canonical Screen::InitRaster calculation (RasterState).
void EmulatorAPI::getBeamPosition(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback,
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

    const CONFIG& config = context->config;
    Screen* screen = context->pScreen;

    if (!screen || config.t_line == 0 || config.frame == 0)
    {
        Json::Value error;
        error["error"] = "Conflict";
        error["message"] = "Machine model timing is not initialized yet";

        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k409Conflict);
        addCorsHeaders(resp);
        callback(resp);
        return;
    }

    Z80* cpu = context->pCore ? context->pCore->GetZ80() : nullptr;
    const uint32_t tstate = cpu ? static_cast<uint32_t>(cpu->t) : screen->GetCurrentTstate();
    const uint32_t tInFrame = tstate % config.frame;

    const VideoModeEnum mode = screen->GetVideoMode();
    const RasterDescriptor& rd = screen->rasterDescriptors[mode];
    const RasterState& rs = screen->GetRasterState();

    // Canonical raster boundaries when the raster state has been calculated;
    // plain config division otherwise (mode not yet set up)
    const bool rasterValid = rs.tstatesPerLine != 0;
    const uint32_t tstatesPerLine = rasterValid ? rs.tstatesPerLine : config.t_line;
    const uint32_t totalLines = rd.vSyncLines + rd.vBlankLines + rd.fullFrameHeight;

    const uint32_t line = tInFrame / tstatesPerLine;
    const uint32_t dotInLine = tInFrame % tstatesPerLine;
    const uint32_t beamX = dotInLine * rs.pixelsPerTState;  // Absolute raster pixel within the line

    // Vertical zone — frame-relative t-state ranges (Screen::InitRaster math)
    std::string vZone = "beyond_raster";
    if (rasterValid)
    {
        if (tInFrame <= rs.blankAreaEnd)
            vZone = (line < rd.vSyncLines) ? "vsync" : "vblank";
        else if (tInFrame <= rs.topBorderAreaEnd)
            vZone = "top_border";
        else if (tInFrame <= rs.screenAreaEnd)
            vZone = "screen";
        else if (tInFrame <= rs.bottomBorderAreaEnd)
            vZone = "bottom_border";
    }

    // Horizontal zone — line-relative t-state ranges, meaningful in screen rows only
    std::string hZone = "-";
    if (vZone == "screen")
    {
        if (dotInLine <= rs.blankLineAreaEnd)
            hZone = "hblank";
        else if (dotInLine <= rs.leftBorderAreaEnd)
            hZone = "left_border";
        else if (dotInLine <= rs.screenLineAreaEnd)
            hZone = "paper";
        else if (dotInLine <= rs.rightBorderAreaEnd)
            hZone = "right_border";
        else
            hZone = "beyond_line";
    }

    // Combined agent-facing zone: paper / border / hblank / vsync / vblank / ...
    std::string zone = vZone;
    if (vZone == "screen")
        zone = (hZone == "paper") ? "paper" : (hZone == "hblank" ? "hblank" : "border");

    const bool inPaper = zone == "paper";
    const bool inVisible = vZone == "screen" && hZone != "hblank" && hZone != "beyond_line";

    std::string model = Config::GetModelFullName(config.mem_model);

    Json::Value ret;
    ret["model"] = model;
    ret["video_mode"] = Screen::GetVideoModeName(mode);
    ret["tstate"] = tstate;
    ret["tstate_in_frame"] = tInFrame;
    ret["frame"] = static_cast<Json::UInt64>(context->emulatorState.frame_counter);
    ret["line"] = line;
    ret["dot_in_line"] = dotInLine;
    ret["beam_x"] = beamX;
    ret["beam_y"] = line;
    ret["zone"] = zone;
    ret["vertical_zone"] = vZone;
    ret["horizontal_zone"] = hZone;
    ret["in_visible_area"] = inVisible;
    ret["in_paper"] = inPaper;

    if (inPaper)
    {
        // Position relative to the paper area (256x192 for standard modes)
        Json::Value paper;
        paper["x"] = (dotInLine - rs.screenLineAreaStart) * rs.pixelsPerTState;
        paper["y"] = line - (rd.vSyncLines + rd.vBlankLines + rd.screenOffsetTop);
        ret["paper"] = paper;
    }

    // Frame timing derived from the machine model
    Json::Value timing;
    timing["tstates_per_line"] = config.t_line;
    timing["lines_per_frame"] = totalLines;
    timing["frame_tstates"] = config.frame;
    timing["raster_frame_tstates"] = tstatesPerLine * totalLines;  // Raster-defined duration; config.frame may pad it
    timing["frame_duration_us"] = config.frame_duration_us;
    timing["frames_per_second"] = config.intfq;
    timing["cpu_hz"] = static_cast<double>(config.frame) * config.intfq;
    timing["frequency_multiplier"] = context->emulatorState.current_z80_frequency_multiplier;
    ret["frame_timing"] = timing;

    // Raw raster geometry for the current video mode
    Json::Value raster;
    raster["full_frame_width"] = rd.fullFrameWidth;
    raster["full_frame_height"] = rd.fullFrameHeight;
    raster["screen_width"] = rd.screenWidth;
    raster["screen_height"] = rd.screenHeight;
    raster["screen_offset_left"] = rd.screenOffsetLeft;
    raster["screen_offset_top"] = rd.screenOffsetTop;
    raster["pixels_per_line"] = rd.pixelsPerLine;
    raster["h_sync_pixels"] = rd.hSyncPixels;
    raster["h_blank_pixels"] = rd.hBlankPixels;
    raster["v_sync_lines"] = rd.vSyncLines;
    raster["v_blank_lines"] = rd.vBlankLines;
    raster["total_lines"] = totalLines;
    ret["raster"] = raster;

    auto resp = HttpResponse::newHttpJsonResponse(ret);
    addCorsHeaders(resp);
    callback(resp);
}

/// @brief GET /api/v1/emulator/{id}/state/screen/digest
/// @brief Deterministic FNV-1a 64 digest over screen memory with poll-driven
///        change tracking. Query params:
///        - banks=5,7     Physical RAM pages to hash (default: model-dependent)
///        - start/end     Explicit Z80 address range override (hex or decimal)
///        - include_border=false  Drop the border color from the combined digest
void EmulatorAPI::getStateScreenDigest(const HttpRequestPtr& req,
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
    if (!context || !context->pMemory || !context->pScreen)
    {
        Json::Value error;
        error["error"] = "Internal Error";
        error["message"] = "Emulator context is not fully initialized";

        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k500InternalServerError);
        addCorsHeaders(resp);
        callback(resp);
        return;
    }

    const CONFIG& config = context->config;
    EmulatorState& state = context->emulatorState;
    Memory* memory = context->pMemory;

    const bool is128K =
        (config.mem_model == MM_SPECTRUM128 || config.mem_model == MM_PENTAGON || config.mem_model == MM_PLUS3);

    // Parse query parameters
    auto params = req->getParameters();

    auto parseAddressValue = [](const std::string& value, uint16_t& out) -> bool
    {
        try
        {
            unsigned long parsed;
            if (value.rfind("0x", 0) == 0 || value.rfind("0X", 0) == 0)
                parsed = std::stoul(value, nullptr, 16);
            else
                parsed = std::stoul(value);
            if (parsed > 0xFFFF)
                return false;
            out = static_cast<uint16_t>(parsed);
            return true;
        }
        catch (...)
        {
            return false;
        }
    };

    Json::Value ret;
    ret["emulator_id"] = id;
    ret["frame"] = static_cast<Json::UInt64>(state.frame_counter);
    ret["algorithm"] = "fnv1a-64";

    const uint64_t previousDigest = state.last_screen_digest;
    const uint64_t previousFrame = state.last_screen_digest_frame;

    uint64_t combined = ScreenDigest::kInitialValue;

    // Explicit Z80 range mode: ?start=&end= override the bank list
    auto startIt = params.find("start");
    auto endIt = params.find("end");
    if (startIt != params.end() || endIt != params.end())
    {
        uint16_t start = 0x4000;
        uint16_t end = 0x7FFF;

        if ((startIt != params.end() && !parseAddressValue(startIt->second, start)) ||
            (endIt != params.end() && !parseAddressValue(endIt->second, end)) || start > end)
        {
            Json::Value error;
            error["error"] = "Bad Request";
            error["message"] = "Invalid 'start'/'end' parameters (expected hex or decimal, start <= end)";

            auto resp = HttpResponse::newHttpJsonResponse(error);
            resp->setStatusCode(HttpStatusCode::k400BadRequest);
            addCorsHeaders(resp);
            callback(resp);
            return;
        }

        uint64_t rangeDigest = ScreenDigest::DigestZ80Range(memory, start, end);
        combined = ScreenDigest::MixValue(combined, static_cast<uint8_t>(rangeDigest & 0xFF));
        combined = ScreenDigest::MixValue(combined, static_cast<uint8_t>((rangeDigest >> 8) & 0xFF));
        combined = ScreenDigest::MixValue(combined, static_cast<uint8_t>((rangeDigest >> 16) & 0xFF));
        combined = ScreenDigest::MixValue(combined, static_cast<uint8_t>((rangeDigest >> 24) & 0xFF));
        combined = ScreenDigest::MixValue(combined, static_cast<uint8_t>((rangeDigest >> 32) & 0xFF));
        combined = ScreenDigest::MixValue(combined, static_cast<uint8_t>((rangeDigest >> 40) & 0xFF));
        combined = ScreenDigest::MixValue(combined, static_cast<uint8_t>((rangeDigest >> 48) & 0xFF));
        combined = ScreenDigest::MixValue(combined, static_cast<uint8_t>((rangeDigest >> 56) & 0xFF));

        Json::Value range;
        range["start"] = StringHelper::Format("0x%04X", start);
        range["end"] = StringHelper::Format("0x%04X", end);
        range["digest"] = StringHelper::Format("0x%016llX", rangeDigest);
        ret["z80_range"] = range;
    }
    else
    {
        // Bank mode: default is both screen pages on 128K-class models, page 5 only otherwise
        std::vector<uint16_t> banks;
        auto banksIt = params.find("banks");
        if (banksIt != params.end())
        {
            std::string token;
            std::istringstream stream(banksIt->second);
            while (std::getline(stream, token, ','))
            {
                try
                {
                    unsigned long page = std::stoul(token);
                    if (page <= 255)
                        banks.push_back(static_cast<uint16_t>(page));
                }
                catch (...)
                {
                }
            }

            if (banks.empty())
            {
                Json::Value error;
                error["error"] = "Bad Request";
                error["message"] = "Invalid 'banks' parameter (expected comma-separated page numbers)";

                auto resp = HttpResponse::newHttpJsonResponse(error);
                resp->setStatusCode(HttpStatusCode::k400BadRequest);
                addCorsHeaders(resp);
                callback(resp);
                return;
            }
        }
        else
        {
            banks.push_back(ScreenDigest::kScreen0RAMPage);
            if (is128K)
                banks.push_back(ScreenDigest::kScreen1RAMPage);
        }

        Json::Value banksJson(Json::arrayValue);
        for (uint16_t page : banks)
        {
            uint64_t digest = ScreenDigest::DigestRAMPage(memory, page);

            Json::Value item;
            item["page"] = page;
            item["digest"] = StringHelper::Format("0x%016llX", digest);
            item["size"] = static_cast<Json::UInt64>(ScreenDigest::kRAMPageSize);
            banksJson.append(item);

            for (int shift = 0; shift < 64; shift += 8)
            {
                combined = ScreenDigest::MixValue(combined, static_cast<uint8_t>((digest >> shift) & 0xFF));
            }
        }
        ret["banks"] = banksJson;
    }

    // Border color fold-in (visible output includes the border)
    bool includeBorder = true;
    auto borderIt = params.find("include_border");
    if (borderIt != params.end())
    {
        includeBorder = borderIt->second == "true" || borderIt->second == "1" || borderIt->second == "yes";
    }

    uint8_t borderColor = 0;
    if (includeBorder)
    {
        borderColor = context->pScreen->GetBorderColor();
        combined = ScreenDigest::MixValue(combined, borderColor);
    }

    // Change tracking against the previous poll
    const bool changed = combined != previousDigest;

    ret["combined"] = StringHelper::Format("0x%016llX", combined);
    ret["include_border"] = includeBorder;
    if (includeBorder)
        ret["border_color"] = borderColor;
    ret["changed"] = changed;
    ret["previous_digest"] = StringHelper::Format("0x%016llX", previousDigest);
    if (previousFrame != 0)
    {
        ret["previous_digest_frame"] = static_cast<Json::UInt64>(previousFrame);
        ret["frames_since_previous"] = static_cast<Json::UInt64>(state.frame_counter - previousFrame);
    }

    state.last_screen_digest = combined;
    state.last_screen_digest_frame = state.frame_counter;

    auto resp = HttpResponse::newHttpJsonResponse(ret);
    addCorsHeaders(resp);
    callback(resp);
}

} // namespace v1
} // namespace api
