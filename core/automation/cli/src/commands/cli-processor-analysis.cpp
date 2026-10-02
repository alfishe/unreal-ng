// CLI Analysis Commands — Phase-2 analysis capabilities shared with WebAPI/MCP:
// screen digest, beam position, frame cost, coverage, AY log, audio capture,
// video recording. Mirrors the WebAPI handlers (analyzers_api, profiler_api,
// state_screen_api, recording_api) so every interface exposes the same features.

#include "cli-audio-mixer.h"
#include "cli-processor.h"

#include <3rdparty/tinywav/tinywav.h>
#include <debugger/analyzers/analyzermanager.h>
#include <debugger/analyzers/audiocapture/audiocaptureanalyzer.h>
#include <debugger/analyzers/aylog/ayloganalyzer.h>
#include <debugger/analyzers/coverage/coverageanalyzer.h>
#include <debugger/debugmanager.h>
#include <emulator/config.h>
#include <emulator/cpu/z80.h>
#include <emulator/emulator.h>
#include <emulator/emulatormanager.h>
#include <emulator/emulatorcontext.h>
#include <emulator/memory/memory.h>
#include <emulator/memory/rom.h>
#include <emulator/cpu/core.h>
#include <emulator/platform.h>
#include <emulator/ports/portdecoder.h>
#include <emulator/sound/soundmanager.h>
#include <emulator/sound/audio.h>
#include <emulator/state/devicestate.h>
#include <emulator/video/screendigest.h>

#include <algorithm>
#include <cmath>
#include <ctime>
#include <filesystem>
#include <thread>
#include <iomanip>
#include <sstream>

#ifdef ENABLE_RECORDING
#include "recordingmanager.h"
#include "cli-videorecord-options.h"
#include "../../../temporalstatus.h"
#endif

namespace
{
/// Parse a decimal or 0x-prefixed hex 16-bit address; returns false on garbage
bool parseAddress16(const std::string& value, uint16_t& out)
{
    try
    {
        unsigned long parsed = std::stoul(value, nullptr, 0);
        if (parsed > 0xFFFF)
            return false;
        out = static_cast<uint16_t>(parsed);
        return true;
    }
    catch (...)
    {
        return false;
    }
}

/// Default recording output: temp dir / unreal-cli / <id>_<timestamp>.<ext>
std::string defaultRecordingPath(const std::string& id, const std::string& extension)
{
    std::string safeId = id;
    for (char& c : safeId)
    {
        if (c == '-')
            c = '_';
    }

    static unsigned counter = 0;
    const long long stamp = static_cast<long long>(std::time(nullptr)) * 1000 + (counter++ % 1000);

    std::error_code ec;
    std::filesystem::path directory = std::filesystem::temp_directory_path(ec) / "unreal-cli";
    if (!ec)
        std::filesystem::create_directories(directory, ec);
    const std::string name = "video-" + safeId + "-" + std::to_string(stamp) + "." + extension;
    return ec ? name : (directory / name).string();
}
}  // namespace

// HandleDigest — FNV-1a-64 digest of the screen area (change detection without
// transferring pixels). Mirrors GET /api/v1/emulator/{id}/state/screen/digest.
// --active hashes the RAM pages the CURRENT video mode actually displays
// (ATM hardware modes follow the 7FFD-selected bit-plane pair) instead of the
// fixed model-dependent pages 5/7 (P1-3). Explicit range/banks still win.
void CLIProcessor::HandleDigest(const ClientSession& session, const std::vector<std::string>& args)
{
    auto emulator = GetSelectedEmulator(session);
    if (!emulator)
    {
        session.SendResponse("No emulator selected.");
        return;
    }

    EmulatorContext* context = emulator->GetContext();
    if (!context || !context->pMemory || !context->pScreen)
    {
        session.SendResponse("Emulator context is not fully initialized.");
        return;
    }

    // Arguments: explicit Z80 range ("digest <start> <end>"), bank list, border toggle
    ScreenDigestQuery query;
    for (size_t i = 0; i < args.size(); i++)
    {
        if (args[i] == "--banks" && i + 1 < args.size())
        {
            std::string token;
            std::istringstream stream(args[++i]);
            while (std::getline(stream, token, ','))
            {
                uint16_t page;
                if (parseAddress16(token, page))
                    query.banks.push_back(page);
            }
        }
        else if (args[i] == "--active")
        {
            query.active = true;
        }
        else if (args[i] == "--no-border")
        {
            query.includeBorder = false;
        }
        else if (i + 1 < args.size() && query.banks.empty())
        {
            // Positional pair: start end
            if (!parseAddress16(args[i], query.start) || !parseAddress16(args[i + 1], query.end) || query.start > query.end)
            {
                session.SendResponse(
                    "Invalid range. Usage: digest [<start> <end>] [--banks p1,p2] [--active] [--no-border]");
                return;
            }
            query.range = query.start != 0 || query.end != 0;
            i++;
        }
    }

    // One computation for every interface (ScreenDigestCompute): the same pages, surface and change tracking
    const ScreenDigestResult r = ScreenDigestCompute::Compute(context, query);
    std::stringstream ss;
    ss << std::hex << std::uppercase << std::setfill('0');
    if (r.range)
    {
        ss << "Range:  $" << std::setw(4) << r.start << "-$" << std::setw(4) << r.end << NEWLINE;
        ss << "Digest: 0x" << std::setw(16) << r.rangeDigest << NEWLINE;
    }
    else if (r.deviceSurface)
    {
        ss << "Mode: " << r.videoMode << ", surface: " << r.surface.name << " (" << std::dec << r.surface.bytes
           << " bytes: " << r.surface.description << ")" << NEWLINE << std::hex;
        ss << "Surface: 0x" << std::setw(16) << r.surface.digest << NEWLINE;
    }
    else
    {
        if (r.activeSurface)
        {
            ss << "Mode: " << r.videoMode << ", pages:";
            for (uint16_t page : r.activePages)
                ss << " " << std::dec << page;
            ss << NEWLINE << std::hex << std::uppercase << std::setfill('0');
        }
        for (const auto& [page, digest] : r.banks)
            ss << "Page " << std::dec << std::setw(2) << page << std::hex << std::uppercase << ": 0x" << std::setw(16)
               << digest << NEWLINE;
    }

    ss << "Combined: 0x" << std::setw(16) << r.combined << NEWLINE;
    ss << "Algorithm: fnv1a-64" << NEWLINE;
    ss << "Frame: " << std::dec << r.frame << NEWLINE;
    ss << "Border: " << static_cast<int>(r.border) << (r.includeBorder ? "" : " (excluded)") << NEWLINE;
    ss << "Changed: " << (r.changed ? "yes" : "no") << NEWLINE;
    if (r.previousFrame != 0)
    {
        ss << "Previous: 0x" << std::hex << std::setw(16) << r.previousDigest << " (frame " << std::dec << r.previousFrame
           << ", " << (r.frame - r.previousFrame) << " frames ago)" << NEWLINE;
    }

    session.SendResponse(ss.str());
}

