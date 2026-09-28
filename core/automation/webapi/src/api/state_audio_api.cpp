// WebAPI State Audio Inspection Implementation
// Extracted from emulator_api.cpp - 2026-01-08

#include <drogon/HttpResponse.h>
#include <emulator/emulator.h>
#include <emulator/emulatormanager.h>
#include <json/json.h>

#include <bitset>
#include <filesystem>
#include <vector>

#include "../emulator_api.h"
#include "../common/statenode_json.h"
#include <emulator/state/devicestate.h>
#include <debugger/ttd/timetravelmanager.h>
#include <emulator/sound/chips/gs/soundchip_gs.h>
#include <emulator/sound/chips/neogs/neogsmedia.h>


using namespace drogon;
using namespace api::v1;

namespace api
{
namespace v1
{

// Helper functions declared in emulator_api.h / emulator_api.cpp.
extern void addCorsHeaders(HttpResponsePtr& resp);
// getEmulatorByIdOrIndex is a free function in api::v1 (emulator_api.h) —
// visible here without an EmulatorAPI instance.

/// @brief GET /api/v1/emulator/{id}/state/audio/ay
void EmulatorAPI::getStateAudioAY(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback,
                                  const std::string& id) const
{
    auto emulator = getEmulatorByIdOrIndex(id);

    if (!emulator)
    {
        Json::Value error;
        error["error"] = "Not Found";
        error["message"] = "Emulator not found with ID: " + id;

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

    SoundManager* soundManager = context->pSoundManager;
    if (!soundManager)
    {
        Json::Value error;
        error["error"] = "Internal Error";
        error["message"] = "Sound manager not available";

        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k500InternalServerError);
        addCorsHeaders(resp);
        callback(resp);
        return;
    }

    // Core report (DeviceState::Ay): the same tree every interface renders
    Json::Value ret = StateNodeToJson(DeviceState::Ay(context));

    auto resp = HttpResponse::newHttpJsonResponse(ret);
    addCorsHeaders(resp);
    callback(resp);
}

/// @brief GET /api/v1/emulator/{id}/state/audio/ay/{chip}
void EmulatorAPI::getStateAudioAYIndex(const HttpRequestPtr& req,
                                       std::function<void(const HttpResponsePtr&)>&& callback, const std::string& id,
                                       const std::string& chipStr) const
{
    auto emulator = getEmulatorByIdOrIndex(id);

    if (!emulator)
    {
        Json::Value error;
        error["error"] = "Not Found";
        error["message"] = "Emulator not found with ID: " + id;

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

    SoundManager* soundManager = context->pSoundManager;
    if (!soundManager || !soundManager->hasTurboSound())
    {
        Json::Value error;
        error["error"] = "Internal Error";
        error["message"] = "AY chips not available";

        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k500InternalServerError);
        addCorsHeaders(resp);
        callback(resp);
        return;
    }

    // Parse chip index
    int chipIndex = -1;
    try
    {
        chipIndex = std::stoi(chipStr);
    }
    catch (const std::exception&)
    {
        Json::Value error;
        error["error"] = "Bad Request";
        error["message"] = "Invalid chip index (must be integer)";

        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k400BadRequest);
        addCorsHeaders(resp);
        callback(resp);
        return;
    }

    // Get the requested chip
    SoundChip_AY8910* chip = soundManager->getAYChip(chipIndex);

    if (!chip)
    {
        Json::Value error;
        error["error"] = "Not Found";
        error["message"] = "AY chip " + chipStr + " not available";

        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k404NotFound);
        addCorsHeaders(resp);
        callback(resp);
        return;
    }

    // Core report (DeviceState::AyChip): the same tree every interface renders
    Json::Value ret = StateNodeToJson(DeviceState::AyChip(context, chipIndex));
    auto resp = HttpResponse::newHttpJsonResponse(ret);
    addCorsHeaders(resp);
    callback(resp);
}

