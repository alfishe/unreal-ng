#pragma once

#include "stdafx.h"

#include "debugger/ttd/ttd_serializable.h"  // TTDSerializable (P1.5 peripheral serializer)
#include "emulator/io/tape/tapetypes.h"      // tape vocabulary types (design §5.1a leaf header)
#include "emulator/io/tape/tapecatalog.h"    // TapeFastLoadPlan (§5.8) — leaf, no cycle
#include "emulator/platform.h"
#include "common/sound/filters/filter_dc.h"
#include "common/sound/filters/filter_lpf.h"


class EmulatorContext;
class ModuleLogger;

/// region <Constants>

constexpr uint16_t PILOT_TONE_HALF_PERIOD = 2168;       // Pilot tone has 2168 t-states half-period
constexpr uint16_t PILOT_SYNCHRO_1 = 667;               // At the end of pilot two synchro pulses are generated. First with 667 t-states duration
constexpr uint16_t PILOT_SYNCHRO_2 = 735;               //  second - with 735 t-states duration
constexpr uint16_t PILOT_DURATION_HEADER = 8064;        // Pilot for header block lasts for 3220 full period cycles (8064 * 2168 * 2)
constexpr uint16_t PILOT_DURATION_DATA = 3220;          // Pilot for data block lasts for 3220 full period cycles (3220 * 2168 * 2)
constexpr uint16_t ZERO_ENCODE_HALF_PERIOD = 855;       // Zeroes encoded as two 855 t-states half-periods
constexpr uint16_t ONE_ENCODE_HALF_PERIOD = 1710;       // One encoded as two 1710 t-states half-periods
constexpr uint16_t TAPE_PAUSE_BETWEEN_BLOCKS = 1000;    // 1000ms

// Sustained EAR-polling resume threshold (reads per frame). A loader's
// pilot/data poll loop reads the ULA port ~1000+ times per frame; the ROM
// keyboard scan reads the 8 half-row ports ~8 times per frame. 256 sits
// safely between the two, so a RAM-resident custom loader resumes paused
// playback within one frame of polling, while keyboard and menu activity
// can never reach it. Tight game loops polling the two Sinclair-joystick
// rows are excluded by port — see Tape::IsJoystickPollPort.
constexpr uint16_t TAPE_EAR_POLL_RESUME_THRESHOLD = 256;

/// endregion </Constants>

/// region </Types — moved>

// ZXTapeBlockTypeEnum, TapeBlockFlagEnum and TapeBlock moved to tapetypes.h
// (design §5.1a — the pure-data leaf that breaks the tape.h <-> tapecatalog.h
// include cycle). They remain visible here unchanged: same names, same global
// namespace, now simply declared one header down the dependency chain. TapeBlock
// gained `std::optional<TapeTimingProfile> timing` there — nullopt preserves
// today's ROM-standard encoding for every existing TAP image and test.

/// endregion </Types — moved>

/// region <Playback state and position (design §6.1)>

/// Coarse playback state for every control plane. `Paused` is the frozen
/// position the read-gap watchdog / manual pause leaves behind (in-flight
/// block and pulse cursor survive); `Ended` is the natural end-of-tape stop
/// (image and cursor survive, the cursor sits past the last block).
enum class TapePlaybackState : uint8_t
{
    Idle,
    Playing,
    Paused,
    Ended
};

/// Point-in-time playback position. `blockIndex` is the in-flight block
/// (Playing/Paused) or the next-up one (Idle); equal to the block count it
/// means end-of-tape (Ended). Zeroed pulse fields unless a block is in
/// flight.
struct TapePosition
{
    size_t blockIndex = 0;          // in-flight (Playing/Paused) or next-up (Idle/Ended) block
    size_t pulseIndex = 0;          // index into edgePulseTimings
    size_t offsetWithinPulse = 0;   // T-states consumed inside current pulse
    double secondsIntoBlock = 0.0;  // derived: elapsed pulse durations / 3.5 MHz
    double blockTotalSeconds = 0.0; // from catalog descriptor
};

/// endregion </Playback state and position>

/// A 'pulse' here is either a mark or a space, so 2 pulses makes a complete square wave cycle.
/// Pilot tone: before each block is a sequence of 8064 (header) or 3220 (data) pulses, each of length 2168 T-states.
/// Sync pulses: the pilot tone is followed by two sync pulses of 667 and 735 T-states respectively
/// A '0' bit is encoded as 2 pulses of 855 T-states each.
/// A '1' bit is encoded as 2 pulses of 1710 T-states each (ie. twice the length of a '0')
///
/// The initial polarity of the signal does not matter - everything in the ROM loader is edge-triggered rather than level-triggered.
/// @see https://sinclair.wiki.zxnet.co.uk/wiki/Spectrum_tape_interface