// HandlePorts — static port-map introspection: which devices answer which I/O
// ports on this model, under which gating conditions, plus the live routing
// flags. Mirrors GET /api/v1/emulator/{id}/ports (PortDecoder::getPortMapEntries /
// GetMouseRoutingState, single source).
void CLIProcessor::HandlePorts(const ClientSession& session, const std::vector<std::string>& args)
{
    (void)args;  // No parameters

    auto emulator = GetSelectedEmulator(session);
    if (!emulator)
    {
        session.SendResponse("No emulator selected.");
        return;
    }

    EmulatorContext* context = emulator->GetContext();
    if (!context || !context->pPortDecoder)
    {
        session.SendResponse("Emulator context is not fully initialized.");
        return;
    }

    PortDecoder* decoder = context->pPortDecoder;
    const CONFIG& config = context->config;
    EmulatorState& state = context->emulatorState;

    std::stringstream ss;
    ss << "Model: " << Config::GetModelFullName(config.mem_model) << NEWLINE << NEWLINE;

    // Tags and latch names come from the core single-source serializers
    // (PortTagSetToStrings / PagingLatchToString) - same names as WebAPI,
    // MCP, Lua and Python
    ss << "Port    Mask    Match   Tags                  Latch     Device                            Gate" << NEWLINE;
    for (const PortMapEntry& entry : decoder->getPortMapEntries())
    {
        std::string tagNames;
        for (const std::string& tagName : PortTagSetToStrings(entry.tags))
        {
            if (!tagNames.empty())
                tagNames += ",";
            tagNames += tagName;
        }
        const char* latchName = PagingLatchToString(entry.latch);

        ss << "0x" << std::hex << std::uppercase << std::setfill('0') << std::setw(4) << entry.port << "  0x"
           << std::setw(4) << entry.mask << "  0x" << std::setw(4) << entry.match << "  " << std::setfill(' ')
           << std::left << std::setw(21) << (tagNames.empty() ? "-" : tagNames)
           << std::setw(9) << (latchName ? latchName : "-")
           << std::setw(33) << entry.device << (entry.gate ? entry.gate : "") << NEWLINE << std::right;
    }

    // Live routing state: the flags that flip rows on/off right now
    bool mouseDecoded = false;
    std::string mouseNote;
    decoder->GetMouseRoutingState(mouseDecoded, mouseNote);

    ss << NEWLINE << "Live routing state:" << NEWLINE;
    ss << "  TR-DOS active: " << (((state.flags & (CF_TRDOS | CF_DOSPORTS)) != 0) ? "yes" : "no") << NEWLINE;
    ss << "  Mouse ports: " << (mouseDecoded ? "" : "shadowed - ") << mouseNote << NEWLINE;

    const bool scorpion = (config.mem_model == MM_SCORP || config.mem_model == MM_PROFSCORP);
    if (scorpion)
        ss << "  Shadow monitor paged: " << (((state.p1FFD & 0x02) != 0) ? "yes" : "no") << NEWLINE;
    else
        ss << "  Shadow monitor paged: n/a (model has no #1FFD latch)" << NEWLINE;

    session.SendResponse(ss.str());
}

