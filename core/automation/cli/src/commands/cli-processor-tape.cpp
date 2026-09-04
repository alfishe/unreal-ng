#include <base/featuremanager.h>
#include <emulator/emulator.h>
#include <emulator/emulatorcontext.h>
#include <emulator/io/tape/tapecatalog.h>
#include <emulator/io/tape/tape.h>
#include <emulator/platform.h>

#include <algorithm>
#include <iomanip>
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
        ss << NEWLINE;
        ss << "Examples:" << NEWLINE;
        ss << "  tape load /path/to/game.tap" << NEWLINE;
        ss << "  tape play" << NEWLINE;
        ss << "  tape seek 4" << NEWLINE;
        ss << "  tape blocks" << NEWLINE;
        ss << "  tape info" << NEWLINE;

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
    ss << plan.summary << NEWLINE;

    session.SendResponse(ss.str());
}

/// endregion </Tape Control Commands>
