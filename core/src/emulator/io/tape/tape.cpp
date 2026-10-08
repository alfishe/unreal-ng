#include "tape.h"

#include "common/filehelper.h"
#include "common/stringhelper.h"
#include "emulator/cpu/core.h"
#include "emulator/emulatorcontext.h"
#include "emulator/io/tape/tapecatalog.h"
#include "emulator/io/tape/tapepulsegen.h"
#include "emulator/sound/soundmanager.h"
#include "loaders/tape/loader_tape.h"
#include "stdafx.h"
#include <cstring>
#include "debugger/ttd/timetravelmanager.h"  // TimeTravelManager (Item 6 markers)

/// region <Constructors / destructors>

Tape::Tape(EmulatorContext* context)
{
    _context = context;
    _logger = _context->pModuleLogger;

    reset();
}

Tape::~Tape() {}

/// endregion </Constructors / destructors>

/// region <Tape control methods>
void Tape::startTape()
{
    // Phase 2 Item 6 - record an external-event marker. Tape playback is
    // nondeterministic from the emulator's perspective (content arrives via
    // the host clock, not via CPU-readable state), so SeekTo across a
    // startTape boundary would silently produce wrong state. The marker is
    // a replay barrier: SeekTo stops at it and surfaces it to the caller.
    //
    // No-op unless the TTD session is Recording - same guard as
    // RecordInputEvent. The CLI / WebAPI handlers pause the emulator
    // before calling startTape, so frame_counter and z80.t are stable.
    if (_context && _context->pTimeTravelHooks)
        _context->pTimeTravelHooks->RecordExternalEvent(
            ttd::TTDExternalEventKind::TapeControl, "tape play");

    _playbackFrozen = false;
    _tapeStarted = true;
    _muteEAR = true;
    _lastTapeBit = false;
    _framesNotListened = 0;
    MLOGINFO("Tape started");
}

void Tape::stopTape()
{
    // Phase 2 Item 6 - see startTape() for rationale.
    if (_context && _context->pTimeTravelHooks)
        _context->pTimeTravelHooks->RecordExternalEvent(
            ttd::TTDExternalEventKind::TapeControl, "tape stop");

    _tapeStarted = false;
    _playbackFrozen = false;
    _muteEAR = false;

    // Reset all tape-related fields and free up blocks memory.
    // This is the tape-control stop: the image is invalidated (design §9.4),
    // so the loaded-path key must be dropped as well or EnsureImageLoaded()
    // would wrongly consider the (now empty) image fresh.
    _tapeBlocks = std::vector<TapeBlock>();
    _catalog.clear();
    _fastLoadPlan = TapeFastLoadPlan();
    _imageLoadedPath.clear();
    _imageFormatId.clear();
    _currentTapeBlock = nullptr;
    _currentTapeBlockIndex = UINT64_MAX;
    _currentPulseIdxInBlock = 0;
    _currentOffsetWithinPulse = 0;

    _currentClockCount = 0;
    _lastTapeBit = false;
}

void Tape::stopPlayback()
{
    // Watchdog / natural end-of-tape stop. Unlike stopTape() this is NOT a
    // tape-control command: the image and the consumption cursor survive, so
    // a later load (or trap) continues from where the tape stopped — matching
    // a real cassette that keeps rolling.

    // Phase 2 Item 6 - same replay fencing as stopTape(): the playback region
    // itself is wall-clock driven (nondeterministic), so a stop boundary must
    // remain a SeekTo barrier.
    if (_context && _context->pTimeTravelHooks)
        _context->pTimeTravelHooks->RecordExternalEvent(
            ttd::TTDExternalEventKind::TapeControl, "tape stop");

    // A stop never consumes a partly played block (nonstandard-loader
    // investigation P2): the cursor stays on it, and the next load reads it
    // from its pilot. Every caller stops at the end of the tape, with no block
    // in flight; a stop that is not final is a freeze (pausePlayback()).

    _tapeStarted = false;
    _playbackFrozen = false;
    _muteEAR = false;
    _currentTapeBlock = nullptr;
    _currentPulseIdxInBlock = 0;
    _currentOffsetWithinPulse = 0;
    _lastTapeBit = false;
}

void Tape::pausePlayback()
{
    // Freeze the head in place: everything positional survives (in-flight
    // block, pulse indices, last EAR level) so ResumePlaybackAfterPoll()
    // continues the bitstream mid-block — un-pausing a real deck. Only the
    // playback driver stops. Terminal stopPlayback() is for the end of the
    // tape: it forgets the pulse position a loader that merely stopped
    // listening while it processes needs to continue from.
    //
    // Inside a pilot, go back to its first pulse instead (loader-follow
    // design §5.4): a pilot is restartable, and the rest of one can be too
    // short for the ROM (256 pulses to lock on) or for a loader that times it.
    if (_currentTapeBlock != nullptr && _currentOffsetWithinPulse < _currentTapeBlock->pilotEdgeCount)
    {
        _currentOffsetWithinPulse = 0;
        _currentPulseIdxInBlock = 0;
    }

    // Same replay fencing as stopPlayback(): a pause boundary is still a
    // wall-clock-driven playback boundary for SeekTo.
    if (_context && _context->pTimeTravelHooks)
        _context->pTimeTravelHooks->RecordExternalEvent(
            ttd::TTDExternalEventKind::TapeControl, "tape pause");

    MLOGINFO("Tape paused at block %zu, pulse %zu (no loader listening)",
             GetConsumptionCursor(), _currentOffsetWithinPulse);

    _tapeStarted = false;
    _muteEAR = false;
    _playbackFrozen = true;

    // Re-seed the clock on resume: a delta spanning the pause would otherwise
    // be consumed as one giant pulse. 0 is the "re-seed on next read" marker
    // honored by getTapeStreamBit().
    _currentClockCount = 0;
}