/// @brief GET /api/v1/emulator/{id}/state/audio/ay/register/{reg}
void EmulatorAPI::getStateAudioAYRegister(const HttpRequestPtr& req,
                                          std::function<void(const HttpResponsePtr&)>&& callback, const std::string& id,
                                          const std::string& chipStr, const std::string& regStr) const
{
    auto emulator = getEmulatorByIdOrIndex(id);

    if (!emulator)
    {
        Json::Value error;
        error["error"] = "Not Found";
        error["message"] = "Emulator not found with ID: " + id;

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

    SoundManager* soundManager = context->pSoundManager;
    if (!soundManager || !soundManager->hasTurboSound())
    {
        Json::Value error;
        error["error"] = "Internal Error";
        error["message"] = "AY chips not available";

        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k500InternalServerError);
        addCorsHeaders(resp);
        callback(resp);
        return;
    }

    // Parse chip index
    int chipIndex = -1;
    try
    {
        chipIndex = std::stoi(chipStr);
    }
    catch (const std::exception&)
    {
        Json::Value error;
        error["error"] = "Bad Request";
        error["message"] = "Invalid chip index: " + chipStr;

        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k400BadRequest);
        addCorsHeaders(resp);
        callback(resp);
        return;
    }

    SoundChip_AY8910* chip = soundManager->getAYChip(chipIndex);
    if (!chip)
    {
        Json::Value error;
        error["error"] = "Not Found";
        error["message"] = "AY chip " + chipStr + " not available";

        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k404NotFound);
        addCorsHeaders(resp);
        callback(resp);
        return;
    }

    // Parse register number
    int regNum = -1;
    try
    {
        regNum = std::stoi(regStr);
    }
    catch (const std::exception&)
    {
        Json::Value error;
        error["error"] = "Bad Request";
        error["message"] = "Invalid register number (must be 0-15)";

        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k400BadRequest);
        addCorsHeaders(resp);
        callback(resp);
        return;
    }

    if (regNum < 0 || regNum > 15)
    {
        Json::Value error;
        error["error"] = "Bad Request";
        error["message"] = "Register number must be between 0 and 15";

        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k400BadRequest);
        addCorsHeaders(resp);
        callback(resp);
        return;
    }

    const uint8_t* registers = chip->getRegisters();
    uint8_t regValue = registers[regNum];

    Json::Value ret;
    ret["register_number"] = regNum;
    ret["register_name"] = SoundChip_AY8910::AYRegisterNames[regNum];
    ret["value_hex"] = "0x" + std::string((regValue < 16 ? "0" : "") + std::to_string(regValue));
    ret["value_dec"] = (int)regValue;
    ret["value_bin"] = std::bitset<8>(regValue).to_string();

    // Add specific decoding based on register
    Json::Value decoding;

    switch (regNum)
    {
        case 0:
        case 2:
        case 4:  // Fine period registers
        {
            int channel = regNum / 2;
            const char* channelNames[] = {"A", "B", "C"};
            decoding["description"] = std::string("Channel ") + channelNames[channel] + " tone period (fine)";
            decoding["note"] = "Lower 8 bits of 12-bit period value";
            uint8_t coarse = registers[regNum + 1];
            uint16_t period = (coarse << 8) | regValue;
            decoding["full_period"] = period;
            decoding["frequency_hz"] = 1750000.0 / (16.0 * (period + 1));
            break;
        }
        case 1:
        case 3:
        case 5:  // Coarse period registers
        {
            int channel = (regNum - 1) / 2;
            const char* channelNames[] = {"A", "B", "C"};
            decoding["description"] = std::string("Channel ") + channelNames[channel] + " tone period (coarse)";
            decoding["note"] = "Upper 4 bits of 12-bit period value";
            uint8_t fine = registers[regNum - 1];
            uint16_t period = (regValue << 8) | fine;
            decoding["full_period"] = period;
            decoding["frequency_hz"] = 1750000.0 / (16.0 * (period + 1));
            break;
        }
        case 6:  // Noise period
            decoding["description"] = "Noise generator period";
            decoding["period_value"] = (int)(regValue & 0x1F);
            decoding["frequency_hz"] = 1750000.0 / (16.0 * ((regValue & 0x1F) + 1));
            break;
        case 7:  // Mixer control
            decoding["description"] = "Mixer control and I/O port direction";
            decoding["channel_a_tone_enabled"] = ((regValue & 0x01) == 0);
            decoding["channel_b_tone_enabled"] = ((regValue & 0x02) == 0);
            decoding["channel_c_tone_enabled"] = ((regValue & 0x04) == 0);
            decoding["channel_a_noise_enabled"] = ((regValue & 0x08) == 0);
            decoding["channel_b_noise_enabled"] = ((regValue & 0x10) == 0);
            decoding["channel_c_noise_enabled"] = ((regValue & 0x20) == 0);
            decoding["porta_direction"] = ((regValue & 0x40) ? "input" : "output");
            decoding["portb_direction"] = ((regValue & 0x80) ? "input" : "output");
            break;
        case 8:
        case 9:
        case 10:  // Volume registers
        {
            int channel = regNum - 8;
            const char* channelNames[] = {"A", "B", "C"};
            decoding["description"] = std::string("Channel ") + channelNames[channel] + " volume";
            decoding["volume_level"] = (int)(regValue & 0x0F);
            decoding["envelope_mode"] = ((regValue & 0x10) != 0);
            if (regValue & 0x10)
            {
                decoding["note"] = "Volume controlled by envelope generator";
            }
            else
            {
                decoding["note"] = "Fixed volume level";
            }
            break;
        }
        case 11:  // Envelope period fine
            decoding["description"] = "Envelope period (fine)";
            decoding["note"] = "Lower 8 bits of 16-bit envelope period";
            {
                uint8_t coarse = registers[12];
                uint16_t period = (coarse << 8) | regValue;
                decoding["full_period"] = period;
                decoding["frequency_hz"] = 1750000.0 / (256.0 * (period + 1));
            }
            break;
        case 12:  // Envelope period coarse
            decoding["description"] = "Envelope period (coarse)";
            decoding["note"] = "Upper 8 bits of 16-bit envelope period";
            {
                uint8_t fine = registers[11];
                uint16_t period = (regValue << 8) | fine;
                decoding["full_period"] = period;
                decoding["frequency_hz"] = 1750000.0 / (256.0 * (period + 1));
            }
            break;
        case 13:  // Envelope shape
            decoding["description"] = "Envelope shape control";
            decoding["shape_value"] = (int)(regValue & 0x0F);
            decoding["continue"] = ((regValue & 0x01) != 0);
            decoding["attack"] = ((regValue & 0x02) != 0);
            decoding["alternate"] = ((regValue & 0x04) != 0);
            decoding["hold"] = ((regValue & 0x08) != 0);
            break;
        case 14:  // I/O Port A
            decoding["description"] = "I/O Port A";
            decoding["direction"] = ((registers[7] & 0x40) ? "input" : "output");
            decoding["value"] = (int)regValue;
            break;
        case 15:  // I/O Port B
            decoding["description"] = "I/O Port B";
            decoding["direction"] = ((registers[7] & 0x80) ? "input" : "output");
            decoding["value"] = (int)regValue;
            break;
    }

    ret["decoding"] = decoding;

    auto resp = HttpResponse::newHttpJsonResponse(ret);
    addCorsHeaders(resp);
    callback(resp);
}

/// @brief GET /api/v1/emulator/{id}/state/audio/beeper
void EmulatorAPI::getStateAudioBeeper(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback,
                                      const std::string& id) const
{
    auto emulator = getEmulatorByIdOrIndex(id);

    if (!emulator)
    {
        Json::Value error;
        error["error"] = "Not Found";
        error["message"] = "Emulator not found with ID: " + id;

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

    SoundManager* soundManager = context->pSoundManager;
    if (!soundManager)
    {
        Json::Value error;
        error["error"] = "Internal Error";
        error["message"] = "Sound manager not available";

        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k500InternalServerError);
        addCorsHeaders(resp);
        callback(resp);
        return;
    }

    Json::Value ret;
    ret["device"] = "Beeper (ULA integrated)";
    ret["output_port"] = "0xFE";
    ret["current_level"] = "unknown";  // Internal state not accessible
    ret["last_output"] = "unknown";    // Internal state not accessible
    ret["frequency_range_hz"] = "20 - 10000";
    ret["bit_resolution"] = 1;
    ret["sound_played_since_reset"] = false;  // TODO: Implement sound played tracking

    auto resp = HttpResponse::newHttpJsonResponse(ret);
    addCorsHeaders(resp);
    callback(resp);
}

