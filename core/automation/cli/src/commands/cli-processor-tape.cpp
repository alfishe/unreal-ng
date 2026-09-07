#include <base/featuremanager.h>
#include <emulator/emulator.h>
#include <emulator/emulatorcontext.h>
#include <emulator/io/tape/tapecatalog.h>
#include <emulator/io/tape/tape.h>
#include <emulator/platform.h>
#include <tapeaudio/tapeaudioimporter.h>
#include <tapeaudio/tapeaudiorenderer.h>

#include <algorithm>
#include <cstring>
#include <iomanip>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "cli-processor.h"
#include "common/timehelper.h"

/// region <Local helpers>

namespace
{
/// Pause() -> op -> Resume() bracket shared by every tape handler (design
/// §7.1). Pause only when actually running; Resume exactly then. RAII so an
/// early return can never leave the emulator parked.
class EmulatorPauseBracket
{
public:
    explicit EmulatorPauseBracket(const std::shared_ptr<Emulator>& emulator)
        : _emulator(emulator), _wasRunning(emulator && emulator->IsRunning() && !emulator->IsPaused())
    {
        if (_wasRunning)
        {
            _emulator->Pause();
            sleep_ms(10);  // Give emulator time to pause
        }
    }

    ~EmulatorPauseBracket()
    {
        if (_wasRunning)
        {
            _emulator->Resume();
        }
    }

private:
    std::shared_ptr<Emulator> _emulator;
    bool _wasRunning;
};

const char* PlaybackStateName(TapePlaybackState state)
{
    switch (state)
    {
        case TapePlaybackState::Idle:
            return "idle";
        case TapePlaybackState::Playing:
            return "playing";
        case TapePlaybackState::Paused:
            return "paused";
        case TapePlaybackState::Ended:
            return "ended";
        default:
            break;
    }
    return "unknown";
}

/// Compact `FAST` column words (design §7.1): "yes", "-" for structural
/// entries, or a one-word reject reason
const char* FastLoadShortReason(FastLoadRejectEnum reason)
{
    switch (reason)
    {
        case FastLoadRejectEnum::None:
            return "yes";
        case FastLoadRejectEnum::ControlBlock:
            return "-";
        case FastLoadRejectEnum::NonStandardTiming:
            return "turbo";
        case FastLoadRejectEnum::PulseStream:
            return "pulses";
        case FastLoadRejectEnum::NonStandardFlag:
            return "flag";
        case FastLoadRejectEnum::ChecksumInvalid:
            return "cksum";
        case FastLoadRejectEnum::Headerless:
            return "hdrless";
        case FastLoadRejectEnum::Unplayable:
            return "n/a";
        case FastLoadRejectEnum::ControlFlowInert:
            return "inert";
        default:
            break;
    }
    return "?";
}

/// `SPEED` column: profile word + baud estimate where one exists
std::string FormatSpeedColumn(const TapeBlockDescriptor& descriptor)
{
    if (descriptor.kind == TapeBlockKindEnum::Control)
        return "-";

    if (descriptor.timing.profile == TapeSpeedProfileEnum::PulseStream)
        return "pulse";

    std::string result = descriptor.timing.profile == TapeSpeedProfileEnum::Custom ? "Turbo" : "Std";
    if (descriptor.baudEstimate > 0)
    {
        result += " " + std::to_string(descriptor.baudEstimate) + " bps";
    }
    return result;
}

/// `HEADER` column: YES only when a header is provided — i.e. byte-payload
/// blocks paired with a valid ROM header (only those can be headerless)
std::string FormatHeaderColumn(const TapeBlockDescriptor& descriptor)
{
    if (descriptor.kind == TapeBlockKindEnum::Data || descriptor.kind == TapeBlockKindEnum::Custom)
        return descriptor.headerless ? "no" : "yes";
    return "-";
}

/// `CKSUM` column: "n/a" when there is no byte payload to checksum
std::string FormatChecksumColumn(const TapeBlockDescriptor& descriptor)
{
    if (descriptor.rawSize == 0)
        return "n/a";
    return descriptor.checksumValid ? "ok" : "err";
}

/// One-line position snapshot shared by `tape pos`, `tape seek` and `tape info`
void AppendPositionLine(std::stringstream& ss, Tape& tape)
{
    ss << "State: " << PlaybackStateName(tape.GetPlaybackState());

    std::optional<TapePosition> position = tape.GetPosition();
    if (position.has_value())
    {
        ss << ", block: " << position->blockIndex << "/" << tape.GetBlocks().size() << ", pulse: " << position->pulseIndex
           << ", time: " << std::fixed << std::setprecision(1) << position->secondsIntoBlock << "s of "
           << position->blockTotalSeconds << "s";
    }

    ss << ", cursor: " << tape.GetConsumptionCursor();
}
}  // namespace