// HandlePaging — tagged paging latches + bank table (P1-2 design).
// Mirrors GET /api/v1/emulator/{id}/state/paging.
void CLIProcessor::HandlePaging(const ClientSession& session, const std::vector<std::string>& args)
{
    (void)args;  // No parameters

    auto emulator = GetSelectedEmulator(session);
    if (!emulator)
    {
        session.SendResponse("No emulator selected.");
        return;
    }

    EmulatorContext* context = emulator->GetContext();
    if (!context || !context->pPortDecoder || !context->pMemory)
    {
        session.SendResponse("Emulator context is not fully initialized.");
        return;
    }

    PortDecoder* decoder = context->pPortDecoder;
    Memory& memory = *context->pMemory;
    const CONFIG& config = context->config;
    EmulatorState& state = context->emulatorState;
    ROM* rom = context->pCore ? context->pCore->GetROM() : nullptr;

    std::stringstream ss;
    ss << "Model: " << Config::GetModelFullName(config.mem_model) << NEWLINE;
    ss << "Paging locked: " << ((state.p7FFD & PORT_7FFD_LOCK) ? "yes" : "no") << NEWLINE;
    ss << "TR-DOS active: " << (((state.flags & (CF_TRDOS | CF_DOSPORTS)) != 0) ? "yes" : "no") << NEWLINE;
    ss << NEWLINE;

    // Latches table
    ss << "Latches:" << NEWLINE;
    std::vector<PortMapEntry> latches = decoder->GetPagingLatches(Tags(PortTag::Memory));
    for (const PortMapEntry& entry : latches)
    {
        uint32_t value = PortDecoder::ReadPagingLatch(entry.latch, state);
        const char* latchName = PagingLatchToString(entry.latch);
        ss << "  0x" << std::hex << std::uppercase << std::setfill('0') << std::setw(4) << entry.port
           << (latchName ? " (" + std::string(latchName) + ")" : "") << " = 0x" << std::setw(2) << value << std::dec << std::setfill(' ');

        // Decoded bits - core §5.1 dictionary (DecodePagingLatch), the same
        // keys and values as /state/paging and the Lua/Python bindings
        for (const DecodedLatchField& field : DecodePagingLatch(entry.latch, value, config.mem_model, config.ramsize))
        {
            ss << "  " << field.key << "=";
            if (field.isBool)
                ss << (field.boolValue ? "1" : "0");
            else
                ss << field.intValue;
        }
        ss << NEWLINE;
    }
    ss << NEWLINE;

    // Banks table
    ss << "Banks:" << NEWLINE;
    // Bank 0
    ss << "  #0  0x0000-0x3FFF  ";
    if (memory.IsBank0ROM())
    {
        uint8_t romPage = memory.GetROMPage();
        ss << "ROM p" << (int)romPage;
        if (rom)
        {
            uint8_t* pagePtr = memory.ROMPageHostAddress(romPage);
            if (pagePtr)
            {
                std::string sig = rom->CalculateSignature(pagePtr, 0x4000);
                // GetROMTitle carries the "Unknown ROM, <digest>" fallback;
                // role comes from the core layout table (ROM::GetROMPageRole,
                // §5.2) - a role/name mismatch is the wrong-ROM signal
                ss << "  \"" << rom->GetROMTitle(sig) << "\"";
                ss << "  [" << rom->GetROMPageRole(romPage) << "]";
            }
        }
    }
    else
    {
        ss << "RAM p" << (int)memory.GetRAMPageForBank0();
    }
    // Contended: the CPU waits for the video logic there (Core::IsSlotContended)
    auto contended = [context](uint8_t slot) {
        return (context->pCore && context->pCore->IsSlotContended(slot)) ? "  (contended)" : "";
    };
    ss << contended(0) << NEWLINE;

    // Banks 1-3
    ss << "  #1  0x4000-0x7FFF  RAM p" << (int)memory.GetRAMPageForBank1() << contended(1) << "  [Screen 0]" << NEWLINE;
    ss << "  #2  0x8000-0xBFFF  RAM p" << (int)memory.GetRAMPageForBank2() << contended(2) << NEWLINE;
    ss << "  #3  0xC000-0xFFFF  RAM p" << (int)memory.GetRAMPageForBank3() << contended(3) << NEWLINE;

    session.SendResponse(ss.str());
}

// HandleBeam — current raster beam position and zone, plus the layer pixel
// under it. Mirrors GET /api/v1/emulator/{id}/video/beam (DeviceState::VideoBeam).
void CLIProcessor::HandleBeam(const ClientSession& session, const std::vector<std::string>& args)
{
    (void)args;  // No parameters

    auto emulator = GetSelectedEmulator(session);
    if (!emulator)
    {
        session.SendResponse("No emulator selected.");
        return;
    }
    EmulatorContext* context = emulator->GetContext();
    if (!context || !context->pScreen)
    {
        session.SendResponse("Emulator context is not fully initialized.");
        return;
    }
    session.SendResponse("Beam position:" + std::string(NEWLINE) + DeviceState::ToText(DeviceState::VideoBeam(context)));
}