/// @brief GET /api/v1/emulator/{id}/state/audio/gs
/// General Sound card state (GS design §10.1): mailbox flags, MPAG page,
/// per-channel DAC sample/volume and the coprocessor core. 404 when the
/// machine has no GS fitted ([SOUND] GSType=Z80 selects the card).
void EmulatorAPI::getStateAudioGS(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback,
                                  const std::string& id) const
{
    auto emulator = getEmulatorByIdOrIndex(id);

    if (!emulator)
    {
        Json::Value error;
        error["error"] = "Not Found";
        error["message"] = "Emulator not found with ID: " + id;

        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k404NotFound);
        addCorsHeaders(resp);
        callback(resp);
        return;
    }

    EmulatorContext* context = emulator->GetContext();
    SoundManager* soundManager = context ? context->pSoundManager : nullptr;
    GeneralSoundCard* gs = soundManager ? soundManager->getGeneralSound() : nullptr;

    if (!gs)
    {
        Json::Value error;
        error["error"] = "Not Found";
        error["message"] = "General Sound card not fitted (configure [SOUND] GSType=Z80 or LW)";

        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k404NotFound);
        addCorsHeaders(resp);
        callback(resp);
        return;
    }

    const uint8_t status = gs->getStatusRaw();

    Json::Value ret;
    ret["device"] = gs->deviceDescription();
    ret["implementation"] = gsImplementationLabel(gs->implementation());
    ret["enabled"] = true;
    ret["rom_loaded"] = gs->isROMLoaded();
    ret["firmware"] = gs->firmwareDescription();
    ret["ram_kb"] = static_cast<Json::UInt64>(gs->getRamSizeKB());
    ret["status"] = status;
    ret["command_pending"] = (status & 0x01) != 0;  // bit0: ZX command waiting
    ret["data_pending"] = (status & 0x80) != 0;     // bit7: GS data waiting
    // Both cards use a single-latch mailbox (2026-09-21 firmware-parity rewrite,
    // not a FIFO): command_queue_count is always 0 or 1 (mirrors command_pending)
    // on both personalities. data_queue_count is 0/1 on the LLE (mirrors
    // data_pending) but on the LW card counts its internal param-ordering
    // buffer (0..16, soundchip_gslw's PARAM_QUEUE_CAPACITY) - a real backlog
    // depth, not a bit mirror, because the instant-dispatch LW model buffers
    // params ahead of the command that consumes them.
    ret["command_queue_count"] = static_cast<Json::UInt64>(gs->getCommandQueueCount());
    ret["data_queue_count"] = static_cast<Json::UInt64>(gs->getDataQueueCount());
    ret["command_from_host"] = gs->getCommandFromHost();
    ret["data_from_host"] = gs->getDataFromHost();
    ret["data_to_host"] = gs->getDataToHost();
    ret["page"] = gs->getMPAG();  // MPAG banking latch (GS design §2.3)

    Json::Value channels(Json::arrayValue);
    for (int i = 0; i < gs->channelCount(); i++)
    {
        Json::Value channel;
        channel["sample"] = gs->getChannelSample(i);
        channel["volume"] = gs->getChannelVolume(i);
        channels.append(channel);
    }
    ret["channels"] = channels;

    Json::Value cpu;
    if (gs->hasCoprocessor())
    {
        cpu["pc"] = gs->getCPUReg(GSCpuRegister::PC);
        cpu["sp"] = gs->getCPUReg(GSCpuRegister::SP);
        cpu["af"] = gs->getCPUReg(GSCpuRegister::AF);
        cpu["halted"] = gs->isCPUHalted();
    }
    else
    {
        cpu["coprocessor"] = false;  // lightweight personality: no registers to show
    }
    ret["cpu"] = cpu;

    // NeoGS extras: configuration, windows, interrupts, SD, MP3, DMA
    NeoGSStateInfo ngs;
    if (gs->neogsState(ngs))
    {
        Json::Value n;
        n["flash"] = ngs.flashTitle;
        n["flash_modified"] = ngs.flashModified;
        n["gscfg0"] = ngs.gscfg0;
        n["clock_hz"] = ngs.clockHz;
        Json::Value pages(Json::arrayValue);
        for (int w = 0; w < 4; w++)
        {
            Json::Value window;
            window["page"] = ngs.pages[w];
            window["flash"] = ngs.windowFlash[w];
            pages.append(window);
        }
        n["windows"] = pages;
        n["led_on"] = ngs.ledOn;
        n["ready"] = ngs.readyForCommands;
        n["int_enable"] = ngs.intEnable;
        n["int_request"] = ngs.intRequest;
        n["tim_freq"] = ngs.timFreq;
        n["sctrl"] = ngs.sctrl;
        Json::Value sd;
        sd["present"] = ngs.sdPresent;
        if (ngs.sdPresent)
        {
            sd["path"] = ngs.sdPath;
            sd["sdhc"] = ngs.sdSdhc;
            sd["size_bytes"] = static_cast<Json::UInt64>(ngs.sdSizeBytes);
            sd["blocks_read"] = static_cast<Json::UInt64>(ngs.sdBlocksRead);
            sd["blocks_written"] = static_cast<Json::UInt64>(ngs.sdBlocksWritten);
        }
        n["sd"] = sd;
        Json::Value mp3;
        mp3["fitted"] = ngs.mp3Fitted;
        if (ngs.mp3Fitted)
        {
            mp3["chip"] = ngs.mp3Chip;
            mp3["dreq"] = ngs.mp3Dreq;
            mp3["rate"] = ngs.mp3Rate;
            mp3["channels"] = ngs.mp3Channels;
            mp3["frames"] = static_cast<Json::UInt64>(ngs.mp3Frames);
            mp3["decode_time_s"] = ngs.mp3DecodeSeconds;
            mp3["input_fill"] = static_cast<Json::UInt64>(ngs.mp3InputFill);
        }
        n["mp3"] = mp3;
        Json::Value dma;
        dma["select"] = ngs.dmaSelect;
        static const char* kModules[3] = {"zx", "sd", "mp3"};
        for (int m = 0; m < 3; m++)
        {
            Json::Value module;
            module["running"] = ngs.dmaRunning[m];
            module["address"] = ngs.dmaAddress[m];
            dma[kModules[m]] = module;
        }
        // ZX-DMA: the host's view (neogs-zxdma-design.md §7)
        Json::Value& zx = dma["zx"];
        zx["mode"] = ngs.zxMode;
        zx["overlay_installed"] = ngs.zxOverlayInstalled;
        zx["read_latch"] = ngs.zxReadLatch;
        zx["pending"] = ngs.zxPending;
        zx["pending_address"] = ngs.zxPendingAddress;
        zx["bytes_read"] = static_cast<Json::UInt64>(ngs.zxBytesRead);
        zx["bytes_written"] = static_cast<Json::UInt64>(ngs.zxBytesWritten);
        zx["bytes_dropped"] = static_cast<Json::UInt64>(ngs.zxBytesDropped);
        zx["wait_tstates"] = static_cast<Json::UInt64>(ngs.zxWaitTStates);
        zx["late_starts"] = static_cast<Json::UInt64>(ngs.zxLateStarts);
        zx["late_start_ticks"] = static_cast<Json::UInt64>(ngs.zxLateStartUnits);
        zx["watch_setting"] = ngs.zxWatchSetting;
        zx["watch_frames"] = ngs.zxWatchFrames;
        zx["watch_frames_left"] = ngs.zxWatchFramesLeft;
        n["dma"] = dma;
        ret["neogs"] = n;
    }

    // ?ram=1 - read-only dump of the card CPU's window #4000-#7FFF, where the
    // GS-compatible firmwares keep their runtime variables (NUMPG #4080 ..
    // MTSTAT #4151). Read through the card's side-effect-free peek, so it is
    // right on every card with a CPU (classic GS: upper half of MPAG 1;
    // NeoGS: whatever PG1 maps).
    uint8_t probe = 0;
    if (req->getParameter("ram") == "1" && gs->peekCardMemory(0x4000, probe))
    {
        static const char kHexDigits[] = "0123456789abcdef";
        constexpr size_t kWindow = 0x4000;
        std::string windowHex(kWindow * 2, '0');
        for (size_t i = 0; i < kWindow; i++)
        {
            uint8_t byte = 0;
            gs->peekCardMemory(static_cast<uint16_t>(0x4000 + i), byte);
            windowHex[i * 2] = kHexDigits[byte >> 4];
            windowHex[i * 2 + 1] = kHexDigits[byte & 0x0F];
        }
        ret["fixed_window_base"] = 0x4000;
        ret["fixed_window_hex"] = windowHex;
    }

    auto resp = HttpResponse::newHttpJsonResponse(ret);
    addCorsHeaders(resp);
    callback(resp);
}