/// endregion </Local helpers>

/// region <Tape Control Commands>

void CLIProcessor::HandleTape(const ClientSession& session, const std::vector<std::string>& args)
{
    // File-conversion subcommands first (tape-audio-bridge design §7.1):
    // render/import are pure path-to-path operations that never touch
    // emulator state, so they run before the selected-emulator requirement
    // every playback subcommand below shares.
    if (!args.empty())
    {
        std::string first = args[0];
        std::transform(first.begin(), first.end(), first.begin(), ::tolower);
        if (first == "render")
        {
            HandleTapeRender(session, args);
            return;
        }
        if (first == "import")
        {
            HandleTapeImport(session, args);
            return;
        }
    }

    // Get the selected emulator
    auto emulator = GetSelectedEmulator(session);
    if (!emulator)
    {
        session.SendResponse(std::string("Error: No emulator selected.") + NEWLINE);
        return;
    }

    // Get emulator context
    EmulatorContext* context = emulator->GetContext();
    if (!context)
    {
        session.SendResponse(std::string("Error: Unable to access emulator context.") + NEWLINE);
        return;
    }

    // If no arguments, show usage
    if (args.empty())
    {
        std::stringstream ss;
        ss << "Usage: tape <subcommand> [args]" << NEWLINE;
        ss << NEWLINE;
        ss << "Available subcommands:" << NEWLINE;
        ss << "  load <file>   - Load tape image (.tap, .tzx)" << NEWLINE;
        ss << "  eject         - Eject current tape" << NEWLINE;
        ss << "  play          - Start (or resume in place) tape playback" << NEWLINE;
        ss << "  pause         - Pause playback; the next play resumes in place" << NEWLINE;
        ss << "  stop          - Stop tape playback" << NEWLINE;
        ss << "  rewind        - Rewind to block 0 (image kept)" << NEWLINE;
        ss << "  seek <index>  - Position the tape at block <index>" << NEWLINE;
        ss << "  pos           - One-line playback position" << NEWLINE;
        ss << "  blocks        - Block catalog table" << NEWLINE;
        ss << "  info          - Detailed tape status" << NEWLINE;
        ss << "  render <img> [opts] -o <out> - Render tape image to WAV/FLAC audio" << NEWLINE;
        ss << "  import <snd> [opts] -o <out> - Import WAV/FLAC/MP3 as .tzx/.tap" << NEWLINE;
        ss << NEWLINE;
        ss << "Examples:" << NEWLINE;
        ss << "  tape load /path/to/game.tap" << NEWLINE;
        ss << "  tape play" << NEWLINE;
        ss << "  tape seek 4" << NEWLINE;
        ss << "  tape blocks" << NEWLINE;
        ss << "  tape info" << NEWLINE;
        ss << "  tape render game.tzx --blocks 2-5 --rate 48000 -o game.wav" << NEWLINE;
        ss << "  tape import recording.wav --target tzx -o imported.tzx" << NEWLINE;

        session.SendResponse(ss.str());
        return;
    }

    std::string subcommand = args[0];
    std::transform(subcommand.begin(), subcommand.end(), subcommand.begin(), ::tolower);

    // Dispatch to subcommand handlers
    if (subcommand == "load")
    {
        HandleTapeLoad(session, emulator, context, args);
    }
    else if (subcommand == "eject")
    {
        HandleTapeEject(session, emulator, context);
    }
    else if (subcommand == "play")
    {
        HandleTapePlay(session, emulator, context);
    }
    else if (subcommand == "pause")
    {
        HandleTapePause(session, emulator, context);
    }
    else if (subcommand == "stop")
    {
        HandleTapeStop(session, emulator, context);
    }
    else if (subcommand == "rewind")
    {
        HandleTapeRewind(session, emulator, context);
    }
    else if (subcommand == "seek")
    {
        HandleTapeSeek(session, emulator, context, args);
    }
    else if (subcommand == "pos")
    {
        HandleTapePos(session, emulator, context);
    }
    else if (subcommand == "blocks")
    {
        HandleTapeBlocks(session, emulator, context);
    }
    else if (subcommand == "info")
    {
        HandleTapeInfo(session, emulator, context);
    }
    else
    {
        session.SendResponse(std::string("Error: Unknown subcommand '") + args[0] + "'" + NEWLINE +
                             "Use 'tape' without arguments to see available subcommands." + NEWLINE);
    }
}

