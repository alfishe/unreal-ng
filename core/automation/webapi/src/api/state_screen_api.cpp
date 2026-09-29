// WebAPI State Screen Inspection Implementation
// Extracted from emulator_api.cpp - 2026-01-08

#include "../common/statenode_json.h"
#include "emulator/state/devicestate.h"
#include "../common/jsonnumber.h"
#include "../emulator_api.h"

#include <drogon/HttpResponse.h>
#include <common/stringhelper.h>
#include <emulator/config.h>
#include <emulator/cpu/z80.h>
#include <emulator/emulator.h>
#include <emulator/emulatormanager.h>
#include <emulator/platform.h>
#include <emulator/video/screen.h>
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

    // Check if verbose mode is requested
    bool verbose = false;
    auto params = req->getParameters();
    if (params.find("verbose") != params.end())
    {
        const std::string& verboseParam = params.at("verbose");
        verbose = (verboseParam == "true" || verboseParam == "1" || verboseParam == "yes");
    }

    // One report for every automation module (DeviceState::Screen)
    Json::Value ret = StateNodeToJson(DeviceState::Screen(context, verbose));
    // Legacy aliases kept for existing clients
    ret["is_128k"] = ret["shadow_screen_capable"];
    ret["display_mode"] = ret["video_mode"];

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
    if (!context || !context->pScreen)
    {
        Json::Value error;
        error["error"] = "Internal Error";
        error["message"] = "Unable to access emulator context or screen";

        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k500InternalServerError);
        addCorsHeaders(resp);
        callback(resp);
        return;
    }

    // One report for every automation module (DeviceState::ScreenMode)
    Json::Value ret = StateNodeToJson(DeviceState::ScreenMode(context));

    // Legacy per-mode flags kept for existing clients (the mode name says the same)
    switch (context->pScreen->GetVideoMode())
    {
        case M_P16:  ret["eff7_16col"] = true; break;
        case M_PMC:  ret["eff7_hwmc"] = true; break;
        case M_PHR:  ret["eff7_512"] = true; break;
        case M_P384: ret["overscan"] = true; break;
        case M_PROFIHR:
            ret["profi_hires"] = true;
            ret["framebuffer"] = "608x288";
            ret["raster"] = "312 lines x 224 T (69888 T frame)";
            break;
        default: break;
    }

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

    Json::Value ret = StateNodeToJson(DeviceState::ScreenFlash(context));

    auto resp = HttpResponse::newHttpJsonResponse(ret);
    addCorsHeaders(resp);
    callback(resp);
}