/// @brief POST /api/v1/emulator/{id}/control/audio/gs
/// @param body {"action": "reset|reset_card|nmi|send_command|send_data|read_status|read_data|switch_personality|
///                         dump_module|sd_insert|sd_eject|flash_save",
///              "value": 0..255 (byte actions), "personality": "z80|lle|lw|lightweight|ngs|neogs" (switch_personality),
///              "path": image or file path (sd_insert, dump_module)}
/// Actions mirror the host-port semantics (GS design §10.1). The writes,
/// resets and NMI are live input: applied on the machine's thread at the next
/// instruction boundary, where the card is first flushed to the current ZX
/// tact (while paused: when execution continues); 409 while TTD replay owns
/// input. The reads are side-effect-free peeks:
///   reset             - full power-on reset (mailbox, volumes and timing too)
///   reset_card        - #33 bit7 pulse (CPU/banking/timing only, mailbox survives)
///   nmi               - #33 bit6 pulse
///   send_command      - OUT #BB semantics (sets the command-pending flag)
///   send_data         - OUT #B3 semantics (sets the data-pending flag)
///   read_status       - peek the IN #BB value (status | 0x7E)
///   read_data         - peek the GS->ZX byte (IN #B3 value; bit7 is not cleared)
///   switch_personality - runtime GS card personality swap (gs-card-
///                       personalities design): the host mailbox and activity
///                       counters survive the handoff; a module captured by
///                       the lightweight card is replayed through a fresh
///                       LLE firmware. Requested here, applied at the next
///                       frame boundary on the emulation thread
void EmulatorAPI::postControlAudioGS(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback,
                                     const std::string& id) const
{
    auto emulator = getEmulatorByIdOrIndex(id);

    if (!emulator)
    {
        Json::Value error;
        error["error"] = "Not Found";
        error["message"] = "Emulator not found with ID: " + id;

        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k404NotFound);
        addCorsHeaders(resp);
        callback(resp);
        return;
    }

    EmulatorContext* context = emulator->GetContext();
    SoundManager* soundManager = context ? context->pSoundManager : nullptr;
    GeneralSoundCard* gs = soundManager ? soundManager->getGeneralSound() : nullptr;

    if (!gs)
    {
        Json::Value error;
        error["error"] = "Not Found";
        error["message"] = "General Sound card not fitted (configure [SOUND] GSType=Z80 or LW)";

        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k404NotFound);
        addCorsHeaders(resp);
        callback(resp);
        return;
    }

    auto json = req->getJsonObject();
    if (!json || !json->isMember("action") || !json->get("action", "").isString())
    {
        Json::Value error;
        error["error"] = "Bad Request";
        error["message"] = "Missing or invalid 'action' field";

        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k400BadRequest);
        addCorsHeaders(resp);
        callback(resp);
        return;
    }

    const std::string action = json->get("action", "").asString();
    const bool needsValue = (action == "send_command" || action == "send_data");

    int value = 0;
    if (json->isMember("value"))
    {
        if (!json->get("value", 0).isNumeric() || json->get("value", 0).asInt() < 0
            || json->get("value", 0).asInt() > 255)
        {
            Json::Value error;
            error["error"] = "Bad Request";
            error["message"] = "'value' must be an integer in 0..255";

            auto resp = HttpResponse::newHttpJsonResponse(error);
            resp->setStatusCode(HttpStatusCode::k400BadRequest);
            addCorsHeaders(resp);
            callback(resp);
            return;
        }
        value = json->get("value", 0).asInt();
    }
    else if (needsValue)
    {
        Json::Value error;
        error["error"] = "Bad Request";
        error["message"] = "Action '" + action + "' requires a 'value' field (0..255)";

        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k400BadRequest);
        addCorsHeaders(resp);
        callback(resp);
        return;
    }

    // switch_personality selects the target with a string instead of a byte
    GSTypeKind personalityKind = GSTypeKind::NONE;
    if (action == "switch_personality")
    {
        if (!json->isMember("personality") || !json->get("personality", "").isString())
        {
            Json::Value error;
            error["error"] = "Bad Request";
            error["message"] = std::string("Action 'switch_personality' requires a 'personality' field (") + GS_PERSONALITY_NAMES + ")";

            auto resp = HttpResponse::newHttpJsonResponse(error);
            resp->setStatusCode(HttpStatusCode::k400BadRequest);
            addCorsHeaders(resp);
            callback(resp);
            return;
        }

        const std::string personality = json->get("personality", "").asString();
        if (!gsParsePersonality(personality, personalityKind))
        {
            Json::Value error;
            error["error"] = "Bad Request";
            error["message"] = "Unknown personality '" + personality + "' (expected " + GS_PERSONALITY_NAMES + ")";

            auto resp = HttpResponse::newHttpJsonResponse(error);
            resp->setStatusCode(HttpStatusCode::k400BadRequest);
            addCorsHeaders(resp);
            callback(resp);
            return;
        }
    }

    Json::Value ret;
    ret["status"] = "success";
    ret["action"] = action;

    // Host-port stimuli step the card's Z80, so they never run on this HTTP
    // thread: they go through the live-input path - applied on the machine's
    // thread at the next instruction boundary and journaled for TTD replay
    ttd::TTDInputKind inputKind = ttd::TTDInputKind::GSReset;
    const bool isInput = action == "reset" || action == "reset_card" || action == "nmi" ||
                         action == "send_command" || action == "send_data";
    if (action == "reset_card")
        inputKind = ttd::TTDInputKind::GSResetCard;
    else if (action == "nmi")
        inputKind = ttd::TTDInputKind::GSNmi;
    else if (action == "send_command")
        inputKind = ttd::TTDInputKind::GSCommand;
    else if (action == "send_data")
        inputKind = ttd::TTDInputKind::GSData;

    if (isInput)
    {
        ttd::TTDInputEvent ev;
        ev.kind = inputKind;
        ev.value = static_cast<uint8_t>(value);
        if (!context->pTimeTravelManager || !context->pTimeTravelManager->SubmitLiveInput(ev))
        {
            Json::Value error;
            error["error"] = "Conflict";
            error["message"] = "GS input refused: TTD replay owns input";

            auto resp = HttpResponse::newHttpJsonResponse(error);
            resp->setStatusCode(HttpStatusCode::k409Conflict);
            addCorsHeaders(resp);
            callback(resp);
            return;
        }
        ret["note"] = "applied at the next instruction boundary";
    }
    else if (action == "read_status")
    {
        // Peek: no host read cycle, the card is not stepped
        ret["value"] = gs->getStatusRaw() | 0x7E;
    }
    else if (action == "read_data")
    {
        // Peek: status bit 7 is left set, the card is not stepped
        ret["value"] = gs->getDataToHost();
    }
    else if (action == "switch_personality")
    {
        // HTTP thread: request the frame-boundary switch (the synchronous
        // switchGeneralSoundCard deletes/recreates the card and belongs to
        // the emulation thread)
        GSCardImplementation requestedImpl = GSCardImplementation::LLE;
        (void)gsImplementationOf(personalityKind, requestedImpl);
        ret["personality"] = gsImplementationShortName(requestedImpl);
        ret["current"] = gsImplementationShortName(gs->implementation());
        ret["requested"] = soundManager->requestGeneralSoundCardSwitch(personalityKind);
        ret["note"] = "applied at the next frame boundary";
    }
    else if (action == "dump_module")
    {
        // Diagnostics: write the last completed COM30..D2 upload (the raw
        // ProTracker module the host streamed) to a file, so real content
        // can be rendered offline / cross-checked against a reference player
        std::vector<uint8_t> bytes;
        bool playing = false;
        if (!gs->captureModuleUpload(bytes, playing))
        {
            Json::Value error;
            error["error"] = "Not Found";
            error["message"] = "No completed module upload to dump (no COM30..D2 stream captured yet)";
            auto resp = HttpResponse::newHttpJsonResponse(error);
            resp->setStatusCode(HttpStatusCode::k404NotFound);
            addCorsHeaders(resp);
            callback(resp);
            return;
        }
        std::string path = json->get("path", "").asString();
        if (path.empty())
            path = "gs-module-dump.mod";

        // WebAPI has no per-caller filesystem sandbox, and this is the only
        // GS action that takes a caller-supplied path: without a check, a
        // remote client (the server binds 0.0.0.0 by default) could write
        // the captured module to an arbitrary absolute path or escape the
        // working directory via "..", i.e. an arbitrary-file-write primitive
        // dressed up as a diagnostics dump. Restrict to a relative path with
        // no ".." component - matches the "drop a .mod in the working/scratch
        // directory" use this action was actually built for.
        const std::filesystem::path requested(path);
        bool pathIsSafe = !requested.is_absolute();
        if (pathIsSafe)
        {
            for (const auto& part : requested)
            {
                if (part == "..")
                {
                    pathIsSafe = false;
                    break;
                }
            }
        }
        if (!pathIsSafe)
        {
            Json::Value error;
            error["error"] = "Bad Request";
            error["message"] = "'path' must be a relative path with no '..' component: '" + path + "'";
            auto resp = HttpResponse::newHttpJsonResponse(error);
            resp->setStatusCode(HttpStatusCode::k400BadRequest);
            addCorsHeaders(resp);
            callback(resp);
            return;
        }

        std::ofstream out(path, std::ios::binary);
        if (!out)
        {
            Json::Value error;
            error["error"] = "Internal Server Error";
            error["message"] = "Cannot open '" + path + "' for writing";
            auto resp = HttpResponse::newHttpJsonResponse(error);
            resp->setStatusCode(HttpStatusCode::k500InternalServerError);
            addCorsHeaders(resp);
            callback(resp);
            return;
        }
        out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        ret["path"] = path;
        ret["bytes"] = static_cast<Json::UInt64>(bytes.size());
        ret["playing"] = playing;
    }
    else if (action == "sd_insert" || action == "sd_eject" || action == "flash_save")
    {
        // Checked here, carried out on the machine's thread (neogsmedia.h);
        // insert / eject are refused while a TTD recording runs
        NeoGSMediaResult result;
        if (action == "sd_insert")
        {
            const std::string path = json->get("path", "").asString();
            result = NeoGSRequestSdInsert(context, path);
            ret["path"] = path;
        }
        else if (action == "sd_eject")
            result = NeoGSRequestSdEject(context);
        else
            result = NeoGSRequestFlashSave(context);

        if (!NeoGSMediaAccepted(result))
        {
            const bool conflict = result == NeoGSMediaResult::NoNeoGS || result == NeoGSMediaResult::TtdRecording ||
                                  result == NeoGSMediaResult::ReplayOwnsInput;
            Json::Value error;
            error["error"] = conflict ? "Conflict" : "Unprocessable";
            error["message"] = "Action '" + action + "': " + NeoGSMediaResultText(result);
            auto resp = HttpResponse::newHttpJsonResponse(error);
            resp->setStatusCode(conflict ? HttpStatusCode::k409Conflict : HttpStatusCode::k422UnprocessableEntity);
            addCorsHeaders(resp);
            callback(resp);
            return;
        }
        ret["status"] = result == NeoGSMediaResult::Queued ? "queued" : "done";
    }
    else
    {
        Json::Value error;
        error["error"] = "Bad Request";
        error["message"] =
            "Unknown action '" + action +
            "' (expected reset, reset_card, nmi, send_command, send_data, read_status, read_data, switch_personality, "
            "dump_module, sd_insert, sd_eject or flash_save)";

        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k400BadRequest);
        addCorsHeaders(resp);
        callback(resp);
        return;
    }

    auto resp = HttpResponse::newHttpJsonResponse(ret);
    addCorsHeaders(resp);
    callback(resp);
}