void CLIProcessor::HandleTapeLoad(const ClientSession& session, std::shared_ptr<Emulator> emulator,
                                  EmulatorContext* context, const std::vector<std::string>& args)
{
    if (args.size() < 2)
    {
        session.SendResponse(std::string("Error: Missing file path") + NEWLINE + "Usage: tape load <file>" + NEWLINE);
        return;
    }

    std::string filepath = args[1];

    // Use existing LoadTape method (already handles file loading)
    bool success = emulator->LoadTape(filepath);

    if (success)
    {
        session.SendResponse(std::string("Tape loaded: ") + filepath + NEWLINE);
    }
    else
    {
        session.SendResponse(std::string("Error: Failed to load tape: ") + filepath + NEWLINE);
    }
}

void CLIProcessor::HandleTapeEject(const ClientSession& session, std::shared_ptr<Emulator> emulator,
                                   EmulatorContext* context)
{
    if (!context->pTape)
    {
        session.SendResponse(std::string("Error: Tape subsystem not available") + NEWLINE);
        return;
    }

    EmulatorPauseBracket bracket(emulator);

    // Stop tape and clear filepath
    context->pTape->stopTape();
    context->coreState.tapeFilePath.clear();

    session.SendResponse(std::string("Tape ejected") + NEWLINE);
}

void CLIProcessor::HandleTapePlay(const ClientSession& session, std::shared_ptr<Emulator> emulator,
                                  EmulatorContext* context)
{
    if (!context->pTape)
    {
        session.SendResponse(std::string("Error: Tape subsystem not available") + NEWLINE);
        return;
    }

    if (context->coreState.tapeFilePath.empty())
    {
        session.SendResponse(std::string("Error: No tape loaded") + NEWLINE + "Use 'tape load <file>' first" + NEWLINE);
        return;
    }

    EmulatorPauseBracket bracket(emulator);

    // Parse-once (idempotent): surfaces never race the lazy image load
    if (!context->pTape->EnsureImageLoaded())
    {
        session.SendResponse(std::string("Error: Tape image has no loadable blocks") + NEWLINE);
        return;
    }

    // Paused -> resume the frozen position IN PLACE (FR-6); otherwise start
    // at the consumption cursor — where a real tape head would be
    if (context->pTape->GetPlaybackState() == TapePlaybackState::Paused)
    {
        context->pTape->ResumePlaybackFromPause();
        session.SendResponse(std::string("Tape playback resumed (in place)") + NEWLINE);
    }
    else
    {
        context->pTape->StartPlaybackAtCursor();
        session.SendResponse(std::string("Tape playback started") + NEWLINE);
    }
}

void CLIProcessor::HandleTapePause(const ClientSession& session, std::shared_ptr<Emulator> emulator,
                                   EmulatorContext* context)
{
    if (!context->pTape)
    {
        session.SendResponse(std::string("Error: Tape subsystem not available") + NEWLINE);
        return;
    }

    if (context->coreState.tapeFilePath.empty())
    {
        session.SendResponse(std::string("Error: No tape loaded") + NEWLINE + "Use 'tape load <file>' first" + NEWLINE);
        return;
    }

    EmulatorPauseBracket bracket(emulator);

    const TapePlaybackState state = context->pTape->GetPlaybackState();
    if (state == TapePlaybackState::Paused)
    {
        session.SendResponse(std::string("Tape already paused") + NEWLINE);
        return;
    }

    if (state != TapePlaybackState::Playing)
    {
        session.SendResponse(std::string("Error: Tape is not playing") + NEWLINE);
        return;
    }

    context->pTape->pausePlayback();
    session.SendResponse(std::string("Tape paused (play resumes in place)") + NEWLINE);
}