void Tape::ResumePlaybackAfterPoll()
{
    // Sustained EAR polling from arbitrary code (custom loaders live in RAM
    // and never hit the ROM $0562/$0564 auto-start anchor). Two cases:
    //
    // 1. Frozen mid-block position still valid (paused by the read-gap
    //    watchdog, trap has not consumed past it): un-pause IN PLACE. The
    //    stream continues mid-block with level continuity — startTape() is
    //    not used because it resets _lastTapeBit, which would inject a
    //    spurious edge into the stream a mid-block loader is decoding.
    // 2. No usable position (never started / trap consumed past the frozen
    //    block): position at the consumption cursor like a fresh signal start.
    const size_t cursor = GetConsumptionCursor();
    const bool frozenInFlight = _currentTapeBlock != nullptr &&
                                _currentTapeBlockIndex != UINT64_MAX &&
                                _currentTapeBlockIndex >= cursor &&
                                _currentTapeBlockIndex < _tapeBlocks.size();

    if (!frozenInFlight)
    {
        StartPlaybackAtCursor();
        return;
    }

    if (_context && _context->pTimeTravelHooks)
        _context->pTimeTravelHooks->RecordExternalEvent(
            ttd::TTDExternalEventKind::TapeControl, "tape play");

    // Same flag set as startTape(), but position and last level survive
    _playbackFrozen = false;
    _tapeStarted = true;
    _muteEAR = true;
    _framesNotListened = 0;

    MLOGINFO("Tape resumed at block %zu, pulse %zu (sustained EAR polling)",
             _currentTapeBlockIndex, _currentOffsetWithinPulse);
}

/// Read a whole tape file into memory. The unified loader contract is
/// buffer-based (design §5.3), so the one filesystem touch lives here, at the
/// caller — loaders never see paths.
static bool ReadTapeFile(const std::string& path, std::vector<uint8_t>& bytes)
{
    FILE* file = FileHelper::OpenExistingFile(path);
    if (file == nullptr)
    {
        return false;
    }

    size_t size = FileHelper::GetFileSize(file);
    if (size == 0)
    {
        FileHelper::CloseFile(file);
        return false;
    }

    bytes.resize(size);
    size_t read = FileHelper::ReadFileToBuffer(file, bytes.data(), size);
    FileHelper::CloseFile(file);

    return read == size;
}

void Tape::AttachImage(const TapeImage* image, const std::string& sourcePath, const std::string& formatId)
{
    // A media change: the manager refuses it while a TTD recording runs
    reset();
    _attachedImage = image;
    _attachedPath = sourcePath;
    _attachedFormatId = formatId;
    _attachGeneration++;
    _context->coreState.tapeFilePath = sourcePath;
}

void Tape::DetachImage()
{
    reset();
    _attachedImage = nullptr;
    _attachedPath.clear();
    _attachedFormatId.clear();
    _context->coreState.tapeFilePath.clear();
}

bool Tape::EnsureImageLoaded()
{
    const std::string& path = _context->coreState.tapeFilePath;

    // The media manager's tape: install a copy of the medium's blocks once per
    // attach (a stop or a reset dropped them; the medium still has them). A
    // path written into coreState by hand (tests, older code) falls through to
    // the file below
    if (_attachedImage && path == _attachedPath)
    {
        const std::string key = "\x01medium:" + std::to_string(_attachGeneration);
        if (_imageLoadedPath == key)
            return !_tapeBlocks.empty();

        _catalog = TapeCatalogParser::Build(*_attachedImage);
        _fastLoadPlan = TapeFastLoadEligibility::Analyze(_catalog, _attachedImage->controlFlowLinearized);
        _tapeBlocks = _attachedImage->blocks;
        _imageLoadedPath = key;
        _imageFormatId = _attachedFormatId;

        _playbackFrozen = false;
        _currentTapeBlock = nullptr;
        _currentTapeBlockIndex = UINT64_MAX;
        _currentPulseIdxInBlock = 0;
        _currentOffsetWithinPulse = 0;
        MLOGINFO("Tape image attached: '%s', format: %s, blocks: %zu", _attachedPath.c_str(), _imageFormatId.c_str(),
                 _tapeBlocks.size());
        return !_tapeBlocks.empty();
    }

    // Idempotent and path-keyed (design §9.4): an unchanged path (including
    // the empty "no tape selected" path) never re-parses over live blocks.
    if (_imageLoadedPath == path)
        return !_tapeBlocks.empty();

    if (path.empty())
        return false;

    // Unified loader contract (design §5.3/§5.6): read the file once, select
    // the loader by content probe (extension as tie-breaker), decode to a
    // TapeImage, then derive catalog and (later) fast-load plan beside the
    // blocks — one source of truth, one invalidation point.
    std::vector<uint8_t> bytes;
    if (!ReadTapeFile(path, bytes))
    {
        _imageLoadedPath = path;  // do not retry the same unreadable path every call
        _tapeBlocks.clear();
        _catalog.clear();
        _fastLoadPlan = TapeFastLoadPlan();
        _imageFormatId.clear();
        MLOGERROR("Tape image unreadable: '%s'", path.c_str());
        return false;
    }

    LoaderTapeBase* loader = TapeLoaderRegistry::Instance().Select(bytes, path);
    if (loader == nullptr)
    {
        _imageLoadedPath = path;
        _tapeBlocks.clear();
        _catalog.clear();
        _fastLoadPlan = TapeFastLoadPlan();
        _imageFormatId.clear();
        MLOGERROR("No tape loader claims '%s' (unknown or unsupported format)", path.c_str());
        return false;
    }

    TapeImage image = loader->Load(bytes, path);

    // Usability must be captured BEFORE the blocks move out of the image —
    // IsUsable() consults image.blocks, which the move empties (the check
    // used to sit below the move and fired a misleading empty-text error on
    // every successful load).
    const bool imageUsable = image.IsUsable();

    // Catalog derivation and fast-load eligibility run before the blocks
    // move out of the image (design §5.5/§5.8) — both pure, both once per load.
    // The plan runs over the FILLED catalog: loader-supplied descriptors are
    // partial by contract, and analyzing them directly classified checksums
    // and durations that were never derived (live smoke-test catch).
    _catalog = TapeCatalogParser::Build(image);
    _fastLoadPlan = TapeFastLoadEligibility::Analyze(_catalog, image.controlFlowLinearized);
    _tapeBlocks = std::move(image.blocks);
    _imageLoadedPath = path;
    _imageFormatId = loader->Format().id;

    for (const std::string& warning : image.parseWarnings)
    {
        MLOGWARNING("Tape '%s': %s", path.c_str(), warning.c_str());
    }

    if (!imageUsable)
    {
        MLOGERROR("Tape '%s' not loadable (%s): %s", path.c_str(), loader->Format().id.c_str(),
                  image.errorText.c_str());
    }

    // Fresh image: consumption cursor at the first block, playback state reset.
    _playbackFrozen = false;
    _currentTapeBlock = nullptr;
    _currentTapeBlockIndex = UINT64_MAX;
    _currentPulseIdxInBlock = 0;
    _currentOffsetWithinPulse = 0;

    MLOGINFO("Tape image loaded: '%s', format: %s, blocks: %zu", path.c_str(),
             loader->Format().id.c_str(), _tapeBlocks.size());

    return !_tapeBlocks.empty();
}

