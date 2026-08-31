#include "tape.h"

#include "common/stringhelper.h"
#include "emulator/cpu/core.h"
#include "emulator/emulatorcontext.h"
#include "emulator/sound/soundmanager.h"
#include "emulator/spectrumconstants.h"
#include "loaders/tape/loader_tap.h"
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
    if (_context && _context->pTimeTravelManager)
        _context->pTimeTravelManager->RecordExternalEvent(
            ttd::TTDExternalEventKind::TapeControl, "tape play");

    _tapeStarted = true;
    _muteEAR = true;
    _lastTapeBit = false;
    _framesSinceLastRead = 0;
    _initialErrNr = _context->pMemory->DirectReadFromZ80Memory(SystemVariables48k::ERR_NR);
    MLOGINFO("Tape started, initial ERR_NR=0x%02X", _initialErrNr);
}

void Tape::stopTape()
{
    // Phase 2 Item 6 - see startTape() for rationale.
    if (_context && _context->pTimeTravelManager)
        _context->pTimeTravelManager->RecordExternalEvent(
            ttd::TTDExternalEventKind::TapeControl, "tape stop");

    _tapeStarted = false;
    _muteEAR = false;

    // Reset all tape-related fields and free up blocks memory.
    // This is the tape-control stop: the image is invalidated (design §9.4),
    // so the loaded-path key must be dropped as well or EnsureImageLoaded()
    // would wrongly consider the (now empty) image fresh.
    _tapeBlocks = std::vector<TapeBlock>();
    _imageLoadedPath.clear();
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
    if (_context && _context->pTimeTravelManager)
        _context->pTimeTravelManager->RecordExternalEvent(
            ttd::TTDExternalEventKind::TapeControl, "tape stop");

    // Partially played block counts as consumed (design §9.4): advance the
    // cursor past the in-flight block. A block pointer already null means
    // playback sat exactly between two blocks — the cursor is already synced.
    if (_currentTapeBlock != nullptr && _currentTapeBlockIndex != UINT64_MAX)
        _currentTapeBlockIndex++;

    _tapeStarted = false;
    _muteEAR = false;
    _currentTapeBlock = nullptr;
    _currentPulseIdxInBlock = 0;
    _currentOffsetWithinPulse = 0;
    _lastTapeBit = false;
}

bool Tape::EnsureImageLoaded()
{
    const std::string& path = _context->coreState.tapeFilePath;

    // Idempotent and path-keyed (design §9.4): an unchanged path (including
    // the empty "no tape selected" path) never re-parses over live blocks.
    if (_imageLoadedPath == path)
        return !_tapeBlocks.empty();

    if (path.empty())
        return false;

    LoaderTAP loader(_context);
    _tapeBlocks = loader.loadTAP(path);
    _imageLoadedPath = path;

    // Fresh image: consumption cursor at the first block, playback state reset.
    _currentTapeBlock = nullptr;
    _currentTapeBlockIndex = UINT64_MAX;
    _currentPulseIdxInBlock = 0;
    _currentOffsetWithinPulse = 0;

    MLOGINFO("Tape image loaded: '%s', blocks: %zu", path.c_str(), _tapeBlocks.size());

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
    // handleFrameStart() / getTapeStreamBit() pass.
    _currentTapeBlock = nullptr;

    startTape();
}
/// endregion </Tape control methods>

void Tape::reset()
{
    // Phase 2 Item 6 - distinguish "user-driven rewind" from "system-level
    // reset". The constructor calls reset() before _context is fully wired;
    // CLI/WebAPI rewind calls it after pausing the emulator. Recording is
    // guarded below, so no caller-passed flag is needed.
    const bool wasStarted = _tapeStarted;

    _tapeStarted = false;
    _tapePosition = 0LL;

    // Tape input bitstream related
    _tapeBlocks = std::vector<TapeBlock>();
    _imageLoadedPath.clear();
    _currentTapeBlock = nullptr;
    _currentTapeBlockIndex = UINT64_MAX;
    _currentPulseIdxInBlock = 0;
    _currentOffsetWithinPulse = 0;

    _currentClockCount = 0;
    _lastTapeBit = false;
    // Phase 2 Item 6 - only record a rewind marker when reset() actually
    // changes tape state mid-session. A no-op reset (constructor, system
    // reset before any session) must not pollute the journal.
    if (wasStarted && _context && _context->pTimeTravelManager)
        _context->pTimeTravelManager->RecordExternalEvent(
            ttd::TTDExternalEventKind::TapeControl, "tape rewind");

};