/// Tape signal is frequency-modulation encoded
/// Signal types:
/// 1. Pilot tone - 807Hz (2168 high + 2168 low Z80 t-states @3.5MHz). Pilot Freq = 3500000 / (2168 + 2168) = 808Hz
///    Pilot tone duration (PILOT_DURATION_HEADER / PILOT_DURATION_DATA — pulses, one edge each):
///       - 8064 pulses - for the header
///       - 3220 pulses - for data block
/// 2. Synchronization signal - asymmetrical: 667 t-states high (190.6 uS) and 735 t-states low (210 uS)
/// 3. Data: 0-encoding - 2047Hz (855 high + 855 low t-states). Zero encoding Freq = 3500000 / (855 + 855) = 2047Hz
/// 4. Data: 1-encoding - 1023Hz (1710 high + 1710 low t-states). One encoding Freq = 3500000 / (1710 + 1710) = 1023Hz
///
/// The cassette loading routines have a great tolerance, and will allow variations in the speed of up to +/-15%
/// @see https://retrocomputing.stackexchange.com/questions/15810/zx-spectrum-red-stripes-during-loading
class Tape : public ttd::TTDSerializable
{
    /// region <ModuleLogger definitions for Module/Submodule>
public:
    const PlatformModulesEnum _MODULE = PlatformModulesEnum::MODULE_IO;
    const uint16_t _SUBMODULE = PlatformIOSubmodulesEnum::SUBMODULE_IO_TAPE;
    ModuleLogger* _logger;
    /// endregion </ModuleLogger definitions for Module/Submodule>

    /// region <Fields>
protected:
    EmulatorContext* _context;

    bool _tapeStarted = false;

    // Frozen-position flag (design §6.1): set by pausePlayback(), cleared by
    // every state-changing control (start/stop/seek/rewind/new image). Makes
    // Paused queryable — the freeze was implicit in a live _currentTapeBlock
    // before, invisible to GetPlaybackState().
    bool _playbackFrozen = false;

    size_t _tapePosition = 0;

    bool _muteEAR = false;              // Mute EAR output when active tape loading is done (prevent noise clicks)

    // Tape input bitstream related
    std::vector<TapeBlock> _tapeBlocks; // Tape representation as parsed TapeBlock vector

    // Per-block catalog derived from _tapeBlocks (design §5.6): same
    // indexing, same invalidation point — all three die together on
    // stopTape()/reset()/new insert. Built once per image load inside
    // EnsureImageLoaded(), never per frame.
    std::vector<TapeBlockDescriptor> _catalog;

    // Whole-image turbo verdict computed beside the catalog (design §5.8).
    // Advisory only — never gates the runtime trap (honesty contract).
    TapeFastLoadPlan _fastLoadPlan;

    // Path the live _tapeBlocks were parsed from ("" = no image loaded). Key for
    // EnsureImageLoaded() idempotency: only a path change (new insert) re-parses.
    std::string _imageLoadedPath;

    // Format id of the loader that produced the live blocks ("tap"/"tzx"/...).
    // Set beside _imageLoadedPath, cleared with it — surfaces report the format
    // the content probe actually selected, not the extension (design §7).
    std::string _imageFormatId;

    TapeBlock* _currentTapeBlock;       // Shortcut to current block object
    // Consumption cursor: index of the NEXT block to deliver to the CPU, by signal
    // playback or by the fast-loading trap (single source of truth — design §9.4).
    // UINT64_MAX is the "nothing consumed yet / not started" sentinel. During signal
    // playback of block k the field holds k (the in-flight block); a watchdog stop
    // mid-block advances it past k (partially played counts as consumed).
    size_t _currentTapeBlockIndex;
    size_t _currentPulseIdxInBlock;     // Index in TapeBlock::edgePulseTimings vector
    size_t _currentOffsetWithinPulse;   // How many pulses already processed within single TapeBlock::edgePulseTimings vector element
    uint64_t _currentClockCount;        // Store clock count for next iteration
    bool _lastTapeBit = false;          // Last tape bit state for band-limited step edge detection
    bool _tapeBitState = false;         // Digital signal output level of current tape pulse

    uint8_t _initialErrNr = 0;          // ERR_NR value when tape started (to detect change)
    uint32_t _framesSinceLastRead = 0;  // Frames since last tape IN read (to detect loader exit)

    // Non-keyboard-row ULA port reads since the current frame started. Drives
    // the sustained EAR-polling resume (loader-agnostic signal auto-start);
    // reset every handleFrameStart().
    uint32_t _earPollsThisFrame = 0;