// HandleVideo — video debug translation (PLAN #42): layout, pixel sources,
// byte -> pixels, text grid. Mirrors GET /api/v1/emulator/{id}/video/layout,
// /video/pixel, /video/address and /video/text (the same DeviceState reports).
void CLIProcessor::HandleVideo(const ClientSession& session, const std::vector<std::string>& args)
{
    static const char* kUsage =
        "Usage:\n"
        "  video layout                   - layers, beam windows, framebuffer placement\n"
        "  video pixel <x> <y> [layer]    - memory, registers and palette cell behind a pixel\n"
        "  video pixel t <tstate>         - the same for the point under the beam at a frame T\n"
        "  video address <page> <offset>  - pixels a RAM byte feeds\n"
        "  video address z80 <addr>       - pixels the byte at a Z80 address feeds\n"
        "  video address palette <offset> - pixels drawn with a palette cell (16-bit cells at 2n)\n"
        "  video address sprite_ram <off> - pixels of the sprite a sprite attribute word describes\n"
        "  video address vram <offset>    - pixels a byte of the machine's video RAM feeds (Sprinter)\n"
        "  video text [layer]             - text grid of a text mode (ATM / ZX-Evo / TS-Conf / Sprinter text squares)\n"
        "  video changes [1|2]            - video change log: latch changes with T, line, PC; palette / mode table writes\n"
        "  video temporal [status]        - ZX DLSS de-flicker: algorithm, delays, timing\n"
        "  video temporal list            - algorithms that can be switched on\n"
        "  video temporal off             - switch the de-flicker off\n"
        "  video temporal <algorithm>     - switch it on (e.g. mod-tpgwafsd)\n"
        "Numbers are decimal or 0x-hex.";

    auto emulator = GetSelectedEmulator(session);
    if (!emulator)
    {
        session.SendResponse("No emulator selected.");
        return;
    }
    EmulatorContext* context = emulator->GetContext();
    if (!context || !context->pScreen)
    {
        session.SendResponse("Emulator context is not fully initialized.");
        return;
    }

    auto number = [](const std::string& text, unsigned& out) {
        try
        {
            size_t used = 0;
            out = static_cast<unsigned>(std::stoul(text, &used, 0));
            return used == text.size();
        }
        catch (...)
        {
            return false;
        }
    };

    const std::string sub = args.empty() ? "" : args[0];
    if (sub == "temporal")
    {
        HandleVideoTemporal(session, context, args);
        return;
    }
    unsigned a = 0, b = 0, c = 0;
    StateNode report;
    if (sub == "layout" && args.size() == 1)
        report = DeviceState::VideoLayout(context);
    else if (sub == "pixel" && args.size() == 3 && args[1] == "t" && number(args[2], a))
        report = DeviceState::VideoPixelAtBeam(context, a);
    else if (sub == "pixel" && (args.size() == 3 || args.size() == 4) && number(args[1], a) && number(args[2], b) &&
             (args.size() == 3 || number(args[3], c)))
        report = DeviceState::VideoPixel(context, c, a, b);
    else if (sub == "address" && args.size() == 3 && args[1] == "z80" && number(args[2], a))
        report = DeviceState::VideoAddressZ80(context, a);
    else if (sub == "address" && args.size() == 3 && (args[1] == "sprite_ram" || args[1] == "palette" || args[1] == "vram") &&
             number(args[2], a))
        report = DeviceState::VideoAddressIn(context, args[1], 0, a);
    else if (sub == "address" && args.size() == 3 && number(args[1], a) && number(args[2], b))
        report = DeviceState::VideoAddress(context, a, b);
    else if (sub == "text" && args.size() <= 2 && (args.size() == 1 || number(args[1], a)))
        report = DeviceState::VideoText(context, args.size() == 2 ? a : 0);
    else if (sub == "changes" && args.size() <= 2 && (args.size() == 1 || number(args[1], a)))
        report = DeviceState::VideoChanges(context, args.size() == 2 ? a : 2);
    else
    {
        session.SendResponse(kUsage);
        return;
    }
    session.SendResponse(DeviceState::ToText(report));
}

// HandleVideoTemporal — `video temporal [status|list|off|<algorithm>]`: the ZX
// DLSS de-flicker run on every frame before presentation. Mirrors GET / PUT
// /api/v1/emulator/{id}/video/temporal (the same TemporalStatus report).
void CLIProcessor::HandleVideoTemporal(const ClientSession& session, EmulatorContext* context,
                                       const std::vector<std::string>& args)
{
    const std::string op = args.size() >= 2 ? args[1] : "status";
    if (args.size() > 2)
    {
        session.SendResponse("Usage: video temporal [status|list|off|<algorithm>]");
        return;
    }
    if (op == "list")
    {
        std::ostringstream out;
        out << "Temporal algorithms (default " << TemporalStatus::kDefaultAlgorithm << "):" << NEWLINE;
        const std::string current = context->pScreen->GetTemporalAlgorithm();
        for (const std::string& name : TemporalStatus::OfferedAlgorithms())
            out << "  " << name << (name == current ? "  (selected)" : "") << NEWLINE;
        session.SendResponse(out.str());
        return;
    }
    if (op != "status")
    {
        if (!TemporalStatus::Set(context, op))
        {
            session.SendResponse("Unknown temporal algorithm '" + op + "'. Valid: " + TemporalStatus::OfferedList() +
                                 ", off");
            return;
        }
    }
    session.SendResponse(TemporalStatus::Summary(context) + NEWLINE + DeviceState::ToText(TemporalStatus::Report(context)));
}

