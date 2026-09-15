// CLI Analysis Commands — Phase-2 analysis capabilities shared with WebAPI/MCP:
// screen digest, beam position, frame cost, coverage, AY log, audio capture,
// video recording. Mirrors the WebAPI handlers (analyzers_api, profiler_api,
// state_screen_api, recording_api) so every interface exposes the same features.

#include "cli-processor.h"

#include <3rdparty/tinywav/tinywav.h>
#include <debugger/analyzers/analyzermanager.h>
#include <debugger/analyzers/audiocapture/audiocaptureanalyzer.h>
#include <debugger/analyzers/aylog/ayloganalyzer.h>
#include <debugger/analyzers/coverage/coverageanalyzer.h>
#include <debugger/debugmanager.h>
#include <emulator/cpu/z80.h>
#include <emulator/emulator.h>
#include <emulator/emulatormanager.h>
#include <emulator/emulatorcontext.h>
#include <emulator/memory/memory.h>
#include <emulator/platform.h>
#include <emulator/sound/soundmanager.h>
#include <emulator/video/screendigest.h>

#include <algorithm>
#include <cmath>
#include <ctime>
#include <filesystem>
#include <iomanip>
#include <sstream>

#ifdef ENABLE_RECORDING
#include "recordingmanager.h"
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

    const CONFIG& config = context->config;
    EmulatorState& state = context->emulatorState;
    Memory* memory = context->pMemory;

    const bool is128K =
        (config.mem_model == MM_SPECTRUM128 || config.mem_model == MM_PENTAGON || config.mem_model == MM_PLUS3);

    // Arguments: explicit Z80 range ("digest <start> <end>"), bank list, border toggle
    uint16_t rangeStart = 0;
    uint16_t rangeEnd = 0;
    std::vector<uint16_t> banks;
    bool includeBorder = true;

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
                    banks.push_back(page);
            }
        }
        else if (args[i] == "--no-border")
        {
            includeBorder = false;
        }
        else if (i + 1 < args.size() && banks.empty())
        {
            // Positional pair: start end
            if (!parseAddress16(args[i], rangeStart) || !parseAddress16(args[i + 1], rangeEnd) || rangeStart > rangeEnd)
            {
                session.SendResponse("Invalid range. Usage: digest [<start> <end>] [--banks p1,p2] [--no-border]");
                return;
            }
            i++;
        }
    }

    const uint64_t previousDigest = state.last_screen_digest;
    const uint64_t previousFrame = state.last_screen_digest_frame;

    uint64_t combined = ScreenDigest::kInitialValue;
    std::stringstream ss;
    ss << std::hex << std::uppercase << std::setfill('0');

    if (rangeEnd >= rangeStart && (rangeStart != 0 || rangeEnd != 0))
    {
        // Explicit Z80 range mode
        const uint64_t rangeDigest = ScreenDigest::DigestZ80Range(memory, rangeStart, rangeEnd);
        for (int shift = 0; shift < 64; shift += 8)
        {
            combined = ScreenDigest::MixValue(combined, static_cast<uint8_t>((rangeDigest >> shift) & 0xFF));
        }

        ss << "Range:  $";
        ss << std::setw(4) << rangeStart << "-$" << std::setw(4) << rangeEnd << NEWLINE;
        ss << "Digest: 0x" << std::setw(16) << rangeDigest << NEWLINE;
    }
    else
    {
        // Bank mode: both screen pages on 128K-class models, page 5 only otherwise
        if (banks.empty())
        {
            banks.push_back(ScreenDigest::kScreen0RAMPage);
            if (is128K)
                banks.push_back(ScreenDigest::kScreen1RAMPage);
        }

        for (uint16_t page : banks)
        {
            const uint64_t digest = ScreenDigest::DigestRAMPage(memory, page);
            for (int shift = 0; shift < 64; shift += 8)
            {
                combined = ScreenDigest::MixValue(combined, static_cast<uint8_t>((digest >> shift) & 0xFF));
            }
            ss << "Page " << std::dec << std::setw(2) << page << std::hex << std::uppercase << ": 0x" << std::setw(16)
               << digest << NEWLINE;
        }
    }

    uint8_t borderColor = 0;
    if (includeBorder)
    {
        borderColor = context->pScreen->GetBorderColor();
        combined = ScreenDigest::MixValue(combined, borderColor);
    }

    state.last_screen_digest = combined;
    state.last_screen_digest_frame = state.frame_counter;

    ss << "Combined: 0x" << std::setw(16) << combined << NEWLINE;
    ss << "Algorithm: fnv1a-64" << NEWLINE;
    ss << "Frame: " << std::dec << state.frame_counter << NEWLINE;
    ss << "Border: " << static_cast<int>(borderColor) << (includeBorder ? "" : " (excluded)") << NEWLINE;
    ss << "Changed: " << (combined != previousDigest ? "yes" : "no") << NEWLINE;
    if (previousFrame != 0)
    {
        ss << "Previous: 0x" << std::hex << std::setw(16) << previousDigest << " (frame " << std::dec << previousFrame
           << ", " << (state.frame_counter - previousFrame) << " frames ago)" << NEWLINE;
    }

    session.SendResponse(ss.str());
}