    /// endregion </Fields>

    /// region <Constructors / Destructors>
public:
    Tape() = delete;    // Disable default constructor. C++ 11 feature
    Tape(EmulatorContext* context);
    virtual ~Tape();
    /// endregion </Constructors / Destructors>

    /// region <Tape control methods>
public:
    void reset();
    void startTape();
    void stopTape();

    /// Stop playback WITHOUT invalidating the image: the consumption cursor
    /// advances past a partially played block (design §9.4 — a real tape keeps
    /// rolling; the ROM loader resynchronizes on the next pilot tone, never
    /// mid-block). Used by the load-completion watchdogs and natural end-of-tape.
    /// Tape-control commands (stop / eject / rewind / new insert) keep using
    /// stopTape() / reset(), which drop the image as well.
    void stopPlayback();

    /// Pause playback WITHOUT consuming anything: freeze the head exactly
    /// where it is — the in-flight block, its pulse position and the last EAR
    /// level all survive, so a later ResumePlaybackAfterPoll() continues the
    /// bitstream mid-block (like un-pausing a real deck). Used by the read-gap
    /// watchdog when a multi-stage loader stops polling while it processes
    /// (decompression, bank switching) — the terminal stopPlayback() would
    /// consume the partially heard block and lose it.
    void pausePlayback();

    /// Resume (or first-start) signal playback triggered by sustained EAR
    /// polling from ANY code — RAM-resident custom loaders never reach the ROM
    /// $0562/$0564 anchor, so the read-gap pause must be recoverable for them.
    /// With a frozen mid-block position it un-pauses in place (level
    /// continuity preserved); otherwise it positions at the consumption
    /// cursor like StartPlaybackAtCursor().
    void ResumePlaybackAfterPoll();
    /// endregion </Tape control methods>

    /// region <Image and consumption cursor interface (fast tape loading)>
public:
    /// Lazily parse coreState.tapeFilePath into _tapeBlocks. Idempotent and
    /// path-keyed: re-parses only when the path differs from the one the live
    /// blocks came from (never re-parses over live blocks — that would reset the
    /// consumption cursor and dangle _currentTapeBlock). Returns true when blocks
    /// are available.
    bool EnsureImageLoaded();

    /// Index of the next block to deliver (signal or trap). The UINT64_MAX
    /// sentinel maps to 0 for external observers.
    size_t GetConsumptionCursor() const;

    /// Advance the consumption cursor past block `index` (trap consumption path).
    void ConsumeBlock(size_t index);

    /// Start signal playback honoring the consumption cursor (signal fallback
    /// path). No-op without blocks; a cursor at end-of-tape leaves the tape off.
    void StartPlaybackAtCursor();

    /// Direct read access to the parsed blocks (UI / trap component / tests).
    const std::vector<TapeBlock>& GetBlocks() const { return _tapeBlocks; };

    /// Per-block catalog, coherent with GetBlocks() (same indexing, same
    /// invalidation). Empty until an image is loaded (FR-2).
    const std::vector<TapeBlockDescriptor>& GetBlockCatalog() const { return _catalog; };

    /// Whole-image fast-load pre-analysis (design §5.8), coherent with
    /// GetBlockCatalog(). Default-constructed (Empty verdict) until an image
    /// loads. Advisory: the runtime trap matrix remains the sole authority.
    const TapeFastLoadPlan& GetFastLoadPlan() const { return _fastLoadPlan; };

    /// Registry format id of the loaded image ("tap"/"tzx"/...), empty when
    /// none — the id of the loader the content probe selected (design §7).
    const std::string& GetLoadedFormatId() const { return _imageFormatId; };

    /// Whether signal playback is currently active.
    bool IsPlaying() const { return _tapeStarted; };
    /// endregion </Image and consumption cursor interface>

    /// region <Playback state, position and seek (design §6)>
public:
    /// Coarse playback state (FR-3). `Idle` when no image is loaded.
    TapePlaybackState GetPlaybackState() const;

    /// Position snapshot (FR-3): the in-flight block (with its pulse cursor
    /// and elapsed signal time) while Playing/Paused, otherwise the next-up
    /// block. nullopt: no image loaded.
    std::optional<TapePosition> GetPosition() const;

    /// Position the tape so the next delivery (signal or trap) starts at
    /// block `index`'s pilot tone (FR-4). Forward and backward, including
    /// already-consumed blocks; never starts playback (seek arms, play
    /// delivers). False: no image / out of range. Seeking the current index
    /// is a legal "restart this block" call.
    bool SeekToBlock(size_t index);