// HandleFrameCost — halt/active cost of the last frame plus session averages.
// Mirrors GET /api/v1/emulator/{id}/frame_cost.
void CLIProcessor::HandleFrameCost(const ClientSession& session, const std::vector<std::string>& args)
{
    (void)args;  // No parameters

    auto emulator = GetSelectedEmulator(session);
    if (!emulator)
    {
        session.SendResponse("No emulator selected.");
        return;
    }

    EmulatorContext* context = emulator->GetContext();
    if (!context)
    {
        session.SendResponse("Emulator context is not available.");
        return;
    }

    const CONFIG& config = context->config;
    const EmulatorState& state = context->emulatorState;

    const uint64_t frameBudget = static_cast<uint64_t>(config.frame) * state.current_z80_frequency_multiplier;

    // Last completed frame
    const uint64_t lastHalted = state.tstates_halted_last;
    const uint64_t lastActive = frameBudget > lastHalted ? frameBudget - lastHalted : 0;

    // Session averages over all accounted frames
    const uint64_t frames = state.frame_cost_frames;
    const uint64_t totalHalted = state.tstates_halted_total;
    const uint64_t totalActive = state.tstates_frame_total - totalHalted;

    std::stringstream ss;
    ss << std::fixed << std::setprecision(1);
    ss << "Frame cost:" << NEWLINE;
    ss << "  Last frame:" << NEWLINE;
    ss << "    Budget:   " << std::dec << frameBudget << " t-states" << NEWLINE;
    ss << "    Halted:   " << lastHalted << " (" << (frameBudget ? lastHalted * 100.0 / frameBudget : 0.0) << "%)"
       << NEWLINE;
    ss << "    Active:   " << lastActive << " (" << (frameBudget ? lastActive * 100.0 / frameBudget : 0.0) << "%)"
       << NEWLINE;
    ss << "  Session (" << frames << " frames):" << NEWLINE;
    ss << "    Total:    " << state.tstates_frame_total << " t-states" << NEWLINE;
    ss << "    Halted:   " << totalHalted << " (" << (state.tstates_frame_total ? totalHalted * 100.0 / state.tstates_frame_total : 0.0)
       << "%)" << NEWLINE;
    ss << "    Active:   " << totalActive << " ("
       << (state.tstates_frame_total ? totalActive * 100.0 / state.tstates_frame_total : 0.0) << "%)" << NEWLINE;

    session.SendResponse(ss.str());
}

// HandleCoverage — code coverage analyzer control and queries. Mirrors
// POST /coverage/{start,stop,clear} and GET /coverage[,/gaps].
void CLIProcessor::HandleCoverage(const ClientSession& session, const std::vector<std::string>& args)
{
    auto emulator = GetSelectedEmulator(session);
    if (!emulator)
    {
        session.SendResponse("No emulator selected.");
        return;
    }

    auto* context = emulator->GetContext();
    if (!context || !context->pDebugManager)
    {
        session.SendResponse("Debug manager not available.");
        return;
    }

    AnalyzerManager* analyzerManager = context->pDebugManager->GetAnalyzerManager();
    CoverageAnalyzer* coverage = analyzerManager ? analyzerManager->getAnalyzer<CoverageAnalyzer>("coverage") : nullptr;
    if (!coverage || !analyzerManager)
    {
        session.SendResponse("Coverage analyzer not available.");
        return;
    }

    const std::string subcommand = args.empty() ? "status" : args[0];

    if (subcommand == "start")
    {
        // Default: clear recorded data so the session starts fresh
        if (std::find(args.begin(), args.end(), "--keep") == args.end())
            coverage->clear();

        if (!analyzerManager->activate("coverage"))
        {
            session.SendResponse("Failed to activate coverage analyzer.");
            return;
        }
        session.SendResponse("Coverage session started (recording executed addresses).");
        return;
    }

    if (subcommand == "stop")
    {
        const bool deactivated = analyzerManager->deactivate("coverage");
        session.SendResponse(deactivated ? "Coverage session stopped. Data kept for queries."
                                         : "Coverage analyzer was not active.");
        return;
    }

    if (subcommand == "clear")
    {
        coverage->clear();
        session.SendResponse("Coverage data cleared.");
        return;
    }

    if (subcommand == "gaps")
    {
        uint16_t start = 0x4000;
        uint16_t end = 0xFFFF;
        if (args.size() >= 3)
        {
            if (!parseAddress16(args[1], start) || !parseAddress16(args[2], end) || start > end)
            {
                session.SendResponse("Invalid range. Usage: coverage gaps [<start> <end>]");
                return;
            }
        }

        auto gaps = coverage->getGaps(start, end, 50);
        std::stringstream ss;
        ss << std::hex << std::uppercase << std::setfill('0');
        ss << "Coverage gaps in $";
        ss << std::setw(4) << start << "-$" << std::setw(4) << end << " (" << std::dec << gaps.size()
           << (gaps.size() >= 50 ? "+" : "") << " shown):" << NEWLINE;
        for (const auto& gap : gaps)
        {
            ss << "  $" << std::setw(4) << gap.first << "-$" << std::setw(4) << gap.second << std::dec << " ("
               << (gap.second - gap.first + 1) << " bytes)" << NEWLINE;
        }
        session.SendResponse(ss.str());
        return;
    }

    // status (default): summary + executed ranges
    const size_t executedCount = coverage->getExecutedCount();
    auto ranges = coverage->getExecutedRanges(100);

    std::stringstream ss;
    ss << std::hex << std::uppercase << std::setfill('0');
    ss << "Coverage:" << NEWLINE;
    ss << "  Active: " << (analyzerManager->isActive("coverage") ? "yes" : "no") << NEWLINE;
    ss << "  Recording: " << (coverage->isRecording() ? "yes" : "no") << NEWLINE;
    ss << std::dec;
    ss << "  Executed addresses: " << executedCount << " / 65536 ("
       << (executedCount * 100.0 / 65536.0) << "%)" << NEWLINE;
    ss << "  Instructions retired: " << coverage->getInstructionCount() << NEWLINE;
    ss << "  Executed ranges (" << ranges.size() << (ranges.size() >= 100 ? "+" : "") << " shown):" << NEWLINE;
    for (const auto& range : ranges)
    {
        ss << "    $" << std::setw(4) << range.first << "-$" << std::setw(4) << range.second << std::dec << " ("
           << (range.second - range.first + 1) << " bytes)" << NEWLINE;
    }

    session.SendResponse(ss.str());
}