size_t Tape::GetConsumptionCursor() const
{
    return _currentTapeBlockIndex == UINT64_MAX ? 0 : _currentTapeBlockIndex;
}

void Tape::ConsumeBlock(size_t index)
{
    // Trap consumption path: block `index` was delivered in full. The sentinel
    // (nothing consumed yet) and the block itself both advance past `index`;
    // an already-advanced cursor is left untouched (idempotent no-op).
    if (_currentTapeBlockIndex == UINT64_MAX || _currentTapeBlockIndex <= index)
        _currentTapeBlockIndex = index + 1;
}

void Tape::StartPlaybackAtCursor()
{
    if (_tapeBlocks.empty())
        return;

    // Signal fallback begins exactly at the consumption cursor — where a real
    // tape head would be after the blocks already consumed. At end-of-tape the
    // cursor equals size: nothing left to play, leave the tape stopped (the
    // ROM loader then waits in its pilot loop, as with a tape that ran out).
    if (GetConsumptionCursor() >= _tapeBlocks.size())
        return;

    if (_currentTapeBlockIndex == UINT64_MAX)
        _currentTapeBlockIndex = 0;

    // Force (re)generation of the bitstream for the cursor block on the next
    // handleFrameStart() / getTapeStreamBit() pass, from its first pulse. The
    // pulse position must go too: handleFrameStart() rebuilds the block keeping
    // it (a TTD restore needs that), so a block frozen mid-data and restarted by
    // the ROM anchor would continue from the frozen pulse instead of its pilot
    // (nonstandard-loader investigation B4 / P3, test T12)
    _currentTapeBlock = nullptr;
    _currentOffsetWithinPulse = 0;
    _currentPulseIdxInBlock = 0;
    _currentClockCount = 0;

    startTape();
}
/// endregion </Tape control methods>

/// region <Playback state, position and seek (design §6)>

TapePlaybackState Tape::GetPlaybackState() const
{
    // State machine §6.3: no image (eject reset everything) reads as Idle —
    // Ended is only meaningful with an image whose cursor ran past the end.
    if (_tapeBlocks.empty())
        return TapePlaybackState::Idle;

    if (_tapeStarted)
        return TapePlaybackState::Playing;

    if (_playbackFrozen)
        return TapePlaybackState::Paused;

    // Natural end of tape (§6.1): the stopPlayback() the end of the stream
    // leaves behind parks the cursor past the last block.
    if (GetConsumptionCursor() >= _tapeBlocks.size())
        return TapePlaybackState::Ended;

    return TapePlaybackState::Idle;
}

std::optional<TapePosition> Tape::GetPosition() const
{
    if (_tapeBlocks.empty())
        return std::nullopt;

    TapePosition position;

    // Field-naming note: the engine keeps the edgePulseTimings vector index in
    // _currentOffsetWithinPulse and the intra-pulse T-state count in
    // _currentPulseIdxInBlock (getTapeStreamBit() is the source of truth).
    const bool inFlight = _currentTapeBlock != nullptr && _currentTapeBlockIndex != UINT64_MAX &&
                          _currentTapeBlockIndex < _tapeBlocks.size();
    if (inFlight)
    {
        position.blockIndex = _currentTapeBlockIndex;
        position.pulseIndex = std::min(_currentOffsetWithinPulse, _currentTapeBlock->edgePulseTimings.size());
        position.offsetWithinPulse = _currentPulseIdxInBlock;

        // Elapsed signal time: fully consumed pulses plus the partial one
        uint64_t elapsedTStates = position.offsetWithinPulse;
        for (size_t i = 0; i < position.pulseIndex; i++)
        {
            elapsedTStates += _currentTapeBlock->edgePulseTimings[i];
        }
        position.secondsIntoBlock = static_cast<double>(elapsedTStates) / 3500000.0;
    }
    else
    {
        // Next-up block; == block count only in the Ended state
        position.blockIndex = std::min(GetConsumptionCursor(), _tapeBlocks.size());
    }

    if (position.blockIndex < _catalog.size())
    {
        position.blockTotalSeconds = _catalog[position.blockIndex].estimatedSeconds;
    }

    return position;
}

bool Tape::SeekToBlock(size_t index)
{
    if (index >= _tapeBlocks.size())
    {
        return false;
    }

    // TTD (design §11): seek is a position-changing tape-control command —
    // same invalidation class as rewind. The marker carries the target.
    if (_context && _context->pTimeTravelHooks)
    {
        char reason[32];
        snprintf(reason, sizeof(reason), "tape seek %zu", index);
        _context->pTimeTravelHooks->RecordExternalEvent(
            ttd::TTDExternalEventKind::TapeControl, reason);
    }

    // Abandon, never consume (design §6.2): unlike stopPlayback(), the partial
    // in-flight block is dropped by redefining the cursor — and a paused
    // freeze is discarded with it (it pointed into a block the user just
    // left). Seek never starts playback: it arms, the next play/trap request
    // delivers from the new cursor.
    _tapeStarted = false;
    _muteEAR = false;
    _playbackFrozen = false;
    _currentTapeBlock = nullptr;
    _currentPulseIdxInBlock = 0;
    _currentOffsetWithinPulse = 0;
    _currentClockCount = 0;
    _lastTapeBit = false;

    _currentTapeBlockIndex = index;

    MLOGINFO("Tape seek: block %zu of %zu", index, _tapeBlocks.size());
    return true;
}

void Tape::RewindToStart()
{
    // == SeekToBlock(0), image and catalog kept (FR-5) — unlike legacy
    // reset(), which dropped the image as well. No-op without an image.
    if (_tapeBlocks.empty())
        return;

    SeekToBlock(0);
}