// HandleBeam — current raster beam position and zone. Mirrors
// GET /api/v1/emulator/{id}/video/beam.
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

    const CONFIG& config = context->config;
    Screen* screen = context->pScreen;

    if (config.t_line == 0 || config.frame == 0)
    {
        session.SendResponse("Machine model timing is not initialized yet.");
        return;
    }

    Z80* cpu = context->pCore ? context->pCore->GetZ80() : nullptr;
    const uint32_t tstate = cpu ? static_cast<uint32_t>(cpu->t) : screen->GetCurrentTstate();
    const uint32_t tInFrame = tstate % config.frame;

    const VideoModeEnum mode = screen->GetVideoMode();
    const RasterDescriptor& rd = screen->rasterDescriptors[mode];
    const RasterState& rs = screen->GetRasterState();

    const bool rasterValid = rs.tstatesPerLine != 0;
    const uint32_t tstatesPerLine = rasterValid ? rs.tstatesPerLine : config.t_line;
    const uint32_t line = tInFrame / tstatesPerLine;
    const uint32_t dotInLine = tInFrame % tstatesPerLine;
    const uint32_t beamX = dotInLine * rs.pixelsPerTState;

    // Vertical zone (frame-relative), then horizontal zone inside screen rows
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

    std::string zone = vZone;
    if (vZone == "screen")
        zone = (hZone == "paper") ? "paper" : (hZone == "hblank" ? "hblank" : "border");

    std::stringstream ss;
    ss << std::dec;
    ss << "Beam position:" << NEWLINE;
    ss << "  T-state: " << tstate << " (in frame: " << tInFrame << ")" << NEWLINE;
    ss << "  Frame: " << context->emulatorState.frame_counter << NEWLINE;
    ss << "  Line: " << line << "  Dot: " << dotInLine << NEWLINE;
    ss << "  Beam X/Y: " << beamX << "/" << line << NEWLINE;
    ss << "  Zone: " << zone << " (v: " << vZone << ", h: " << hZone << ")" << NEWLINE;
    ss << "  Video mode: " << Screen::GetVideoModeName(mode) << NEWLINE;

    session.SendResponse(ss.str());
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
                session.SendResponse("Invalid duration. Usage: audiocapture start <seconds>");
                return;
            }
        }
        if (seconds < 0.01 || seconds > 30.0)
        {
            session.SendResponse("Duration must be within [0.01, 30.0] seconds.");
            return;
        }

        const size_t rate = context->pSoundManager ? context->pSoundManager->getCoreRate() : 44100;
        const size_t target = static_cast<size_t>(seconds * static_cast<double>(rate)) * 2;  // interleaved stereo

        analyzerManager->activate("audiocapture");
        capture->startCapture(target);

        std::stringstream ss;
        ss << std::dec << "Audio capture armed: " << seconds << "s (" << target << " stereo samples @ " << rate
           << " Hz).";
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

        // videorecord start [format] [filename] [--fps N] [--scale N]
        std::string format = "gif";
        std::string filename;
        float fps = 50.0f;
        uint32_t scale = 1;

        for (size_t i = 1; i < args.size(); i++)
        {
            if (args[i] == "--fps" && i + 1 < args.size())
            {
                try
                {
                    fps = std::stof(args[++i]);
                }
                catch (...)
                {
                    session.SendResponse("Invalid fps value.");
                    return;
                }
                if (fps < 1.0f) fps = 1.0f;
                if (fps > 100.0f) fps = 100.0f;
            }
            else if (args[i] == "--scale" && i + 1 < args.size())
            {
                try
                {
                    scale = std::stoul(args[++i]);
                }
                catch (...)
                {
                    session.SendResponse("Invalid scale value.");
                    return;
                }
                if (scale < 1) scale = 1;
                if (scale > 4) scale = 4;
            }
            else if (filename.empty() && format == "gif")
            {
                format = args[i];
            }
            else if (filename.empty())
            {
                filename = args[i];
            }
        }

        // Map codec-style names to container extensions for the default filename
        std::string extension = format;
        if (format == "h264" || format == "h265" || format == "hevc" || format == "vp9")
            extension = "mkv";
        else if (format == "rawvideo")
            extension = "avi";

        if (filename.empty())
            filename = defaultRecordingPath(emulator->GetId(), extension);

        // Configuration setters refuse changes mid-recording — apply while idle
        rm->SetVideoFrameRate(fps);
        rm->SetScaleFactor(scale);

        if (!rm->StartRecording(filename, format, ""))
        {
            session.SendResponse("Failed to start recording: " + rm->GetLastRecordingError());
            return;
        }

        std::stringstream ss;
        ss << std::dec << "Recording started: " << filename << " (" << format << ", " << fps << " fps, x" << scale
           << ")";
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
        session.SendResponse("Recording stopped: " + rm->GetOutputFilename());
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

    session.SendResponse(ss.str());
#else
    (void)args;
    session.SendResponse("Video recording is disabled in this build (ENABLE_RECORDING=OFF).");
#endif
}