// HandleAyLog — AY register-write logging. Mirrors POST/GET /ay/log.
void CLIProcessor::HandleAyLog(const ClientSession& session, const std::vector<std::string>& args)
{
    auto emulator = GetSelectedEmulator(session);
    if (!emulator)
    {
        session.SendResponse("No emulator selected.");
        return;
    }

    auto* context = emulator->GetContext();
    if (!context || !context->pDebugManager)
    {
        session.SendResponse("Debug manager not available.");
        return;
    }

    AnalyzerManager* analyzerManager = context->pDebugManager->GetAnalyzerManager();
    AYLogAnalyzer* aylog = analyzerManager ? analyzerManager->getAnalyzer<AYLogAnalyzer>("aylog") : nullptr;
    if (!aylog || !analyzerManager)
    {
        session.SendResponse("AY log analyzer not available.");
        return;
    }

    const std::string subcommand = args.empty() ? "status" : args[0];

    if (subcommand == "start")
    {
        size_t capacity = 4096;
        if (args.size() >= 2)
        {
            try
            {
                capacity = std::stoul(args[1]);
            }
            catch (...)
            {
                session.SendResponse("Invalid capacity. Usage: aylog start [capacity]");
                return;
            }
        }

        if (!analyzerManager->activate("aylog"))
        {
            session.SendResponse("Failed to activate AY log analyzer.");
            return;
        }
        aylog->setCapacity(capacity);

        std::stringstream ss;
        ss << "AY logging started (capacity " << std::dec << capacity << " records).";
        session.SendResponse(ss.str());
        return;
    }

    if (subcommand == "stop")
    {
        const bool deactivated = analyzerManager->deactivate("aylog");
        session.SendResponse(deactivated ? "AY logging stopped. Records kept for queries."
                                         : "AY log analyzer was not active.");
        return;
    }

    if (subcommand == "clear")
    {
        aylog->clear();
        session.SendResponse("AY log records cleared.");
        return;
    }

    if (subcommand == "dump")
    {
        size_t count = 20;
        if (args.size() >= 2)
        {
            try
            {
                count = std::stoul(args[1]);
            }
            catch (...)
            {
                session.SendResponse("Invalid count. Usage: aylog dump [count]");
                return;
            }
        }

        const size_t total = aylog->getEntryCount();
        const size_t offset = total > count ? total - count : 0;
        auto records = aylog->getEntries(offset, count);

        std::stringstream ss;
        ss << std::hex << std::uppercase << std::setfill('0');
        ss << "AY log (last " << std::dec << records.size() << " of " << total << " records):" << NEWLINE;
        ss << std::hex << std::setfill('0');
        for (const auto& record : records)
        {
            const char* type = record.port == 0xFFFD ? (record.value > 0x0F ? "switch" : "select") : "write";
            ss << "  #" << std::dec << record.frame << " t=" << record.tacts << " pc=$" << std::setw(4) << record.pc
               << " port=$" << std::setw(4) << record.port << " chip=" << static_cast<int>(record.chip) << " " << type
               << " reg=" << static_cast<int>(record.reg) << " val=$" << std::setw(2)
               << static_cast<int>(record.value) << NEWLINE;
        }

        session.SendResponse(ss.str());
        return;
    }

    // status (default)
    std::stringstream ss;
    ss << std::dec;
    ss << "AY log:" << NEWLINE;
    ss << "  Active: " << (analyzerManager->isActive("aylog") ? "yes" : "no") << NEWLINE;
    ss << "  Recording: " << (aylog->isRecording() ? "yes" : "no") << NEWLINE;
    ss << "  Entries: " << aylog->getEntryCount() << " / " << aylog->getCapacity() << NEWLINE;
    ss << "  Dropped: " << aylog->getDroppedCount() << NEWLINE;

    session.SendResponse(ss.str());
}

// HandleMixer — the per-device mixer (cli-audio-mixer.h; mirrors GET / PUT /audio/mixer)
void CLIProcessor::HandleMixer(const ClientSession& session, const std::vector<std::string>& args)
{
    auto emulator = GetSelectedEmulator(session);
    if (!emulator)
    {
        session.SendResponse("No emulator selected.");
        return;
    }
    std::vector<std::string> full = {"mixer"};
    full.insert(full.end(), args.begin(), args.end());
    session.SendResponse(CliAudioMixer::Text(emulator->GetContext(), full, NEWLINE));
}