/// @brief GET /api/v1/emulator/{id}/state/screen/attributes
/// @brief Per-cell ink/paper/bright/flash decoded from screen attribute memory
void EmulatorAPI::getStateScreenAttributes(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback,
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

    int screen = -1;
    const std::string screenText = req->getParameter("screen");
    if (!screenText.empty())
        screen = std::atoi(screenText.c_str());

    Json::Value ret = StateNodeToJson(DeviceState::ScreenAttributes(context, screen));

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
    const RasterDescriptor& timingRd = screen->GetTimingDescriptor(mode);
    const RasterState& rs = screen->GetRasterState();

    // Canonical raster boundaries when the raster state has been calculated;
    // plain config division otherwise (mode not yet set up)
    const bool rasterValid = rs.tstatesPerLine != 0;
    const uint32_t tstatesPerLine = rasterValid ? rs.tstatesPerLine : config.t_line;
    const uint32_t totalLines = timingRd.vSyncLines + timingRd.vBlankLines + timingRd.fullFrameHeight;

    // Zones and paper position in the active mode's geometry (renderer line origin)
    const BeamPosition beam = screen->DescribeBeam(tInFrame);

    std::string model = Config::GetModelFullName(config.mem_model);

    Json::Value ret;
    ret["model"] = model;
    ret["video_mode"] = Screen::GetVideoModeName(mode);
    ret["tstate"] = tstate;
    ret["tstate_in_frame"] = tInFrame;
    ret["frame"] = static_cast<Json::UInt64>(context->emulatorState.frame_counter);
    ret["line"] = tInFrame / tstatesPerLine;
    ret["dot_in_line"] = tInFrame % tstatesPerLine;
    ret["beam_x"] = beam.beamX;
    ret["beam_y"] = tInFrame / tstatesPerLine;
    ret["zone"] = beam.zone;
    ret["vertical_zone"] = beam.verticalZone;
    ret["horizontal_zone"] = beam.horizontalZone;
    ret["in_visible_area"] = beam.inVisibleArea;
    ret["in_paper"] = beam.inPaper;

    if (beam.inPaper)
    {
        // Mode pixels under the beam (320x200, 640x200, 512x240, 256x192...);
        // one T covers x..x_end
        Json::Value paper;
        paper["x"] = beam.paperX;
        paper["x_end"] = beam.paperXEnd;
        paper["y"] = beam.paperY;
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
    raster["pixels_per_line"] = timingRd.pixelsPerLine;
    raster["h_sync_pixels"] = timingRd.hSyncPixels;
    raster["h_blank_pixels"] = timingRd.hBlankPixels;
    raster["v_sync_lines"] = timingRd.vSyncLines;
    raster["v_blank_lines"] = timingRd.vBlankLines;
    raster["paper_start_t"] = rs.screenLineAreaStart;
    raster["paper_end_t"] = rs.screenLineAreaEnd;
    raster["paper_dots_per_t"] = rs.paperDotsPerT;
    raster["total_lines"] = totalLines;
    ret["raster"] = raster;

    auto resp = HttpResponse::newHttpJsonResponse(ret);
    addCorsHeaders(resp);
    callback(resp);
}

/// @brief GET /api/v1/emulator/{id}/state/screen/digest
/// @brief Deterministic FNV-1a 64 digest over screen memory with poll-driven
///        change tracking. Query params:
///        - mode=active     Hash the RAM pages the CURRENT video mode actually
///                          displays (ATM hardware modes follow the 7FFD-selected
///                          bit-plane pair {videoPage-4, videoPage}); default
///                          `default` keeps the model-dependent pages 5/7
///        - banks=5,7     Physical RAM pages to hash (default: model-dependent;
///                        overrides mode=active)
///        - start/end     Explicit Z80 address range override (hex or decimal;
///                        overrides mode=active and banks)
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

    const bool is128K = Screen::HasShadowScreen(config.mem_model);

    // Parse query parameters
    auto params = req->getParameters();

    auto parseAddressValue = [](const std::string& value, uint16_t& out) -> bool
    {
        uint32_t parsed = 0;
        if (!ParseJsonUInt(Json::Value(value), 0xFFFF, parsed))
            return false;
        out = static_cast<uint16_t>(parsed);
        return true;
    };

    Json::Value ret;
    ret["emulator_id"] = id;
    ret["frame"] = static_cast<Json::UInt64>(state.frame_counter);
    ret["algorithm"] = "fnv1a-64";

    // mode=active: derive the bank list from the video mode the machine is
    // actually displaying instead of the fixed model-dependent pages (P1-3).
    // Explicit banks=/start,end overrides still win.
    bool activeMode = false;
    auto modeIt = params.find("mode");
    if (modeIt != params.end())
    {
        const std::string& modeValue = modeIt->second;
        if (modeValue == "active")
        {
            activeMode = true;
        }
        else if (modeValue != "default")
        {
            Json::Value error;
            error["error"] = "Bad Request";
            error["message"] = "Invalid 'mode' parameter (expected 'default' or 'active')";

            auto resp = HttpResponse::newHttpJsonResponse(error);
            resp->setStatusCode(HttpStatusCode::k400BadRequest);
            addCorsHeaders(resp);
            callback(resp);
            return;
        }
    }

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
            if (activeMode)
            {
                // Surface actually displayed by the current video mode: ZX modes
                // keep pages 5/7, ATM hardware modes hash the 7FFD-selected
                // bit-plane pair - flipping FF77 between ZX and 16c surfaces now
                // flips the digest even with constant underlying pages
                const VideoModeEnum videoMode = context->pScreen->GetVideoMode();
                banks = Screen::GetActiveSurfaceRAMPages(videoMode, state.p7FFD, is128K);

                Json::Value activeSurface;
                activeSurface["video_mode"] = Screen::GetVideoModeName(videoMode);
                Json::Value pagesJson(Json::arrayValue);
                for (uint16_t page : banks)
                    pagesJson.append(page);
                activeSurface["pages"] = pagesJson;
                ret["active_surface"] = activeSurface;
            }
            else
            {
                banks.push_back(ScreenDigest::kScreen0RAMPage);
                if (is128K)
                    banks.push_back(ScreenDigest::kScreen1RAMPage);
            }
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