namespace
{
const char* gsTraceSideToString(GSTraceSide side)
{
    switch (side)
    {
        case GSTraceSide::Host: return "host";
        case GSTraceSide::GsInternal: return "gs";
        case GSTraceSide::DacFetch: return "dac";
        case GSTraceSide::Interrupt: return "interrupt";
        case GSTraceSide::ZxDma: return "zxdma";
    }
    return "unknown";
}
}  // namespace

/// @brief GET /api/v1/emulator/{id}/state/audio/gs/porttrace?events=N
/// Always-on activity counters (proves whether the GS coprocessor is
/// executing and pushing DAC samples) + trace session status, optionally the
/// last N buffered events. Same data model as CLI 'gsporttrace' / MCP / Lua /
/// Python - the GS-coprocessor triage tool (see gsporttrace.h).
void EmulatorAPI::getStateAudioGSPortTrace(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback,
                                           const std::string& id) const
{
    auto emulator = getEmulatorByIdOrIndex(id);
    if (!emulator)
    {
        Json::Value error;
        error["error"] = "Not Found";
        error["message"] = "Emulator not found with ID: " + id;

        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k404NotFound);
        addCorsHeaders(resp);
        callback(resp);
        return;
    }

    EmulatorContext* context = emulator->GetContext();
    SoundManager* soundManager = context ? context->pSoundManager : nullptr;
    GeneralSoundCard* gs = soundManager ? soundManager->getGeneralSound() : nullptr;

    if (!gs)
    {
        Json::Value error;
        error["error"] = "Not Found";
        error["message"] = "General Sound card not fitted (configure [SOUND] GSType=Z80 or LW)";

        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k404NotFound);
        addCorsHeaders(resp);
        callback(resp);
        return;
    }

    const GSActivityCounters& c = gs->getActivityCounters();
    Json::Value ret;
    Json::Value counters;
    counters["cpu_steps"] = static_cast<Json::UInt64>(c.cpuSteps);
    counters["interrupts_accepted"] = static_cast<Json::UInt64>(c.interruptsAccepted);
    counters["interrupt_periods"] = static_cast<Json::UInt64>(c.interruptPeriods);
    counters["interrupts_coalesced"] = static_cast<Json::UInt64>(c.interruptsCoalesced);
    counters["nmis_accepted"] = static_cast<Json::UInt64>(c.nmisAccepted);
    counters["dac_fetches"] = static_cast<Json::UInt64>(c.dacFetches);
    counters["volume_latch_writes"] = static_cast<Json::UInt64>(c.volumeLatchWrites);
    counters["host_commands_received"] = static_cast<Json::UInt64>(c.hostCommandsReceived);
    counters["host_commands_dropped"] = static_cast<Json::UInt64>(c.hostCommandsDropped);
    counters["host_data_written"] = static_cast<Json::UInt64>(c.hostDataWritten);
    counters["host_data_dropped"] = static_cast<Json::UInt64>(c.hostDataDropped);
    counters["host_data_read"] = static_cast<Json::UInt64>(c.hostDataRead);
    counters["last_dac_fetch_gs_cycle"] = static_cast<Json::Int64>(c.lastDacFetchGsCycle);
    counters["last_dac_fetch_frame"] = static_cast<Json::UInt64>(c.lastDacFetchFrame);
    ret["counters"] = counters;

    Json::Value trace;
    trace["capturing"] = gs->isPortTraceCapturing();
    trace["armed"] = gs->isPortTraceArmed();
    trace["event_count"] = static_cast<Json::UInt64>(gs->getPortTraceEventCount());
    trace["total_produced"] = static_cast<Json::UInt64>(gs->getPortTraceTotalProduced());
    trace["total_evicted"] = static_cast<Json::UInt64>(gs->getPortTraceTotalEvicted());
    ret["trace"] = trace;

    Json::Value cpu;
    if (gs->hasCoprocessor())
    {
        cpu["pc"] = gs->getCPUReg(GSCpuRegister::PC);
        cpu["halted"] = gs->isCPUHalted();
    }
    ret["cpu"] = cpu;

    std::string eventsParam = req->getParameter("events");
    if (!eventsParam.empty())
    {
        size_t count = 50;
        try { count = static_cast<size_t>(std::stoul(eventsParam)); } catch (...) {}
        auto events = gs->getPortTraceLast(count);

        Json::Value eventsJson(Json::arrayValue);
        for (const auto& e : events)
        {
            Json::Value ev;
            ev["timestamp"] = static_cast<Json::Int64>(e.timestamp);
            ev["frame"] = e.frameNumber;
            ev["side"] = gsTraceSideToString(e.side);
            ev["direction"] = e.isOut() ? "out" : "in";
            ev["port"] = e.port;
            ev["value"] = e.value;
            ev["pc"] = e.pc;
            if (e.side == GSTraceSide::DacFetch)
                ev["channel"] = e.channel;
            if (e.side == GSTraceSide::ZxDma)
                ev["card_address"] = (static_cast<uint32_t>(e.channel) << 16) | e.port;
            if (e.side == GSTraceSide::Interrupt)
                ev["nmi"] = e.isNmi();
            eventsJson.append(ev);
        }
        ret["events"] = eventsJson;
    }

    auto resp = HttpResponse::newHttpJsonResponse(ret);
    addCorsHeaders(resp);
    callback(resp);
}