// HandleAudioCapture — buffered stereo capture via AudioCaptureAnalyzer.
// Mirrors POST /audio/capture and GET /audio/capture/{status,result}.
void CLIProcessor::HandleAudioCapture(const ClientSession& session, const std::vector<std::string>& args)
{
    auto emulator = GetSelectedEmulator(session);
    if (!emulator)
    {
        session.SendResponse("No emulator selected.");
        return;
    }

    auto* context = emulator->GetContext();
    if (!context || !context->pDebugManager)
    {
        session.SendResponse("Debug manager not available.");
        return;
    }

    AnalyzerManager* analyzerManager = context->pDebugManager->GetAnalyzerManager();
    AudioCaptureAnalyzer* capture =
        analyzerManager ? analyzerManager->getAnalyzer<AudioCaptureAnalyzer>("audiocapture") : nullptr;
    if (!capture || !analyzerManager)
    {
        session.SendResponse("Audio capture analyzer not available.");
        return;
    }

    const std::string subcommand = args.empty() ? "status" : args[0];

    if (subcommand == "start")
    {
        double seconds = 1.0;
        if (args.size() >= 2)
        {
            try
            {
                seconds = std::stod(args[1]);
            }
            catch (...)
            {
                session.SendResponse("Invalid duration. Usage: audiocapture start <seconds> [source]");
                return;
            }
        }
        if (seconds < 0.01 || seconds > 30.0)
        {
            session.SendResponse("Duration must be within [0.01, 30.0] seconds.");
            return;
        }

        // One mixer device instead of the master mix (core AudioMixer::Capturable; GET /audio/mixer lists them)
        AudioSourceType source = AudioSourceType::MasterMix;
        std::string sourceError;
        if (!AudioMixer::Capturable(context, args.size() >= 3 ? args[2] : std::string(), source, sourceError))
        {
            session.SendResponse("Error: " + sourceError);
            return;
        }

        const size_t rate = context->pSoundManager ? context->pSoundManager->getCoreRate() : 44100;
        const size_t target = static_cast<size_t>(seconds * static_cast<double>(rate)) * 2;  // interleaved stereo

        analyzerManager->activate("audiocapture");
        capture->startCapture(target, source);

        std::stringstream ss;
        ss << std::dec << "Audio capture armed: " << seconds << "s (" << target << " stereo samples @ " << rate
           << " Hz), source " << AudioMixer::Key(source) << ".";
        session.SendResponse(ss.str());
        return;
    }

    if (subcommand == "stop")
    {
        capture->stopCapture();
        analyzerManager->deactivate("audiocapture");
        session.SendResponse("Audio capture stopped. Use 'audiocapture result' for stats.");
        return;
    }

    if (subcommand == "clear")
    {
        capture->clearCapture();
        session.SendResponse("Audio capture buffer cleared.");
        return;
    }

    if (subcommand == "result" || subcommand == "save")
    {
        const auto& buffer = capture->getBuffer();
        const size_t frames = buffer.size() / 2;

        if (frames == 0)
        {
            session.SendResponse("No captured audio — start a capture with 'audiocapture start <seconds>' first.");
            return;
        }

        const size_t rate = context->pSoundManager ? context->pSoundManager->getCoreRate() : 44100;

        // Stereo peak/RMS statistics (same math as the WebAPI result endpoint)
        double peak[2] = {0.0, 0.0};
        double sumSquares[2] = {0.0, 0.0};
        for (size_t frame = 0; frame < frames; frame++)
        {
            for (int channel = 0; channel < 2; channel++)
            {
                const double normalized = static_cast<double>(buffer[frame * 2 + channel]) / 32768.0;
                const double magnitude = std::fabs(normalized);
                if (magnitude > peak[channel])
                    peak[channel] = magnitude;
                sumSquares[channel] += normalized * normalized;
            }
        }

        std::stringstream ss;
        ss << std::fixed << std::setprecision(4) << std::dec;
        ss << "Audio capture:" << NEWLINE;
        ss << "  Frames: " << frames << " @ " << rate << " Hz ("
           << (static_cast<double>(frames) / rate) << "s)" << NEWLINE;
        ss << "  Complete: " << (capture->isCaptureComplete() ? "yes" : "no") << NEWLINE;
        ss << "  Left:  peak " << peak[0] << "  rms " << std::sqrt(sumSquares[0] / frames) << NEWLINE;
        ss << "  Right: peak " << peak[1] << "  rms " << std::sqrt(sumSquares[1] / frames) << NEWLINE;

        // Optional WAV export: audiocapture save <path.wav>
        if (subcommand == "save")
        {
            if (args.size() < 2)
            {
                session.SendResponse("Usage: audiocapture save <path.wav>");
                return;
            }

            const std::string& path = args[1];
            TinyWav wav{};
            if (tinywav_open_write(&wav, 2, static_cast<int32_t>(rate), TW_INT16, TW_INTERLEAVED, path.c_str()) != 0)
            {
                session.SendResponse("Failed to open WAV file for writing: " + path);
                return;
            }
            tinywav_write_i(&wav, const_cast<void*>(static_cast<const void*>(buffer.data())),
                            static_cast<int>(frames));
            tinywav_close_write(&wav);

            ss << "  Saved: " << path << NEWLINE;
        }

        session.SendResponse(ss.str());
        return;
    }

    // status (default)
    std::stringstream ss;
    ss << std::dec;
    ss << "Audio capture:" << NEWLINE;
    ss << "  Armed: " << (capture->isCaptureArmed() ? "yes" : "no") << NEWLINE;
    ss << "  Complete: " << (capture->isCaptureComplete() ? "yes" : "no") << NEWLINE;
    ss << "  Samples: " << capture->getCapturedSamples() << " / " << capture->getTargetSamples() << NEWLINE;

    session.SendResponse(ss.str());
}