void CLIProcessor::HandleTapeStop(const ClientSession& session, std::shared_ptr<Emulator> emulator,
                                  EmulatorContext* context)
{
    if (!context->pTape)
    {
        session.SendResponse(std::string("Error: Tape subsystem not available") + NEWLINE);
        return;
    }

    EmulatorPauseBracket bracket(emulator);

    context->pTape->stopTape();

    session.SendResponse(std::string("Tape playback stopped") + NEWLINE);
}

void CLIProcessor::HandleTapeRewind(const ClientSession& session, std::shared_ptr<Emulator> emulator,
                                    EmulatorContext* context)
{
    if (!context->pTape)
    {
        session.SendResponse(std::string("Error: Tape subsystem not available") + NEWLINE);
        return;
    }

    if (context->coreState.tapeFilePath.empty())
    {
        session.SendResponse(std::string("Error: No tape loaded") + NEWLINE + "Use 'tape load <file>' first" + NEWLINE);
        return;
    }

    EmulatorPauseBracket bracket(emulator);

    // Rewind keeps the image and catalog (FR-5) — unlike stop/eject
    context->pTape->EnsureImageLoaded();
    context->pTape->RewindToStart();

    session.SendResponse(std::string("Tape rewound to beginning") + NEWLINE);
}

void CLIProcessor::HandleTapeSeek(const ClientSession& session, std::shared_ptr<Emulator> emulator,
                                  EmulatorContext* context, const std::vector<std::string>& args)
{
    if (!context->pTape)
    {
        session.SendResponse(std::string("Error: Tape subsystem not available") + NEWLINE);
        return;
    }

    if (args.size() < 2)
    {
        session.SendResponse(std::string("Error: Missing block index") + NEWLINE + "Usage: tape seek <index>" + NEWLINE);
        return;
    }

    size_t index = 0;
    try
    {
        index = static_cast<size_t>(std::stoul(args[1]));
    }
    catch (const std::exception&)
    {
        session.SendResponse(std::string("Error: Invalid block index '") + args[1] + "'" + NEWLINE);
        return;
    }

    EmulatorPauseBracket bracket(emulator);

    if (!context->pTape->EnsureImageLoaded())
    {
        session.SendResponse(std::string("Error: Tape image has no loadable blocks") + NEWLINE);
        return;
    }

    if (!context->pTape->SeekToBlock(index))
    {
        std::stringstream ss;
        ss << "Error: Block index " << index << " out of range (tape has " << context->pTape->GetBlocks().size()
           << " block(s), indices 0-" << (context->pTape->GetBlocks().size() - 1) << ")" << NEWLINE;
        session.SendResponse(ss.str());
        return;
    }

    std::stringstream ss;
    ss << "Seeked to block " << index << " (next playback starts there)" << NEWLINE;
    AppendPositionLine(ss, *context->pTape);
    ss << NEWLINE;
    session.SendResponse(ss.str());
}

void CLIProcessor::HandleTapePos(const ClientSession& session, std::shared_ptr<Emulator> emulator,
                                 EmulatorContext* context)
{
    if (!context->pTape)
    {
        session.SendResponse(std::string("Error: Tape subsystem not available") + NEWLINE);
        return;
    }

    if (context->coreState.tapeFilePath.empty())
    {
        session.SendResponse(std::string("No tape loaded") + NEWLINE);
        return;
    }

    EmulatorPauseBracket bracket(emulator);

    if (!context->pTape->EnsureImageLoaded())
    {
        session.SendResponse(std::string("Error: Tape image has no loadable blocks") + NEWLINE);
        return;
    }

    std::stringstream ss;
    AppendPositionLine(ss, *context->pTape);
    ss << NEWLINE;
    session.SendResponse(ss.str());
}