void Tape::ResumePlaybackFromPause()
{
    // Manual un-pause of a frozen position (FR-6): the read-gap watchdog's
    // pausePlayback() freeze, released by user intent rather than by
    // sustained EAR polling. No-op unless actually paused — the caller
    // (CLI `tape play`, WebAPI play) picks StartPlaybackAtCursor() otherwise.
    if (!_playbackFrozen)
        return;

    const size_t cursor = GetConsumptionCursor();
    const bool frozenInFlight = _currentTapeBlock != nullptr &&
                                _currentTapeBlockIndex != UINT64_MAX &&
                                _currentTapeBlockIndex >= cursor &&
                                _currentTapeBlockIndex < _tapeBlocks.size();

    _playbackFrozen = false;

    if (!frozenInFlight)
    {
        // Frozen position no longer usable (e.g. the trap consumed past it):
        // fall back to a fresh start at the cursor
        StartPlaybackAtCursor();
        return;
    }

    if (_context && _context->pTimeTravelHooks)
        _context->pTimeTravelHooks->RecordExternalEvent(
            ttd::TTDExternalEventKind::TapeControl, "tape resume");

    // Same flags startTape() sets; position and last EAR level survive, so
    // the bitstream continues mid-block without a spurious edge
    _tapeStarted = true;
    _muteEAR = true;
    _framesNotListened = 0;

    MLOGINFO("Tape resumed at block %zu, pulse %zu (manual)",
             _currentTapeBlockIndex, _currentOffsetWithinPulse);
}

/// endregion </Playback state, position and seek>

void Tape::reset()
{
    // Phase 2 Item 6 - distinguish "user-driven rewind" from "system-level
    // reset". The constructor calls reset() before _context is fully wired;
    // CLI/WebAPI rewind calls it after pausing the emulator. Recording is
    // guarded below, so no caller-passed flag is needed.
    const bool wasStarted = _tapeStarted;

    _tapeStarted = false;
    _playbackFrozen = false;
    _tapePosition = 0LL;

    // Tape input bitstream related
    _tapeBlocks = std::vector<TapeBlock>();
    _catalog.clear();
    _fastLoadPlan = TapeFastLoadPlan();
    _imageLoadedPath.clear();
    _imageFormatId.clear();
    _currentTapeBlock = nullptr;
    _currentTapeBlockIndex = UINT64_MAX;
    _currentPulseIdxInBlock = 0;
    _currentOffsetWithinPulse = 0;

    _currentClockCount = 0;
    _lastTapeBit = false;
    // Phase 2 Item 6 - only record a rewind marker when reset() actually
    // changes tape state mid-session. A no-op reset (constructor, system
    // reset before any session) must not pollute the journal.
    if (wasStarted && _context && _context->pTimeTravelHooks)
        _context->pTimeTravelHooks->RecordExternalEvent(
            ttd::TTDExternalEventKind::TapeControl, "tape rewind");

};

/// region <Time base>

uint64_t Tape::ClockCount() const
{
    const EmulatorState& state = _context->emulatorState;
    uint64_t inFrame = _context->pCore->GetZ80()->t;
    if (_baseClockTimeBase && (state.hw_turbo_ratio_applied > 1 || state.hw_clock_den_applied > 1))
        inFrame = state.AudioTstate(static_cast<uint32_t>(inFrame));
    return state.t_states + inFrame;
}

/// endregion </Time base>

/// region <Port events>