/// region <Port events>

uint8_t Tape::handlePortIn()
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

    if (_tapeStarted)
    {
        // Reset frame counter - loader is actively reading
        _framesSinceLastRead = 0;

        // Use monotonic counter for tape timing (t_states + t)
        uint64_t clockCount = _context->emulatorState.t_states + cpu.t;

        bool tapeBit = getTapeStreamBit(clockCount);
        result = (uint8_t)tapeBit << 6;
    }
    else
    {
        /// region <Imitate analogue noise>
        static uint16_t counter = 0;
        [[maybe_unused]] static uint8_t prevValue = 0;
        static uint16_t prngState = std::rand();

        if (counter == 0)
        {
            result = prngState & 0b0100'0000;

            prngState = std::rand();
        }

        // Galois LFSR with 16-bit register
        // The polynomial used in this implementation is x^16 + x^5 + x^3 + x^2 + 1, which has a maximal period of
        // 2^16-1, or 65,535 values
        uint16_t bit = (prngState >> 0) ^ (prngState >> 2) ^ (prngState >> 3) ^ (prngState >> 5);
        prngState = (prngState >> 1) | (bit << 15);

        /*
        // Simple XORshift algorithm for PRNG
        prngState ^= prngState << 7;
        prngState ^= prngState >> 5;
        prngState ^= prngState << 3;
        */

        counter++;
        /// endregion </Imitate analogue noise>

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
    // Use monotonic counter for tape timing (t_states + t)
    uint64_t clockCount = _context->emulatorState.t_states + _context->pCore->GetZ80()->t;

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
                // may legitimately sit at block 0, e.g. after StartPlaybackAtCursor)
                if (_currentTapeBlockIndex > 0)
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
    uint64_t clockCount = _context->emulatorState.t_states + tState;

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

    // Check ERR_NR - ROM sets error code on break/error (immediate detection)
    // System variables are in RAM at same addresses regardless of which ROM is paged
    uint8_t errNr = _context->pMemory->DirectReadFromZ80Memory(SystemVariables48k::ERR_NR);
    if (errNr != _initialErrNr)
    {
        MLOGINFO("Tape stopped: ERR_NR changed from 0x%02X to 0x%02X", _initialErrNr, errNr);
        stopPlayback();
        return;
    }

    // Track frames since last tape read (backup detection for load complete)
    // 128K mode has longer gaps between reads due to ROM switching
    _framesSinceLastRead++;

    // 150 frames (~3 seconds) without reads = loader exited
    if (_framesSinceLastRead > 150)
    {
        stopPlayback();
    }
}

/// endregion </Emulation events>

/// region <Helper methods>

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
        if (_currentTapeBlock == nullptr)
        {
            _currentTapeBlock = &_tapeBlocks[_currentTapeBlockIndex];
            generateBitstreamForStandardBlock(*_currentTapeBlock);
            _currentOffsetWithinPulse = 0;
            _currentPulseIdxInBlock = 0;
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

    bool isHeader = tapeBlock.type == TAP_BLOCK_FLAG_HEADER;

    size_t totalBlockDuration =
        generateBitstream(tapeBlock, PILOT_TONE_HALF_PERIOD, PILOT_SYNCHRO_1, PILOT_SYNCHRO_2, ZERO_ENCODE_HALF_PERIOD,
                          ONE_ENCODE_HALF_PERIOD, isHeader ? PILOT_DURATION_HEADER : PILOT_DURATION_DATA, 1000);

    if (totalBlockDuration > 0)
    {
        result = true;
    }

    return result;
}