void CLIProcessor::HandleTapeBlocks(const ClientSession& session, std::shared_ptr<Emulator> emulator,
                                    EmulatorContext* context)
{
    if (!context->pTape)
    {
        session.SendResponse(std::string("Error: Tape subsystem not available") + NEWLINE);
        return;
    }

    if (context->coreState.tapeFilePath.empty())
    {
        session.SendResponse(std::string("No tape loaded") + NEWLINE + "Use 'tape load <file>' first" + NEWLINE);
        return;
    }

    EmulatorPauseBracket bracket(emulator);

    if (!context->pTape->EnsureImageLoaded())
    {
        session.SendResponse(std::string("Error: Tape image has no loadable blocks") + NEWLINE);
        return;
    }

    const std::vector<TapeBlockDescriptor>& catalog = context->pTape->GetBlockCatalog();
    const TapeFastLoadPlan& plan = context->pTape->GetFastLoadPlan();

    if (catalog.empty())
    {
        session.SendResponse(std::string("Tape has no blocks") + NEWLINE);
        return;
    }

    std::stringstream ss;
    ss << std::left;
    ss << std::setw(4) << "IDX" << std::setw(12) << "KIND" << std::setw(7) << "HEADER" << std::setw(16) << "SPEED"
       << std::setw(8) << "FAST" << std::setw(11) << "NAME" << std::setw(15) << "TYPE" << std::setw(6) << "LEN"
       << std::setw(6) << "START" << std::setw(6) << "CKSUM" << "TIME" << NEWLINE;
    ss << std::string(87, '-') << NEWLINE;

    for (const TapeBlockDescriptor& descriptor : catalog)
    {
        ss << std::setw(4) << descriptor.index << std::setw(12) << getTapeBlockKindName(descriptor.kind) << std::setw(7)
           << FormatHeaderColumn(descriptor) << std::setw(16) << FormatSpeedColumn(descriptor) << std::setw(8)
           << (plan.perBlock.size() == catalog.size() ? FastLoadShortReason(plan.perBlock[descriptor.index]) : "?")
           << std::setw(11) << (!descriptor.name.empty() ? descriptor.name : descriptor.groupLabel) << std::setw(15)
           // r11: headerless payloads read as Code — same conventional TYPE the
           // ROM header would declare for raw bytes
           << (descriptor.headerValid
                   ? getTapeBlockTypeName(descriptor.headerType)
                   : (descriptor.headerless ? getTapeBlockTypeName(TAP_BLOCK_CODE) : "-"))
           << std::setw(6)
           << (descriptor.rawSize > 0 ? std::to_string(descriptor.rawSize) : "-") << std::setw(6)
           << (descriptor.headerValid ? std::to_string(descriptor.param1) : "-") << std::setw(6)
           << FormatChecksumColumn(descriptor) << std::fixed << std::setprecision(1) << descriptor.estimatedSeconds
           << "s" << NEWLINE;
    }

    ss << std::string(87, '-') << NEWLINE;
    ss << "Blocks: " << catalog.size() << ", cursor: " << context->pTape->GetConsumptionCursor() << ", total: "
       << std::fixed << std::setprecision(1) << plan.totalSeconds << "s" << NEWLINE;
    ss << plan.summary << NEWLINE;

    session.SendResponse(ss.str());
}