uint8_t Tape::handlePortIn([[maybe_unused]] uint16_t port)
{
    uint8_t result = 0;

    [[maybe_unused]] CONFIG& config = _context->config;
    Z80& cpu = *_context->pCore->GetZ80();
    Memory& memory = *_context->pMemory;

    const uint32_t tState = _context->pCore->GetZ80()->t;
    [[maybe_unused]] uint8_t prevPortValue = _context->emulatorState.pFE;

    // Scale t-state by speed multiplier for correct audio timing
    [[maybe_unused]] uint8_t speedMultiplier = _context->emulatorState.current_z80_frequency_multiplier;
    [[maybe_unused]] uint32_t scaledTState = tState * speedMultiplier;

    // Monotonic counter for tape timing (t_states + t, in the time base: ClockCount)
    const uint64_t clockCount = ClockCount();

    // Is a loader listening? Keyboard, joystick and menu reads never count,
    // however often they come (loader-follow design §4)
    const bool listening = IsListeningRead(ClassifyPortRead(), clockCount);
    if (listening)
        _listenReadsThisFrame++;

    if (_tapeStarted)
    {
        bool tapeBit = getTapeStreamBit(clockCount);
        result = (uint8_t)tapeBit << 6;
    }
    else
    {
        /// region <Start when a loader listens>

        // Any loader, in ROM or RAM, parked or frozen by the pause below or
        // never started: once it listens, the tape moves (design §5.2).
        // ResumePlaybackAfterPoll() picks the resume point (§5.4).
        if (listening && _listenReadsThisFrame == TAPE_START_LISTEN_READS)
        {
            // Load the image if needed (idempotent, path-keyed)
            if (EnsureImageLoaded())
                ResumePlaybackAfterPoll();
        }

        /// endregion </Start when a loader listens>

        /// region <Idle EAR level>

        // No playback: the level depends on the board. Deterministic by
        // design - a random idle level would turn firmware timing into a lottery.
        // - Ferranti ULA (48K / 128K / +2), issue 3: the EAR output feeds back
        //   into the input, bit 6 follows bit 4 of the last #FE write. z80test's
        //   hardware CRCs of the IN tests (z80full: IN A,(N) .. INDR->NOP')
        //   are taken this way, with bit 4 = 0.
        // - Every other board: LOW with no tape image (z80full passes as on the
        //   48K), HIGH once an image is loaded - the line is not driven low
        //   until the first edge of a real tape pulse arrives. No clone feeds
        //   the output back: on the Scorpion the tape input is a self-biased
        //   CD4069 amplifier with no pull-up, its idle level set by no part.
        //   The ProfROM's #FFBE reads are the SMUC IDE status (bit 6 = DRDY,
        //   error #61 = DRDY timeout), not the tape port.
        switch (config.mem_model)
        {
            case MM_SPECTRUM48:
            case MM_SPECTRUM128:
            case MM_PLUS2:
                result = (prevPortValue & 0b0001'0000) ? 0b0100'0000 : 0;
                break;
            default:
                result = _imageLoadedPath.empty() ? 0 : 0b0100'0000;
                break;
        }

        /// endregion </Idle EAR level>

        // If we just executed instruction at $0562 IN A,($FE)
        // And our PC is currently on $0564 RRA (which has opcode 0x1F)
        // Check ROM content directly - works for both 48K and 128K modes
        uint8_t* romBank = memory.GetPhysicalAddressForZ80Page(0);
        if (cpu.pc == 0x0564 && romBank && romBank[0x0564] == 0x1F)
        {
            // Auto-start: the ROM loader is polling EAR with no playback active.
            // Load the image (idempotent, path-keyed) and start signal playback
            // from the consumption cursor — exactly where the fast-load trap left
            // off when it declined, so fallback is seamless (design §9.4).
            // With no tape file selected the correct behavior is "nothing to
            // load" — the previous hardcoded dev-tree demo file is gone.
            //
            // Frame-accurate instant path; RAM-resident loaders use the
            // listening start above instead.
            if (EnsureImageLoaded())
                StartPlaybackAtCursor();
        }
    }

    return result;
}

void Tape::handlePortOut([[maybe_unused]] uint8_t value)
{
    // Hardware ULA Port #FE OUT (EAR/MIC bits) audio synthesis is handled
    // directly by Beeper::handlePortOut(). Tape audio input (during tape loading)
    // is processed in handlePortIn().
}

/// endregion </Port events>

/// region <Emulation events>

/// Prepare for next video frame start
/// If we have previous tape block played, then we can generate bitstream for the next block
void Tape::handleFrameStart()
{
    // Listening reads are counted per frame
    _listenReadsThisFrame = 0;

    // Monotonic counter for tape timing (t_states + t, in the time base: ClockCount)
    uint64_t clockCount = ClockCount();

    if (_tapeStarted && !_tapeBlocks.empty())
    {
        // Tape is just loaded, we need setup fields
        if (_currentTapeBlock == nullptr && _currentTapeBlockIndex == UINT64_MAX)
        {
            _currentTapeBlock = &_tapeBlocks[0];
            _currentTapeBlockIndex = 0;
            _currentPulseIdxInBlock = 0;
            _currentOffsetWithinPulse = 0;

            // Generating bit-stream related data
            generateBitstreamForStandardBlock(*_currentTapeBlock);

            // Record current clock
            _currentClockCount = clockCount;
        }
        // Just switched to next block, need to generate bit stream for it
        else if (_currentTapeBlockIndex < _tapeBlocks.size() && _currentTapeBlock == nullptr)
        {
            // Getting new TapeBlock
            _currentTapeBlock = &_tapeBlocks[_currentTapeBlockIndex];

            if (_currentTapeBlock)
            {
                // Generating bit-stream related data
                generateBitstreamForStandardBlock(*_currentTapeBlock);

                // Clear bit-stream data from previous block (guard: the cursor
                // may legitimately sit at block 0, e.g. after StartPlaybackAtCursor).
                // Byte-payload blocks only: loader-supplied pulse trains
                // (representation 3) ARE the block's content — wiping them
                // would make re-play and seeking past them impossible.
                if (_currentTapeBlockIndex > 0 &&
                    !_tapeBlocks[_currentTapeBlockIndex - 1].data.empty())
                {
                    TapeBlock& previousBlock = _tapeBlocks[_currentTapeBlockIndex - 1];
                    previousBlock.totalBitstreamLength = 0;
                    previousBlock.edgePulseTimings = std::vector<uint32_t>();
                }
            }
            else
            {
                // Error. There must be no nullable blocks
                throw std::logic_error("Tape::handleFrameStart() null TapeBlock found");
            }
        }
        else if (_currentTapeBlockIndex == UINT64_MAX)
        {
            // We've depleted all available blocks
            stopPlayback();
        }
    }
}

void Tape::handleStep()
{
    if (!_tapeStarted)
        return;

    Z80& cpu = *_context->pCore->GetZ80();
    const uint32_t tState = cpu.t;
    uint64_t clockCount = ClockCount();

    bool tapeBit = getTapeStreamBit(clockCount);

    if (tapeBit != _lastTapeBit)
    {
        _lastTapeBit = tapeBit;

        int16_t amp = tapeBit ? 6000 : -6000;
        uint8_t speedMultiplier = _context->emulatorState.current_z80_frequency_multiplier;
        uint32_t scaledTState = tState * speedMultiplier;

        _context->pSoundManager->updateDAC(scaledTState, amp, amp);
    }
}

void Tape::handleFrameEnd()
{
    if (!_tapeStarted)
        return;

    // No system variable decides anything here. ERR_NR ($5C3A) is ordinary
    // RAM that custom loaders use as scratch, so watching it stopped the tape
    // in the middle of their loads (nonstandard-loader investigation B1).

    // The tape moves only while a loader listens (loader-follow design §5.3)
    if (_listenReadsThisFrame > 0)
        _framesNotListened = 0;
    else
        _framesNotListened++;

    // In the silence after a block's data the loader has read the block:
    // wait at the next block's pilot, so a loader that is busy, waiting for a
    // key or playing music gets that pilot from its start whenever it listens
    // again. Inside a block, freeze in place (pausePlayback() rewinds a pilot).
    const bool inTrailingPause = _currentTapeBlock != nullptr && _currentTapeBlock->trailingPause &&
                                 !_currentTapeBlock->edgePulseTimings.empty() &&
                                 _currentOffsetWithinPulse + 1 == _currentTapeBlock->edgePulseTimings.size();

    if (inTrailingPause && _framesNotListened >= TAPE_GAP_HOLD_FRAMES)
    {
        ParkAtNextBlock();
    }
    else if (!inTrailingPause)
    {
        // Pause the playback: inside a pilot after TAPE_PILOT_HOLD_FRAMES (a loader may sit out a fixed delay
        // there). In the tail of the pilot the shorter TAPE_BLOCK_HOLD_FRAMES applies again: playback must be paused
        // (and the pilot rewound) before the head leaves the pilot, however short the pilot is
        const bool inPilot = _currentTapeBlock != nullptr && _currentOffsetWithinPulse < _currentTapeBlock->pilotEdgeCount;
        bool inPilotBody = inPilot;
        if (inPilot && _currentOffsetWithinPulse < _currentTapeBlock->edgePulseTimings.size())
        {
            const uint64_t pulsesLeft = _currentTapeBlock->pilotEdgeCount - _currentOffsetWithinPulse;
            const uint64_t tstatesLeft = pulsesLeft * _currentTapeBlock->edgePulseTimings[_currentOffsetWithinPulse];
            inPilotBody = tstatesLeft > TAPE_PILOT_TAIL_TSTATES;
        }
        if (_framesNotListened >= (inPilotBody ? TAPE_PILOT_HOLD_FRAMES : TAPE_BLOCK_HOLD_FRAMES))
            pausePlayback();
    }
}

/// endregion </Emulation events>

/// region <Helper methods>

TapeReadKind Tape::ClassifyPortRead()
{
    Z80& cpu = *_context->pCore->GetZ80();
    Memory& memory = *_context->pMemory;
    const uint16_t pc = cpu.pc;

    // Code in ROM (design §4.3): only the ROM's own LD-BYTES listens. Every
    // other ROM read is the keyboard scan, BREAK-KEY (TR-DOS calls it over and
    // over while it works the disk) or firmware, and never moves the tape.
    if (pc < 0x4000 && memory.IsBank0ROM())
    {
        const uint8_t* romBank = memory.GetPhysicalAddressForZ80Page(0);
        const bool ldBytes = pc >= 0x0556 && pc <= 0x0605 && romBank != nullptr && romBank[0x0564] == 0x1F;
        return ldBytes ? TapeReadKind::Ear : TapeReadKind::Key;
    }

    const uint64_t frame = _context->emulatorState.frame_counter;
    if (_classifiedValid && _classifiedPc == pc && _classifiedFrame == frame)
        return _classifiedKind;

    auto readByte = [](void* context, uint16_t address) -> uint8_t {
        return static_cast<Memory*>(context)->DirectReadFromZ80Memory(address);
    };

    _classifiedKind = TapeReadClassifier::Classify(readByte, &memory, pc);
    _classifiedPc = pc;
    _classifiedFrame = frame;
    _classifiedValid = true;

    return _classifiedKind;
}

bool Tape::IsListeningRead(TapeReadKind kind, uint64_t clockCount)
{
    if (kind == TapeReadKind::Ear)
        return true;

    if (kind == TapeReadKind::Key)
    {
        _patternRun = 0;
        return false;
    }

    // Other (design §4.2): an edge loop reads from the same IN a few hundred
    // T apart and moves one counter. A is left out, it holds the value read.
    // A wait loop that moves nothing never matches.
    Z80& cpu = *_context->pCore->GetZ80();
    const uint8_t regs[6] = { cpu.b, cpu.c, cpu.d, cpu.e, cpu.h, cpu.l };

    int moved = 0;
    for (int i = 0; i < 6; i++)
    {
        if (regs[i] != _patternRegs[i])
            moved++;
    }

    const uint64_t limit = _tapeStarted ? TAPE_PATTERN_GAP_PLAYING : TAPE_PATTERN_GAP_STOPPED;
    const bool continues = cpu.pc == _patternLastPc && clockCount >= _patternLastTick &&
                           clockCount - _patternLastTick <= limit && moved == 1;

    if (continues)
    {
        if (_patternRun < UINT16_MAX)
            _patternRun++;
    }
    else
    {
        _patternRun = 0;
    }

    _patternLastPc = cpu.pc;
    _patternLastTick = clockCount;
    std::memcpy(_patternRegs, regs, sizeof(_patternRegs));

    return _patternRun >= TAPE_PATTERN_RUN;
}

void Tape::ParkAtNextBlock()
{
    // Same replay fencing as pausePlayback()
    if (_context && _context->pTimeTravelHooks)
        _context->pTimeTravelHooks->RecordExternalEvent(
            ttd::TTDExternalEventKind::TapeControl, "tape park");

    _currentTapeBlockIndex++;
    _currentTapeBlock = nullptr;
    _currentOffsetWithinPulse = 0;
    _currentPulseIdxInBlock = 0;

    if (_currentTapeBlockIndex >= _tapeBlocks.size())
    {
        // The pause after the last block: this is the end of the tape
        stopPlayback();
        return;
    }

    MLOGINFO("Tape parked at block %zu (no loader listening)", _currentTapeBlockIndex);

    // Paused with no block in flight: the next start plays the cursor block
    // from its first pulse (ResumePlaybackAfterPoll -> StartPlaybackAtCursor)
    _tapeStarted = false;
    _muteEAR = false;
    _playbackFrozen = true;
    _currentClockCount = 0;
}

bool Tape::getTapeStreamBit(uint64_t clockCount)
{
    if (!_tapeStarted || _currentTapeBlockIndex == UINT64_MAX)
    {
        _currentClockCount = clockCount;
        return _tapeBitState;
    }

    if (_currentClockCount == 0 || clockCount <= _currentClockCount)
    {
        _currentClockCount = clockCount;
        return _tapeBitState;
    }

    uint64_t deltaTime = clockCount - _currentClockCount;
    _currentClockCount = clockCount;

    while (deltaTime > 0 && _currentTapeBlockIndex < _tapeBlocks.size())
    {
        // Regenerate also when the in-flight block's edges were dropped
        // (TTD restore recomputes the block pointer but not the derived edge
        // data; a pause spanning a seek can land on a cleared block)
        if (_currentTapeBlock == nullptr || _currentTapeBlock->edgePulseTimings.empty())
        {
            _currentTapeBlock = &_tapeBlocks[_currentTapeBlockIndex];
            generateBitstreamForStandardBlock(*_currentTapeBlock);
            // The position inside the block stays: zero for a block just
            // reached, the restored one after a TTD restore into a block whose
            // edges are not generated (freed when the deck moved on, or
            // freshly installed from the medium)
        }

        TapeBlock& block = *_currentTapeBlock;
        if (_currentOffsetWithinPulse >= block.edgePulseTimings.size())
        {
            _currentTapeBlockIndex++;
            _currentTapeBlock = nullptr;
            _currentOffsetWithinPulse = 0;
            _currentPulseIdxInBlock = 0;

            if (_currentTapeBlockIndex >= _tapeBlocks.size())
            {
                // Natural end of tape: stop playback, keep the image (cursor
                // sits at end-of-tape; a rewind or new insert restarts it).
                stopPlayback();
                break;
            }
            continue;
        }

        uint32_t currentPulseDuration = block.edgePulseTimings[_currentOffsetWithinPulse];
        uint32_t remainingInPulse = (currentPulseDuration > _currentPulseIdxInBlock)
                                      ? (currentPulseDuration - static_cast<uint32_t>(_currentPulseIdxInBlock))
                                      : 0;

        if (deltaTime < remainingInPulse)
        {
            _currentPulseIdxInBlock += deltaTime;
            deltaTime = 0;
        }
        else
        {
            deltaTime -= remainingInPulse;
            _currentOffsetWithinPulse++;
            _currentPulseIdxInBlock = 0;
            _tapeBitState = !_tapeBitState;  // Flip digital tape bit on pulse edge transition!

            if (_currentOffsetWithinPulse >= block.edgePulseTimings.size())
            {
                _currentTapeBlockIndex++;
                _currentTapeBlock = nullptr;
                _currentOffsetWithinPulse = 0;
                _currentPulseIdxInBlock = 0;

                if (_currentTapeBlockIndex >= _tapeBlocks.size())
                {
                    // Natural end of tape: stop playback, keep the image.
                    stopPlayback();
                    break;
                }
            }
        }
    }

    return _tapeBitState;
}

/// Generate bitstream assistive data for the TapeBlock data
/// @param tapeBlock Reference to single TapeBlock object
/// @return Result whether generating process finished successfully or not
bool Tape::generateBitstreamForStandardBlock(TapeBlock& tapeBlock)
{
    bool result = false;

    // Representation 3 (design §5.7) and empty control markers: no byte
    // payload means generateBitstream() must not run — for pulse blocks it
    // would append a second, wrong encoding on top of the loader-supplied
    // train, for empty control entries it would emit a spurious pilot+pause.
    if (tapeBlock.data.empty())
    {
        return !tapeBlock.edgePulseTimings.empty();
    }

    size_t totalBlockDuration = 0;

    if (tapeBlock.timing.has_value())
    {
        // Representation 2 (design §5.7): byte payload with a Custom profile —
        // TZX $11 turbo / $14 pure data. The profile mirrors this function's
        // parameters 1:1, so generation is a straight pass-through. A zero
        // pilotPulses ($14) skips pilot+sync entirely, exactly like the engine
        // path below.
        const TapeTimingProfile& profile = *tapeBlock.timing;
        totalBlockDuration = generateBitstream(tapeBlock,
                                               profile.pilotHalfPeriod,
                                               profile.sync1,
                                               profile.sync2,
                                               profile.zeroHalfPeriod,
                                               profile.oneHalfPeriod,
                                               profile.pilotPulses,
                                               profile.pauseMs,
                                               profile.bitsInLastByte);
    }
    else
    {
        // Representation 1: ROM-standard encoding — TAP and TZX $10
        bool isHeader = tapeBlock.type == TAP_BLOCK_FLAG_HEADER;

        totalBlockDuration =
            generateBitstream(tapeBlock, PILOT_TONE_HALF_PERIOD, PILOT_SYNCHRO_1, PILOT_SYNCHRO_2, ZERO_ENCODE_HALF_PERIOD,
                              ONE_ENCODE_HALF_PERIOD, isHeader ? PILOT_DURATION_HEADER : PILOT_DURATION_DATA, 1000);
    }

    if (totalBlockDuration > 0)
    {
        result = true;
    }

    return result;
}

size_t Tape::generateBitstream(TapeBlock& tapeBlock, uint32_t pilotHalfPeriod_tStates, uint32_t synchro1_tStates,
                               uint32_t synchro2_tStates, uint32_t zeroEncodingHalfPeriod_tState,
                               uint32_t oneEncodingHalfPeriod_tStates, size_t pilotLength_pulses, size_t pause_ms,
                               uint8_t bitsInLastByte)
{
    // Signal half-periods come from the shared pure generator (tapepulsegen —
    // tape-audio-bridge design §4.3); the engine-side addition is the pause
    // hold-edge the playback cursor semantics rely on (1 ms == 3500 T-states).
    uint64_t signalTotal = TapePulseGen::GenerateHalfPeriods(tapeBlock.data,
                                                             pilotHalfPeriod_tStates, synchro1_tStates, synchro2_tStates,
                                                             zeroEncodingHalfPeriod_tState, oneEncodingHalfPeriod_tStates,
                                                             pilotLength_pulses, bitsInLastByte,
                                                             tapeBlock.edgePulseTimings);

    size_t result = static_cast<size_t>(signalTotal);

    tapeBlock.pilotEdgeCount = signalTotal > 0 ? pilotLength_pulses : 0;
    tapeBlock.trailingPause = pause_ms > 0;

    if (pause_ms)
    {
        // Pause doesn't require any encoding, just a time mark after the delay
        size_t pauseDuration = pause_ms * 3500;
        tapeBlock.edgePulseTimings.push_back(pauseDuration);

        result += pauseDuration;
    }

    tapeBlock.totalBitstreamLength = result;

    return result;
}

/// endregion </Helper methods>

// TODO: just experimentation method
bool Tape::getPilotSample(size_t clockCount)
{
    [[maybe_unused]] static uint16_t counter = 0;
    static constexpr uint16_t PILOT_HALF_PERIOD = 2168;
    static constexpr uint16_t PILOT_PERIOD = PILOT_HALF_PERIOD * 2;

    size_t normalizedToPeriod = (clockCount % PILOT_PERIOD);
    bool result = (normalizedToPeriod < PILOT_HALF_PERIOD);

    /*
    bool result = (counter < PILOT_HALF_PERIOD);
    counter += (tState - counter);

    if (counter >= PILOT_PERIOD)
    {
        counter = 0;
    }

    if (result)
    {
        counter = counter;
    }
    else
    {
        counter = counter;
    }
    */

    size_t frameCounter = _context->emulatorState.frame_counter;
    size_t tState = _context->pCore->GetZ80()->t;
    MLOGINFO("Frame: %04d tState: %05d clockCount: %08d pilot: %d", frameCounter, tState, clockCount, result);

    return result;
}

/// region <TTDSerializable (P1.5 — parent TDD §6.4, §4 row 3)>
//
// Cursor-packed layout (53 bytes, alignment-safe via per-field memcpy):
//
//   Offset  Size  Field
//   ------  ----  ----------------------------------------
//   0        1    _tapeStarted (0/1)
//   1        1    _playbackFrozen (0/1)
//   2        8    _tapePosition
//   10       8    _currentTapeBlockIndex
//   18       8    _currentPulseIdxInBlock
//   26       8    _currentOffsetWithinPulse
//   34       8    _currentClockCount
//   42       1    _tapeBitState (EAR level the CPU reads on port #FE bit 6)
//   43       1    _lastTapeBit (edge detection of the band-limited EAR step)
//   44       1    reserved, written as 0 and ignored on load (was the
//                 ERR_NR baseline of the removed ERR_NR stop)
//   45       4    _framesNotListened (frames without a listening read)
//   49       4    _listenReadsThisFrame (listening reads this frame)
//   53       2    _patternRun (loader pattern run length)
//   55       2    _patternLastPc
//   57       8    _patternLastTick
//   65       6    _patternRegs (B, C, D, E, H, L at the last pattern read)
//   ------  ---
//   71 bytes total
//
// Bytes 42-43 and 45-70 decide what the program reads and when the tape
// pauses, parks or starts: left out, a restore kept their live values and a
// replay diverged from the recording as soon as the tape was involved.
//
// size_t is serialized as uint64_t (the position indices never approach 2^63;
// this keeps the format identical on 32-bit and 64-bit hosts).

namespace
{
inline void put_u8 (uint8_t*& cur, uint8_t v)   { *cur++ = v; }
inline void put_u16(uint8_t*& cur, uint16_t v) { std::memcpy(cur, &v, 2); cur += 2; }
inline void put_u32(uint8_t*& cur, uint32_t v) { std::memcpy(cur, &v, 4); cur += 4; }
inline void put_u64(uint8_t*& cur, uint64_t v) { std::memcpy(cur, &v, 8); cur += 8; }

inline uint8_t  get_u8 (const uint8_t*& cur)   { return *cur++; }
inline uint16_t get_u16(const uint8_t*& cur)   { uint16_t v; std::memcpy(&v, cur, 2); cur += 2; return v; }
inline uint32_t get_u32(const uint8_t*& cur)   { uint32_t v; std::memcpy(&v, cur, 4); cur += 4; return v; }
inline uint64_t get_u64(const uint8_t*& cur)   { uint64_t v; std::memcpy(&v, cur, 8); cur += 8; return v; }
} // anonymous namespace

static constexpr size_t kTapeStateSize = 2 + 5 * 8 + 3 + 2 * 4 + 2 + 2 + 8 + 6;  // = 71
static_assert(kTapeStateSize == 71, "Tape state size drift");

size_t Tape::TTDStateSize() const
{
    return kTapeStateSize;
}

void Tape::TTDSaveState(uint8_t* dst) const
{
    uint8_t* cur = dst;
    put_u8 (cur, _tapeStarted ? 1 : 0);
    put_u8 (cur, _playbackFrozen ? 1 : 0);
    put_u64(cur, static_cast<uint64_t>(_tapePosition));
    put_u64(cur, static_cast<uint64_t>(_currentTapeBlockIndex));
    put_u64(cur, static_cast<uint64_t>(_currentPulseIdxInBlock));
    put_u64(cur, static_cast<uint64_t>(_currentOffsetWithinPulse));
    put_u64(cur, _currentClockCount);
    put_u8 (cur, _tapeBitState ? 1 : 0);
    put_u8 (cur, _lastTapeBit ? 1 : 0);
    put_u8 (cur, 0);  // reserved
    put_u32(cur, _framesNotListened);
    put_u32(cur, _listenReadsThisFrame);
    put_u16(cur, _patternRun);
    put_u16(cur, _patternLastPc);
    put_u64(cur, _patternLastTick);
    std::memcpy(cur, _patternRegs, sizeof(_patternRegs));
    cur += sizeof(_patternRegs);
}

void Tape::TTDLoadState(const uint8_t* src)
{
    // The blocks are installed from the attached medium lazily, when a loader
    // first reads the tape; a restored state with a block in flight needs them
    // now (installing resets the cursor, so before the restored one is set
    // below). A state with none in flight stays as lazy as the recorded deck
    uint64_t restoredBlock = 0;
    std::memcpy(&restoredBlock, src + 10, sizeof(restoredBlock));   // _currentTapeBlockIndex (layout above)
    if (_tapeBlocks.empty() && restoredBlock != UINT64_MAX)
        EnsureImageLoaded();

    const uint8_t* cur = src;
    _tapeStarted              = (get_u8(cur) != 0);
    _playbackFrozen           = (get_u8(cur) != 0);
    _tapePosition             = static_cast<size_t>(get_u64(cur));
    _currentTapeBlockIndex    = static_cast<size_t>(get_u64(cur));
    _currentPulseIdxInBlock   = static_cast<size_t>(get_u64(cur));
    _currentOffsetWithinPulse = static_cast<size_t>(get_u64(cur));
    _currentClockCount        = get_u64(cur);
    _tapeBitState             = (get_u8(cur) != 0);
    _lastTapeBit              = (get_u8(cur) != 0);
    get_u8(cur);  // reserved
    _framesNotListened        = get_u32(cur);
    _listenReadsThisFrame     = get_u32(cur);
    _patternRun               = get_u16(cur);
    _patternLastPc            = get_u16(cur);
    _patternLastTick          = get_u64(cur);
    std::memcpy(_patternRegs, cur, sizeof(_patternRegs));
    cur += sizeof(_patternRegs);

    // The classification cache is derived from memory; rebuild it on the next read
    _classifiedValid = false;

    // Recompute the derived _currentTapeBlock pointer from the restored index.
    // Tape content (_tapeBlocks) is invariant within a session — it is NOT
    // part of the checkpoint (parent TDD §4 row 3). On restore (always within
    // the same session), the content vector is unchanged, so the index is
    // still valid. A bounds check guards against a corrupt/out-of-range index.
    if (!_tapeBlocks.empty() && _currentTapeBlockIndex < _tapeBlocks.size())
    {
        _currentTapeBlock = &_tapeBlocks[_currentTapeBlockIndex];
    }
    else
    {
        // Content not loaded or index stale — leave the pointer null. This is
        // the correct state for a tape that isn't actively playing content.
        _currentTapeBlock = nullptr;
    }

    // Note: _tapeBlocks, _lpfFilter, _dcFilter, _muteEAR, _context are
    // intentionally not restored — see the header doc for the exclusion list.
}

/// endregion </TTDSerializable>