/// @brief POST /api/v1/emulator/{id}/control/audio/gs/porttrace — body: {"action": "start|stop|pause|resume|clear"}
void EmulatorAPI::postControlAudioGSPortTrace(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback,
                                              const std::string& id) const
{
    auto emulator = getEmulatorByIdOrIndex(id);
    if (!emulator)
    {
        Json::Value error;
        error["error"] = "Not Found";
        error["message"] = "Emulator not found with ID: " + id;

        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k404NotFound);
        addCorsHeaders(resp);
        callback(resp);
        return;
    }

    EmulatorContext* context = emulator->GetContext();
    SoundManager* soundManager = context ? context->pSoundManager : nullptr;
    GeneralSoundCard* gs = soundManager ? soundManager->getGeneralSound() : nullptr;

    if (!gs)
    {
        Json::Value error;
        error["error"] = "Not Found";
        error["message"] = "General Sound card not fitted (configure [SOUND] GSType=Z80 or LW)";

        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k404NotFound);
        addCorsHeaders(resp);
        callback(resp);
        return;
    }

    auto json = req->getJsonObject();
    if (!json || !json->isMember("action") || !json->get("action", "").isString())
    {
        Json::Value error;
        error["error"] = "Bad Request";
        error["message"] = "Missing or invalid 'action' field";

        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k400BadRequest);
        addCorsHeaders(resp);
        callback(resp);
        return;
    }

    const std::string action = json->get("action", "").asString();
    Json::Value ret;
    ret["status"] = "success";
    ret["action"] = action;

    if (action == "start")
        gs->startPortTrace();
    else if (action == "stop")
        gs->stopPortTrace();
    else if (action == "pause")
        gs->pausePortTrace();
    else if (action == "resume")
        gs->resumePortTrace();
    else if (action == "clear")
        gs->clearPortTrace();
    else
    {
        Json::Value error;
        error["error"] = "Bad Request";
        error["message"] = "Unknown action '" + action + "' (expected start, stop, pause, resume or clear)";

        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k400BadRequest);
        addCorsHeaders(resp);
        callback(resp);
        return;
    }

    auto resp = HttpResponse::newHttpJsonResponse(ret);
    addCorsHeaders(resp);
    callback(resp);
}