void CLIProcessor::HandleTapeInfo(const ClientSession& session, std::shared_ptr<Emulator> emulator,
                                  EmulatorContext* context)
{
    if (!context->pTape)
    {
        session.SendResponse(std::string("Error: Tape subsystem not available") + NEWLINE);
        return;
    }

    std::stringstream ss;
    ss << "Tape Status" << NEWLINE;
    ss << "===========" << NEWLINE;
    ss << NEWLINE;

    if (context->coreState.tapeFilePath.empty())
    {
        ss << "No tape loaded" << NEWLINE;
        session.SendResponse(ss.str());
        return;
    }

    EmulatorPauseBracket bracket(emulator);

    ss << "File: " << context->coreState.tapeFilePath << NEWLINE;

    if (!context->pTape->EnsureImageLoaded())
    {
        ss << "Status: no loadable blocks" << NEWLINE;
        session.SendResponse(ss.str());
        return;
    }

    const TapeFastLoadPlan& plan = context->pTape->GetFastLoadPlan();

    ss << "Format: " << context->pTape->GetLoadedFormatId() << NEWLINE;
    AppendPositionLine(ss, *context->pTape);
    ss << NEWLINE;
    ss << "Blocks: " << context->pTape->GetBlocks().size() << NEWLINE;
    ss << "Total duration: " << std::fixed << std::setprecision(1) << plan.totalSeconds << "s" << NEWLINE;
    ss << "Fast tape: " << (context->pFeatureManager && context->pFeatureManager->isEnabled(Features::kFastTape)
                                ? "on"
                                : "off") << NEWLINE;
    ss << "Turbo tape: " << (context->pFeatureManager && context->pFeatureManager->isEnabled(Features::kTurboTape)
                                ? "on"
                                : "off") << NEWLINE;
    ss << plan.summary << NEWLINE;

    session.SendResponse(ss.str());
}

/// endregion </Tape Control Commands>

/// region <Tape Audio Bridge Commands>

namespace
{
    constexpr const char* NEWLINE = CLIProcessor::NEWLINE;

    /// tape render usage block (tape-audio-bridge design §7.1)
    void SendTapeRenderUsage(const ClientSession& session)
    {
        std::stringstream ss;
        ss << "Usage: tape render <image.tap|image.tzx> [--blocks first[-last]] [--rate N] [--amp X] [--invert] -o out.wav|out.flac" << NEWLINE;
        ss << NEWLINE;
        ss << "Options:" << NEWLINE;
        ss << "  --blocks N | N-M  - Render one block or an inclusive range (default: whole tape)" << NEWLINE;
        ss << "  --rate N          - Sample rate, Hz (default 44100)" << NEWLINE;
        ss << "  --amp X           - Amplitude as a fraction of full scale, 0.01-1.0 (default 0.8)" << NEWLINE;
        ss << "  --invert          - Invert signal polarity" << NEWLINE;
        ss << "  -o <file>         - Output .wav or .flac (.flac requires ffmpeg)" << NEWLINE;
        ss << NEWLINE;
        ss << "Examples:" << NEWLINE;
        ss << "  tape render game.tzx -o game.wav" << NEWLINE;
        ss << "  tape render game.tap --blocks 3 -o block3.flac" << NEWLINE;
        session.SendResponse(ss.str());
    }

    /// tape import usage block
    void SendTapeImportUsage(const ClientSession& session)
    {
        std::stringstream ss;
        ss << "Usage: tape import <audio.wav|audio.flac|audio.mp3> [--target auto|tzx|tap] [--hysteresis X] -o out.tzx|out.tap" << NEWLINE;
        ss << NEWLINE;
        ss << "Options:" << NEWLINE;
        ss << "  --target auto|tzx|tap - Expected output format; must match the -o extension (default auto)" << NEWLINE;
        ss << "  --hysteresis X        - Schmitt band, 0.05-0.45 of signal range (default 0.2)" << NEWLINE;
        ss << "  -o <file>             - Output .tzx (exact pulses) or .tap (ROM-standard content only)" << NEWLINE;
        ss << NEWLINE;
        ss << "The recognition summary is printed on success; .tap refuses" << NEWLINE;
        ss << "non-ROM-standard content and names the .tzx alternative." << NEWLINE;
        ss << NEWLINE;
        ss << "Examples:" << NEWLINE;
        ss << "  tape import recording.wav -o imported.tzx" << NEWLINE;
        ss << "  tape import song.mp3 --target tap -o song.tap" << NEWLINE;
        session.SendResponse(ss.str());
    }

    /// Case-insensitive ".ext" suffix check
    bool HasExtension(const std::string& path, const char* extension)
    {
        const size_t length = std::strlen(extension);
        if (path.size() < length)
        {
            return false;
        }
        const std::string tail = path.substr(path.size() - length);
        return std::equal(tail.begin(), tail.end(), extension, [](char a, char b)
                          { return std::tolower(static_cast<unsigned char>(a)) == std::tolower(static_cast<unsigned char>(b)); });
    }
}  // namespace