    /// Rewind = seek to block 0, image and catalog kept (FR-5) — unlike
    /// legacy reset(), which dropped the image as well.
    void RewindToStart();

    /// Manual un-pause of a frozen position (FR-6): continues the bitstream
    /// mid-block, level continuity preserved. No-op unless actually paused —
    /// callers pick StartPlaybackAtCursor() for the not-paused case.
    void ResumePlaybackFromPause();
    /// endregion </Playback state, position and seek>

    /// region <Port events>
public:
    /// @param port  Full 16-bit port address of the IN (any even port reaches
    ///              the ULA). Used to exclude the Sinclair-joystick rows from
    ///              the sustained-polling resume counter.
    uint8_t handlePortIn(uint16_t port);
    void handlePortOut(uint8_t value);
    /// endregion </Port events>

    /// region <Emulation events>
public:
    void handleFrameStart();
    void handleStep();
    void handleFrameEnd();
    /// endregion </Emulation events>

    /// region <Helper methods>
protected:
    bool getTapeStreamBit(uint64_t clockCount);

    /// Whether the port's high byte selects one of the two Sinclair-joystick
    /// rows ($EF = stick 1 / $F7 = stick 2). Games poll those in tight loops,
    /// so they must not accumulate toward the sustained-polling resume
    /// threshold. Keyboard half-row scans never reach the threshold by count
    /// (~8 reads/frame), and loaders deliberately polling other rows (e.g.
    /// $7FFE — the space row, combined EAR + abort-key read) stay counted.
    static bool IsJoystickPollPort(uint16_t port);

    bool generateBitstreamForStandardBlock(TapeBlock& tapeBlock);

    size_t generateBitstream(TapeBlock& tapeBlock,
                             uint32_t pilotHalfPeriod_tStates,
                             uint32_t synchro1_tStates,
                             uint32_t synchro2_tStates,
                             uint32_t zeroEncodingHalfPeriod_tState,
                             uint32_t oneEncodingHalfPeriod_tStates,
                             size_t pilotLength_pulses,
                             size_t pause_ms,
                             uint8_t bitsInLastByte = 8);

    // FIXME: just experimentation method
    bool getPilotSample(size_t clockCount);

    /// endregion </Helper methods>

    /// region <TTDSerializable interface (P1.5 — parent TDD §6.4, §4 row 3)>
public:
    ///
    /// Per parent TDD §4 row 3: checkpoint the playback POSITION, never the
    /// content. Tape content (_tapeBlocks) is invariant within a session —
    /// tape-control commands (load/stop/rewind) invalidate the session (§4.2).
    ///
    /// Serialized fields (42 bytes, cursor-packed):
    ///   _tapeStarted, _playbackFrozen, _tapePosition, _currentTapeBlockIndex,
    ///   _currentPulseIdxInBlock, _currentOffsetWithinPulse, _currentClockCount.
    ///
    /// Excluded:
    ///   - _tapeBlocks (content; invariant within session, not checkpointed)
    ///   - _currentTapeBlock (derived pointer; recomputed from index on load)
    ///   - _lpfFilter / _dcFilter (audio filters; host-side, rebuilt by
    ///     handleFrameStart)
    ///   - _muteEAR (host-side UI setting)
    ///   - _context (pointer)
    size_t TTDStateSize() const override;
    void   TTDSaveState(uint8_t* dst) const override;
    void   TTDLoadState(const uint8_t* src) override;
    /// endregion </TTDSerializable interface>
};

//
// Code Under Test (CUT) wrapper to allow access to protected and private properties and methods for unit testing / benchmark purposes
//
#ifdef _CODE_UNDER_TEST

class TapeCUT : public Tape
{
public:
    TapeCUT(EmulatorContext* context) : Tape(context) {};

    using Tape::handlePortIn;
    using Tape::generateBitstream;
    using Tape::generateBitstreamForStandardBlock;

    using Tape::getPilotSample;

    using Tape::stopPlayback;

    // Cursor fields — exposed so integration tests can set the playback
    // position to known values without depending on the ROM LOAD routine
    // (which would make the test hostage to ROM timing). Used by
    // ttd_subsystem_restore_test.cpp to verify SeekTo round-trips the
    // serialized tape cursor blob.
    using Tape::_tapeStarted;
    using Tape::_tapePosition;
    using Tape::_tapeBlocks;
    using Tape::_imageLoadedPath;
    using Tape::_currentTapeBlockIndex;
    using Tape::_currentPulseIdxInBlock;
    using Tape::_currentOffsetWithinPulse;
    using Tape::_currentClockCount;
    using Tape::_currentTapeBlock;
};

#endif // _CODE_UNDER_TEST