// HandleVideoRecord — video recording via RecordingManager. Mirrors
// POST /video/record and GET /video/record/status.
void CLIProcessor::HandleVideoRecord(const ClientSession& session, const std::vector<std::string>& args)
{
#ifdef ENABLE_RECORDING
    auto emulator = GetSelectedEmulator(session);
    if (!emulator)
    {
        session.SendResponse("No emulator selected.");
        return;
    }

    EmulatorContext* context = emulator->GetContext();
    RecordingManager* rm = context ? context->pRecordingManager : nullptr;
    if (!rm)
    {
        session.SendResponse("Recording manager not available for this emulator.");
        return;
    }

    const std::string subcommand = args.empty() ? "status" : args[0];

    if (subcommand == "start")
    {
        if (rm->IsRecording() || rm->IsPaused())
        {
            session.SendResponse("A recording is already active — stop it first.");
            return;
        }

        // videorecord start [format] [filename] [--fps N] [--scale N] [--audio-rate N|auto]
        //                   [--audio CODEC] [--video-bitrate KBPS] [--audio-bitrate KBPS]
        CliVideoRecord::StartOptions options;
        std::string optionError;
        if (!CliVideoRecord::ParseStart(args, options, optionError))
        {
            session.SendResponse(optionError);
            return;
        }

        if (options.filename.empty())
            options.filename = defaultRecordingPath(emulator->GetId(), CliVideoRecord::DefaultExtension(options.format));

        // Audio codec vs container (gif has no audio) and bitrate ranges, before anything changes
        if (!CliVideoRecord::Validate(options, optionError))
        {
            session.SendResponse(optionError);
            return;
        }

        const std::string& format = options.format;
        const std::string& filename = options.filename;
        const float fps = options.fps;
        const uint32_t scale = options.scale;
        const bool hasAudioRate = options.hasAudioRate;
        const uint32_t audioRate = options.audioRate;

        // Configuration setters refuse changes mid-recording — apply while idle
        rm->SetVideoFrameRate(fps);
        rm->SetScaleFactor(scale);

        // Optional core-rate pin before the first sample is stamped: the
        // recording must start (and stay) at the requested audio rate. The
        // pin applies at the next frame boundary - wait for it so a paused
        // emulator fails here instead of producing a mislabeled file.
        if (hasAudioRate)
        {
            SoundManager* sound = context->pSoundManager;
            if (!sound)
            {
                session.SendResponse("Sound manager not available - cannot pin the audio rate.");
                return;
            }

            sound->setCoreRatePin(audioRate);
            for (int attempt = 0; attempt < 100 && sound->getCoreRate() != sound->getTargetCoreRate(); attempt++)
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
            if (sound->getCoreRate() != sound->getTargetCoreRate())
            {
                session.SendResponse("Core rate change not applied within 1s (emulator paused or stuck) - "
                                     "resume the emulator or set 'setting audio_rate' before pausing.");
                return;
            }
        }

        if (!rm->StartRecording(filename, format, options.audio, options.videoBitrate, options.audioBitrate))
        {
            session.SendResponse("Failed to start recording: " + rm->GetLastRecordingError());
            return;
        }

        std::stringstream ss;
        ss << std::dec << "Recording started: " << filename << " (" << format << ", " << fps << " fps, x" << scale;
        if (rm->HasAudio())
            ss << ", audio " << rm->GetAudioCodec() << " " << rm->GetAudioSampleRate() << " Hz "
               << rm->GetAudioChannels() << " ch";
        else
            ss << ", no audio";
        if (context->pSoundManager)
            ss << ", core " << context->pSoundManager->getCoreRate() << " Hz";
        ss << ")";
        session.SendResponse(ss.str());
        return;
    }

    if (subcommand == "stop")
    {
        if (!rm->IsRecording() && !rm->IsPaused())
        {
            session.SendResponse("No recording is active.");
            return;
        }

        rm->StopRecording();
        const RecordingManager::RecordingStats stats = rm->GetStats();
        std::stringstream ss;
        ss << std::dec << "Recording stopped: " << rm->GetOutputFilename() << " (" << stats.framesRecorded
           << " frames";
        if (rm->HasAudio())
            ss << ", " << stats.audioSamplesRecorded << " audio samples, " << std::fixed << std::setprecision(2)
               << rm->GetAudioDuration() << " s audio";
        ss << ")";
        session.SendResponse(ss.str());
        return;
    }

    if (subcommand == "pause")
    {
        if (!rm->IsRecording())
        {
            session.SendResponse("No active recording to pause.");
            return;
        }

        rm->PauseRecording();
        session.SendResponse("Recording paused.");
        return;
    }

    if (subcommand == "resume")
    {
        if (!rm->IsPaused())
        {
            session.SendResponse("No paused recording to resume.");
            return;
        }

        rm->ResumeRecording();
        session.SendResponse("Recording resumed.");
        return;
    }

    // status (default)
    std::stringstream ss;
    ss << std::dec;
    ss << "Video recording:" << NEWLINE;
    ss << "  Recording: " << (rm->IsRecording() ? "yes" : "no") << NEWLINE;
    ss << "  Paused: " << (rm->IsPaused() ? "yes" : "no") << NEWLINE;
    const std::string& output = rm->GetOutputFilename();
    if (!output.empty())
        ss << "  Output: " << output << NEWLINE;
    const RecordingManager::RecordingStats stats = rm->GetStats();
    ss << "  Frames: " << stats.framesRecorded << NEWLINE;
    if (!rm->GetVideoCodec().empty())
        ss << "  Video codec: " << rm->GetVideoCodec() << NEWLINE;
    if (rm->HasAudio())
    {
        ss << "  Audio: " << rm->GetAudioCodec() << ", " << rm->GetAudioSampleRate() << " Hz, "
           << rm->GetAudioChannels() << " ch" << NEWLINE;
        ss << "  Audio samples: " << stats.audioSamplesRecorded << " (" << std::fixed << std::setprecision(2)
           << rm->GetAudioDuration() << " s)" << NEWLINE;
    }
    else
    {
        ss << "  Audio: none" << NEWLINE;
    }

    session.SendResponse(ss.str());
#else
    (void)args;
    session.SendResponse("Video recording is disabled in this build (ENABLE_RECORDING=OFF).");
#endif
}