void CLIProcessor::HandleTapeRender(const ClientSession& session, const std::vector<std::string>& args)
{
    if (args.size() < 2 || args[1].front() == '-')
    {
        SendTapeRenderUsage(session);
        return;
    }

    TapeRenderRequest request;
    request.sourcePath = args[1];

    bool haveOutput = false;
    bool haveBlocks = false;
    for (size_t i = 2; i < args.size(); i++)
    {
        const std::string& arg = args[i];
        const bool hasValue = i + 1 < args.size();

        if (arg == "--blocks" && hasValue)
        {
            const std::string& value = args[++i];
            const size_t dash = value.find('-');
            try
            {
                request.firstBlock = static_cast<size_t>(std::stoul(dash == std::string::npos ? value : value.substr(0, dash)));
                request.lastBlock = dash == std::string::npos ? request.firstBlock
                                                               : static_cast<size_t>(std::stoul(value.substr(dash + 1)));
            }
            catch (const std::exception&)
            {
                session.SendResponse(std::string("Error: Invalid --blocks '") + value + "' (expected N or N-M)" + NEWLINE);
                return;
            }
            if (request.lastBlock < request.firstBlock)
            {
                session.SendResponse(std::string("Error: --blocks range ends before it starts") + NEWLINE);
                return;
            }
            haveBlocks = true;
        }
        else if (arg == "--rate" && hasValue)
        {
            try
            {
                request.sampleRate = static_cast<uint32_t>(std::stoul(args[++i]));
            }
            catch (const std::exception&)
            {
                session.SendResponse(std::string("Error: Invalid --rate '") + args[i] + "'" + NEWLINE);
                return;
            }
            if (request.sampleRate < 8000 || request.sampleRate > 192000)
            {
                session.SendResponse(std::string("Error: --rate must be 8000-192000 Hz") + NEWLINE);
                return;
            }
        }
        else if (arg == "--amp" && hasValue)
        {
            try
            {
                request.amplitude = std::stod(args[++i]);
            }
            catch (const std::exception&)
            {
                session.SendResponse(std::string("Error: Invalid --amp '") + args[i] + "'" + NEWLINE);
                return;
            }
            if (request.amplitude < 0.01 || request.amplitude > 1.0)
            {
                session.SendResponse(std::string("Error: --amp must be 0.01-1.0") + NEWLINE);
                return;
            }
        }
        else if (arg == "--invert")
        {
            request.invertLevel = true;
        }
        else if (arg == "-o" && hasValue)
        {
            request.outputPath = args[++i];
            haveOutput = true;
        }
        else
        {
            session.SendResponse(std::string("Error: Unknown or incomplete option '") + arg + "'" + NEWLINE +
                                 "Use 'tape render' without arguments to see usage." + NEWLINE);
            return;
        }
    }

    if (!haveOutput)
    {
        session.SendResponse(std::string("Error: Missing -o <output>") + NEWLINE);
        return;
    }
    if (!HasExtension(request.outputPath, ".wav") && !HasExtension(request.outputPath, ".flac"))
    {
        session.SendResponse(std::string("Error: Output must end in .wav or .flac") + NEWLINE);
        return;
    }
    if (haveBlocks)
    {
        // Range validation happens against the catalog inside the renderer;
        // nothing more to check here.
    }

    const TapeRenderResult result = RenderTapeToAudio(request);
    if (!result.ok)
    {
        session.SendResponse(std::string("Error: ") + result.errorText + NEWLINE);
        return;
    }

    std::stringstream ss;
    ss << "Rendered " << result.blocksRendered << " block(s) -> " << request.outputPath << NEWLINE;
    ss << "Duration: " << std::fixed << std::setprecision(2) << result.durationSec << "s, " << result.samplesWritten
       << " samples @ " << request.sampleRate << " Hz (" << result.encoderUsed << ")" << NEWLINE;
    for (const std::string& warning : result.warnings)
    {
        ss << "Warning: " << warning << NEWLINE;
    }
    session.SendResponse(ss.str());
}