/// @brief GET /api/v1/emulator/{id}/state/audio/covox
void EmulatorAPI::getStateAudioCovox(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback,
                                     const std::string& id) const
{
    Json::Value ret;
    ret["status"] = "not_implemented";
    ret["description"] =
        "Covox is an 8-bit DAC (Digital-to-Analog Converter) that connects to various ports on the ZX Spectrum for "
        "sample playback.";
    ret["note"] = "This endpoint is reserved for future implementation.";

    auto resp = HttpResponse::newHttpJsonResponse(ret);
    addCorsHeaders(resp);
    callback(resp);
}

/// @brief GET /api/v1/emulator/{id}/state/audio/channels
void EmulatorAPI::getStateAudioChannels(const HttpRequestPtr& req,
                                        std::function<void(const HttpResponsePtr&)>&& callback,
                                        const std::string& id) const
{
    auto emulator = getEmulatorByIdOrIndex(id);

    if (!emulator)
    {
        Json::Value error;
        error["error"] = "Not Found";
        error["message"] = "Emulator not found with ID: " + id;

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

    SoundManager* soundManager = context->pSoundManager;
    Json::Value ret;

    // Beeper channel
    Json::Value beeper;
    beeper["available"] = true;
    beeper["current_level"] = "unknown";
    beeper["active"] = "unknown";
    ret["beeper"] = beeper;

    // AY channels
    Json::Value ayChannels;
    bool hasAY = (soundManager && soundManager->hasTurboSound());
    ayChannels["available"] = hasAY;

    if (hasAY)
    {
        Json::Value chips(Json::arrayValue);
        int ayCount = soundManager->getAYChipCount();

        for (int chipIdx = 0; chipIdx < ayCount; chipIdx++)
        {
            SoundChip_AY8910* chip = soundManager->getAYChip(chipIdx);
            if (!chip)
                continue;

            Json::Value chipChannels(Json::arrayValue);
            const char* channelNames[] = {"A", "B", "C"};
            const auto* toneGens = chip->getToneGenerators();

            for (int ch = 0; ch < 3; ch++)
            {
                Json::Value channel;
                const auto& toneGen = toneGens[ch];
                channel["name"] = std::string("AY") + std::to_string(chipIdx) + channelNames[ch];
                channel["active"] = (toneGen.toneEnabled() || toneGen.noiseEnabled());
                channel["volume"] = (int)toneGen.volume();
                channel["envelope_enabled"] = toneGen.envelopeEnabled();
                chipChannels.append(channel);
            }

            Json::Value chipInfo;
            chipInfo["chip_index"] = chipIdx;
            chipInfo["channels"] = chipChannels;
            chips.append(chipInfo);
        }
        ayChannels["chips"] = chips;
    }
    ret["ay_channels"] = ayChannels;

    // General Sound (dedicated detail endpoint: /state/audio/gs)
    Json::Value gs;
    bool hasGS = (soundManager && soundManager->hasGeneralSound());
    gs["available"] = hasGS;
    if (hasGS)
    {
        GeneralSoundCard* gsChip = soundManager->getGeneralSound();
        const uint8_t gsStatus = gsChip->getStatusRaw();
        gs["rom_loaded"] = gsChip->isROMLoaded();
        gs["ram_kb"] = (int)gsChip->getRamSizeKB();
        gs["cpu_halted"] = gsChip->isCPUHalted();
        gs["command_pending"] = (gsStatus & 0x01) != 0;
        gs["data_pending"] = (gsStatus & 0x80) != 0;
        Json::Value gsChannels(Json::arrayValue);
        for (int i = 0; i < gsChip->channelCount(); i++)
        {
            Json::Value channel;
            channel["name"] = std::string("GS") + std::to_string(i + 1);
            channel["sample"] = (int)gsChip->getChannelSample(i);
            channel["volume"] = (int)gsChip->getChannelVolume(i);
            gsChannels.append(channel);
        }
        gs["channels"] = gsChannels;
    }
    ret["general_sound"] = gs;

    // Covox (detail endpoint reserved; presence follows the config)
    Json::Value covox;
    covox["available"] = (soundManager && soundManager->hasCovox());
    ret["covox"] = covox;

    // Master audio state
    Json::Value master;
    master["muted"] = (soundManager ? soundManager->isMuted() : false);
    master["sample_rate_hz"] = static_cast<unsigned>(soundManager ? soundManager->getCoreRate() : 44100u);
    master["channels"] = "stereo";
    master["bit_depth"] = 16;
    ret["master"] = master;

    auto resp = HttpResponse::newHttpJsonResponse(ret);
    addCorsHeaders(resp);
    callback(resp);
}

/// @brief Audio state inspection (active emulator - no ID required)
/// Uses global selection priority, then stateless fallback
void EmulatorAPI::getStateAudioAYActive(const HttpRequestPtr& req,
                                        std::function<void(const HttpResponsePtr&)>&& callback) const
{
    auto emulator = getEmulatorWithGlobalSelection();

    if (!emulator)
    {
        auto manager = EmulatorManager::GetInstance();
        auto count = manager->GetEmulatorIds().size();

        Json::Value error;
        error["error"] = count == 0 ? "Not Found" : "Bad Request";
        error["message"] = count == 0 ? "No emulator available (none running)"
                                      : "Multiple emulators running. Please specify emulator ID in path: "
                                        "/api/v1/emulator/{id}/state/audio/ay";

        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(count == 0 ? HttpStatusCode::k404NotFound : HttpStatusCode::k400BadRequest);
        addCorsHeaders(resp);
        callback(resp);
        return;
    }

    getStateAudioAY(req, std::move(callback), emulator->GetId());
}

/// @brief Get specific AY chip details (active emulator)
/// Uses stateless auto-selection: only works if exactly one emulator exists
void EmulatorAPI::getStateAudioAYIndexActive(const HttpRequestPtr& req,
                                             std::function<void(const HttpResponsePtr&)>&& callback,
                                             const std::string& chip) const
{
    auto emulator = getEmulatorWithGlobalSelection();

    if (!emulator)
    {
        auto manager = EmulatorManager::GetInstance();
        auto count = manager->GetEmulatorIds().size();

        Json::Value error;
        error["error"] = count == 0 ? "Not Found" : "Bad Request";
        error["message"] = count == 0 ? "No emulator available (none running)"
                                      : "Multiple emulators running. Please specify emulator ID in path: "
                                        "/api/v1/emulator/{id}/state/audio/ay/" +
                                            chip;

        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(count == 0 ? HttpStatusCode::k404NotFound : HttpStatusCode::k400BadRequest);
        addCorsHeaders(resp);
        callback(resp);
        return;
    }

    getStateAudioAYIndex(req, std::move(callback), emulator->GetId(), chip);
}

/// @brief Get AY chip register details (active emulator)
/// Uses stateless auto-selection: only works if exactly one emulator exists
void EmulatorAPI::getStateAudioAYRegisterActive(const HttpRequestPtr& req,
                                                std::function<void(const HttpResponsePtr&)>&& callback,
                                                const std::string& chip, const std::string& reg) const
{
    auto emulator = getEmulatorWithGlobalSelection();

    if (!emulator)
    {
        auto manager = EmulatorManager::GetInstance();
        auto count = manager->GetEmulatorIds().size();

        Json::Value error;
        error["error"] = count == 0 ? "Not Found" : "Bad Request";
        error["message"] = count == 0 ? "No emulator available (none running)"
                                      : "Multiple emulators running. Please specify emulator ID in path: "
                                        "/api/v1/emulator/{id}/state/audio/ay/" +
                                            chip + "/register/" + reg;

        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(count == 0 ? HttpStatusCode::k404NotFound : HttpStatusCode::k400BadRequest);
        addCorsHeaders(resp);
        callback(resp);
        return;
    }

    getStateAudioAYRegister(req, std::move(callback), emulator->GetId(), chip, reg);
}

/// @brief Get beeper state (active emulator)
/// Uses stateless auto-selection: only works if exactly one emulator exists
void EmulatorAPI::getStateAudioBeeperActive(const HttpRequestPtr& req,
                                            std::function<void(const HttpResponsePtr&)>&& callback) const
{
    auto emulator = getEmulatorWithGlobalSelection();

    if (!emulator)
    {
        auto manager = EmulatorManager::GetInstance();
        auto count = manager->GetEmulatorIds().size();

        Json::Value error;
        error["error"] = count == 0 ? "Not Found" : "Bad Request";
        error["message"] = count == 0 ? "No emulator available (none running)"
                                      : "Multiple emulators running. Please specify emulator ID in path: "
                                        "/api/v1/emulator/{id}/state/audio/beeper";

        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(count == 0 ? HttpStatusCode::k404NotFound : HttpStatusCode::k400BadRequest);
        addCorsHeaders(resp);
        callback(resp);
        return;
    }

    getStateAudioBeeper(req, std::move(callback), emulator->GetId());
}

/// @brief Get GS state (active emulator)
/// Uses stateless auto-selection: only works if exactly one emulator exists
void EmulatorAPI::getStateAudioGSActive(const HttpRequestPtr& req,
                                        std::function<void(const HttpResponsePtr&)>&& callback) const
{
    auto emulator = getEmulatorWithGlobalSelection();

    if (!emulator)
    {
        auto manager = EmulatorManager::GetInstance();
        auto count = manager->GetEmulatorIds().size();

        Json::Value error;
        error["error"] = count == 0 ? "Not Found" : "Bad Request";
        error["message"] = count == 0 ? "No emulator available (none running)"
                                      : "Multiple emulators running. Please specify emulator ID in path: "
                                        "/api/v1/emulator/{id}/state/audio/gs";

        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(count == 0 ? HttpStatusCode::k404NotFound : HttpStatusCode::k400BadRequest);
        addCorsHeaders(resp);
        callback(resp);
        return;
    }

    getStateAudioGS(req, std::move(callback), emulator->GetId());
}

/// @brief Get Covox state (active emulator)
/// Uses stateless auto-selection: only works if exactly one emulator exists
void EmulatorAPI::getStateAudioCovoxActive(const HttpRequestPtr& req,
                                           std::function<void(const HttpResponsePtr&)>&& callback) const
{
    auto emulator = getEmulatorWithGlobalSelection();

    if (!emulator)
    {
        auto manager = EmulatorManager::GetInstance();
        auto count = manager->GetEmulatorIds().size();

        Json::Value error;
        error["error"] = count == 0 ? "Not Found" : "Bad Request";
        error["message"] = count == 0 ? "No emulator available (none running)"
                                      : "Multiple emulators running. Please specify emulator ID in path: "
                                        "/api/v1/emulator/{id}/state/audio/covox";

        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(count == 0 ? HttpStatusCode::k404NotFound : HttpStatusCode::k400BadRequest);
        addCorsHeaders(resp);
        callback(resp);
        return;
    }

    getStateAudioCovox(req, std::move(callback), emulator->GetId());
}

/// @brief Get audio channels state (active emulator)
/// Uses stateless auto-selection: only works if exactly one emulator exists
void EmulatorAPI::getStateAudioChannelsActive(const HttpRequestPtr& req,
                                              std::function<void(const HttpResponsePtr&)>&& callback) const
{
    auto emulator = getEmulatorWithGlobalSelection();

    if (!emulator)
    {
        auto manager = EmulatorManager::GetInstance();
        auto count = manager->GetEmulatorIds().size();

        Json::Value error;
        error["error"] = count == 0 ? "Not Found" : "Bad Request";
        error["message"] = count == 0 ? "No emulator available (none running)"
                                      : "Multiple emulators running. Please specify emulator ID in path: "
                                        "/api/v1/emulator/{id}/state/audio/channels";

        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(count == 0 ? HttpStatusCode::k404NotFound : HttpStatusCode::k400BadRequest);
        addCorsHeaders(resp);
        callback(resp);
        return;
    }

    getStateAudioChannels(req, std::move(callback), emulator->GetId());
}

}  // namespace v1
}  // namespace api