size_t Tape::generateBitstream(TapeBlock& tapeBlock, uint16_t pilotHalfPeriod_tStates, uint16_t synchro1_tStates,
                               uint16_t synchro2_tStates, uint16_t zeroEncodingHalfPeriod_tState,
                               uint16_t oneEncodingHalfPeriod_tStates, size_t pilotLength_periods, size_t pause_ms)
{
    size_t result = 0;
    size_t len = tapeBlock.data.size();

    // Calculate collection size to fit all edge time intervals
    size_t resultSize = 0;
    resultSize += pilotLength_periods;        // Pilot length is specified in pulses (half-periods), one edge each
    resultSize += 2;                          // Two sync pulses at the end of pilot
    resultSize += (len * 8 * 2);              // Each byte split to bits and each bit encoded as 2 edges
    if (pause_ms > 0)
        resultSize += 1;  // Pause is just a marker so single edge is sufficient

    tapeBlock.edgePulseTimings.reserve(resultSize);

    /// region <Pilot tone + sync>

    if (pilotLength_periods > 0)
    {
        // Pilot length is specified in pulses (half-periods), matching the TAP
        // convention (header: 8063-8064 pulses, data: ~3220 pulses). Emitting
        // 2x here would double the real pilot duration (~10s instead of ~5s).
        for (size_t i = 0; i < pilotLength_periods; i++)
        {
            tapeBlock.edgePulseTimings.push_back(pilotHalfPeriod_tStates);

            result += pilotHalfPeriod_tStates;
        }

        // Sync pulses at the end of pilot
        tapeBlock.edgePulseTimings.push_back(synchro1_tStates);
        tapeBlock.edgePulseTimings.push_back(synchro2_tStates);

        result += synchro1_tStates;
        result += synchro2_tStates;
    }

    /// endregion </Pilot tone + sync>

    /// region <Data paramBytes>

    for (size_t i = 0; i < len; i++)
    {
        // Extract bits from input data byte and add correspondent bit encoding length to image array
        for (uint8_t bitMask = 0x80; bitMask != 0; bitMask >>= 1)
        {
            bool bit = (tapeBlock.data[i] & bitMask) != 0;
            uint16_t bitEncoded = bit ? oneEncodingHalfPeriod_tStates : zeroEncodingHalfPeriod_tState;

            // Each bit is encoded by two edges; count both so
            // totalBitstreamLength equals the sum of edgePulseTimings
            tapeBlock.edgePulseTimings.push_back(bitEncoded);
            tapeBlock.edgePulseTimings.push_back(bitEncoded);

            result += bitEncoded;
            result += bitEncoded;
        }
    }

    /// endregion </Data paramBytes>

    /// region <Pause>

    if (pause_ms)
    {
        // Pause doesn't require any encoding, just a time mark after the delay
        size_t pauseDuration = pause_ms * 3500;
        tapeBlock.edgePulseTimings.push_back(pauseDuration);

        result += pauseDuration;
    }

    /// endregion </Pause>

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
// Cursor-packed layout (41 bytes, alignment-safe via per-field memcpy):
//
//   Offset  Size  Field
//   ------  ----  ----------------------------------------
//   0        1    _tapeStarted (0/1)
//   1        8    _tapePosition
//   9        8    _currentTapeBlockIndex
//   17       8    _currentPulseIdxInBlock
//   25       8    _currentOffsetWithinPulse
//   33       8    _currentClockCount
//   ------  ---
//   41 bytes total
//
// size_t is serialized as uint64_t (the position indices never approach 2^63;
// this keeps the format identical on 32-bit and 64-bit hosts).

namespace
{
inline void put_u8 (uint8_t*& cur, uint8_t v)   { *cur++ = v; }
inline void put_u64(uint8_t*& cur, uint64_t v) { std::memcpy(cur, &v, 8); cur += 8; }

inline uint8_t  get_u8 (const uint8_t*& cur)   { return *cur++; }
inline uint64_t get_u64(const uint8_t*& cur)   { uint64_t v; std::memcpy(&v, cur, 8); cur += 8; return v; }
} // anonymous namespace

static constexpr size_t kTapeStateSize = 1 + 5 * 8;  // = 41
static_assert(kTapeStateSize == 41, "Tape state size drift");

size_t Tape::TTDStateSize() const
{
    return kTapeStateSize;
}

void Tape::TTDSaveState(uint8_t* dst) const
{
    uint8_t* cur = dst;
    put_u8 (cur, _tapeStarted ? 1 : 0);
    put_u64(cur, static_cast<uint64_t>(_tapePosition));
    put_u64(cur, static_cast<uint64_t>(_currentTapeBlockIndex));
    put_u64(cur, static_cast<uint64_t>(_currentPulseIdxInBlock));
    put_u64(cur, static_cast<uint64_t>(_currentOffsetWithinPulse));
    put_u64(cur, _currentClockCount);
}

void Tape::TTDLoadState(const uint8_t* src)
{
    const uint8_t* cur = src;
    _tapeStarted              = (get_u8(cur) != 0);
    _tapePosition             = static_cast<size_t>(get_u64(cur));
    _currentTapeBlockIndex    = static_cast<size_t>(get_u64(cur));
    _currentPulseIdxInBlock   = static_cast<size_t>(get_u64(cur));
    _currentOffsetWithinPulse = static_cast<size_t>(get_u64(cur));
    _currentClockCount        = get_u64(cur);

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