void CLIProcessor::HandleTapeImport(const ClientSession& session, const std::vector<std::string>& args)
{
    if (args.size() < 2 || args[1].front() == '-')
    {
        SendTapeImportUsage(session);
        return;
    }

    TapeImportRequest request;
    request.sourcePath = args[1];

    std::string outputPath;
    std::string target = "auto";
    for (size_t i = 2; i < args.size(); i++)
    {
        const std::string& arg = args[i];
        const bool hasValue = i + 1 < args.size();

        if (arg == "--target" && hasValue)
        {
            target = args[++i];
            std::transform(target.begin(), target.end(), target.begin(), ::tolower);
            if (target != "auto" && target != "tzx" && target != "tap")
            {
                session.SendResponse(std::string("Error: --target must be auto, tzx or tap") + NEWLINE);
                return;
            }
        }
        else if (arg == "--hysteresis" && hasValue)
        {
            try
            {
                request.hysteresis = std::stod(args[++i]);
            }
            catch (const std::exception&)
            {
                session.SendResponse(std::string("Error: Invalid --hysteresis '") + args[i] + "'" + NEWLINE);
                return;
            }
            if (request.hysteresis < 0.05 || request.hysteresis > 0.45)
            {
                session.SendResponse(std::string("Error: --hysteresis must be 0.05-0.45") + NEWLINE);
                return;
            }
        }
        else if (arg == "-o" && hasValue)
        {
            outputPath = args[++i];
        }
        else
        {
            session.SendResponse(std::string("Error: Unknown or incomplete option '") + arg + "'" + NEWLINE +
                                 "Use 'tape import' without arguments to see usage." + NEWLINE);
            return;
        }
    }

    if (outputPath.empty())
    {
        session.SendResponse(std::string("Error: Missing -o <output>") + NEWLINE);
        return;
    }

    const bool tapOutput = HasExtension(outputPath, ".tap");
    if (!tapOutput && !HasExtension(outputPath, ".tzx"))
    {
        session.SendResponse(std::string("Error: Output must end in .tzx or .tap") + NEWLINE);
        return;
    }
    if (target == "tzx" && tapOutput)
    {
        session.SendResponse(std::string("Error: --target tzx contradicts the .tap output extension") + NEWLINE);
        return;
    }
    if (target == "tap" && !tapOutput)
    {
        session.SendResponse(std::string("Error: --target tap contradicts the .tzx output extension") + NEWLINE);
        return;
    }

    const TapeImportResult imported = ImportAudioToTape(request);
    if (!imported.ok)
    {
        session.SendResponse(std::string("Error: ") + imported.errorText + NEWLINE);
        return;
    }

    // The TAP gate refuses non-ROM-standard content and names the .tzx
    // alternative in errorText — surfaced verbatim (design §7.1)
    const TapeSaveResult saved = SaveTapeImage(imported.image, outputPath);
    if (!saved.ok)
    {
        session.SendResponse(std::string("Error: ") + saved.errorText + NEWLINE);
        return;
    }

    // Recognition summary: per-kind counts over the catalog the importer
    // built, in the same vocabulary as `tape blocks`
    std::map<TapeBlockKindEnum, size_t> kindCounts;
    for (const TapeBlockDescriptor& descriptor : imported.image.descriptors)
    {
        kindCounts[descriptor.kind]++;
    }
    std::stringstream kinds;
    for (const auto& [kind, count] : kindCounts)
    {
        kinds << count << " " << getTapeBlockKindName(kind) << ", ";
    }
    std::string kindsText = kinds.str();
    if (!kindsText.empty())
    {
        kindsText.resize(kindsText.size() - 2);  // drop the trailing ", "
    }

    std::stringstream ss;
    ss << "Imported " << request.sourcePath << " via " << imported.decoderUsed << NEWLINE;
    ss << "Recognized " << imported.blocksRecognized << " block(s)"
       << (kindsText.empty() ? "" : ": " + kindsText) << NEWLINE;
    ss << "Saved " << saved.blocksWritten << " block(s) -> " << outputPath << NEWLINE;
    for (const std::string& warning : imported.warnings)
    {
        ss << "Warning: " << warning << NEWLINE;
    }
    session.SendResponse(ss.str());
}

/// endregion </Tape Audio Bridge Commands>
