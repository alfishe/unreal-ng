#pragma once

#include "stdafx.h"

#include "debugger/ttd/ttdserializable.h"  // TTDSerializable (P1.5 peripheral serializer)
#include "emulator/io/tape/tapetypes.h"      // tape vocabulary types (design §5.1a leaf header)
#include "emulator/io/tape/tapecatalog.h"
#include "emulator/io/tape/tapereadclassifier.h"    // TapeFastLoadPlan (§5.8) — leaf, no cycle
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

// The tape follows the loader (loader-follow design §5): it moves only while
// a program listens to it. Every read of the port is classified by the code
// after the IN (TapeReadClassifier); keyboard reads never count.
//
// Start: this many listening reads within one frame. A loader's edge loop
// makes about 1000 per frame, so this takes ~0.15 ms of a 2 s pilot, while a
// one-off EAR test (issue 2/3 detection) never starts the tape.
constexpr uint16_t TAPE_START_LISTEN_READS = 8;
// Reads of code the tracker cannot follow count as listening after this many
// in a row from the same IN with exactly one counter register moving (Fuse).
constexpr uint16_t TAPE_PATTERN_RUN = 10;
constexpr uint32_t TAPE_PATTERN_GAP_STOPPED = 500;   // T-states between pattern reads, tape stopped
constexpr uint32_t TAPE_PATTERN_GAP_PLAYING = 1000;  // looser while playing: a wrong stop costs a load
// Pause: whole frames without a single listening read. In the silence after a
// block the tape parks at the next pilot, which costs nothing; inside a block
// a false freeze shifts the data, so it waits longer. A frame always contains
// listening while a load runs, even with an interrupt playing music or
// scanning keys, so one frame is the floor.
constexpr uint32_t TAPE_GAP_HOLD_FRAMES = 2;
constexpr uint32_t TAPE_BLOCK_HOLD_FRAMES = 50;
// Inside a PILOT the wait is longer: a custom loader that has seen the first pilot pulses often sits out a fixed delay
// before it checks that the pilot goes on (DIZZY X KID__DR: ld hl,#0415 / djnz, 3.5 million T, 50.07 frames), and a
// pilot freeze rewinds the pilot, so the loader would find a fresh pilot after every delay and never lock. A data
// pilot lasts about 2 s (3220 pulses of 2168 T): 100 frames is still short enough that a loader that is really
// gone gets its pilot from the start when it comes back.
constexpr uint32_t TAPE_PILOT_HOLD_FRAMES = 100;

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

/// Stable wire name of a playback state for the machine surfaces (CLI,
/// WebAPI, Lua, Python — design §6.3). Lowercase, one word per state.
inline const char* getTapePlaybackStateName(TapePlaybackState value)
{
    const char* result;
    switch (value)
    {
        case TapePlaybackState::Idle:
            result = "idle";
            break;
        case TapePlaybackState::Playing:
            result = "playing";
            break;
        case TapePlaybackState::Paused:
            result = "paused";
            break;
        case TapePlaybackState::Ended:
            result = "ended";
            break;
        default:
            result = "unknown";
            break;
    }
    return result;
}

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

    // The media manager's tape (slot "tape"): the medium owns the parsed
    // image; the deck plays a copy of its blocks. Each attach gets a new
    // generation, so re-inserting the same file starts afresh
    const TapeImage* _attachedImage = nullptr;
    std::string _attachedPath;     // the medium's source, mirrored into coreState.tapeFilePath
    std::string _attachedFormatId;
    uint64_t _attachGeneration = 0;

    TapeBlock* _currentTapeBlock;       // Shortcut to current block object
    // Consumption cursor: index of the NEXT block to deliver to the CPU, by signal
    // playback or by the fast-loading trap (single source of truth — design §9.4).
    // UINT64_MAX is the "nothing consumed yet / not started" sentinel. During signal
    // playback of block k the field holds k (the in-flight block); a stop or a
    // freeze keeps it there, so a partly played block is never lost.
    size_t _currentTapeBlockIndex;
    size_t _currentPulseIdxInBlock;     // Index in TapeBlock::edgePulseTimings vector
    size_t _currentOffsetWithinPulse;   // How many pulses already processed within single TapeBlock::edgePulseTimings vector element
    uint64_t _currentClockCount;        // Store clock count for next iteration
    bool _baseClockTimeBase = false;    // ClockCount() in base T-states (SetBaseClockTimeBase), else CPU clocks
    bool _lastTapeBit = false;          // Last tape bit state for band-limited step edge detection
    bool _tapeBitState = false;         // Digital signal output level of current tape pulse

    // Loader-follow state (design §5). Whole frames without a listening read,
    // and listening reads since the current frame started (reset every
    // handleFrameStart()).
    uint32_t _framesNotListened = 0;
    uint32_t _listenReadsThisFrame = 0;

    // Loader pattern for reads the classifier returns Other for (§4.2): run
    // length, and the PC, time and B,C,D,E,H,L of the previous such read.
    uint16_t _patternRun = 0;
    uint16_t _patternLastPc = 0;
    uint64_t _patternLastTick = 0;
    uint8_t _patternRegs[6] = {};

    // Classification cache: a loader reads from the same IN thousands of times
    // per frame, so the tracker runs once per PC per frame. Not saved for TTD;
    // rebuilt on the next read.
    bool _classifiedValid = false;
    uint16_t _classifiedPc = 0;
    uint64_t _classifiedFrame = 0;
    TapeReadKind _classifiedKind = TapeReadKind::Other;

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
    /// (decompression, bank switching). stopPlayback() is for the end of the
    /// tape only.
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
    /// Install the tape into _tapeBlocks, lazily: the attached medium's image
    /// (once per attach), else a parse of coreState.tapeFilePath. Idempotent:
    /// never re-installs over live blocks (that would reset the consumption
    /// cursor and dangle _currentTapeBlock). Returns true when blocks are
    /// available.
    bool EnsureImageLoaded();

    /// The tape slot puts a medium in: the deck stops, drops what it played and
    /// plays `image` from its first block (the medium keeps owning it).
    /// `sourcePath` is mirrored into coreState.tapeFilePath for display
    void AttachImage(const TapeImage* image, const std::string& sourcePath, const std::string& formatId);
    /// The tape slot takes the medium out: the deck stops, the image is gone
    void DetachImage();
    bool HasAttachedImage() const { return _attachedImage != nullptr; }

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

    /// region <Time base>
public:
    /// The tape's clock: machine time in base (3.5 MHz) T-states, or in CPU clocks (the default).
    /// A tape plays in real time; a machine whose CPU runs N times faster through a hardware clock switch
    /// (hw_turbo_ratio: the Sprinter at 21 MHz) counts N CPU clocks per base T-state inside the frame, and
    /// the plain sum t_states + t makes the tape run N times faster with the CPU (and step back at the frame
    /// boundary, where t_states grows by one base frame only). With the base-clock time base the in-frame
    /// part is scaled back, as the WD1793 does (FDC::SetBaseClockTimeBase): a 2 168-T pilot pulse lasts
    /// 2 168 base T = 13 008 CPU clocks at 21 MHz, the ROM loader's edge loop times it six times too long
    /// and rejects it - what the real board does (tdd-zx-mode.md §3.4). Opt-in per model: the Sprinter
    /// turns it on; other turbo machines keep the CPU-clock time base (a separate change, it moves their
    /// TTD fixtures - tdd-zx-mode Q2)
    void SetBaseClockTimeBase(bool on) { _baseClockTimeBase = on; }
    bool IsBaseClockTimeBase() const { return _baseClockTimeBase; }

    /// The tape clock now (t_states + the in-frame position, scaled per the time base)
    uint64_t ClockCount() const;
    /// endregion </Time base>

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

    /// What the code that just read the port does with the value (design
    /// §4.1, §4.3). Reads from ROM count only inside the ROM's own LD-BYTES.
    TapeReadKind ClassifyPortRead();

    /// Whether this read is a loader listening: an EAR read, or an Other read
    /// that continues the loader pattern of design §4.2.
    bool IsListeningRead(TapeReadKind kind, uint64_t clockCount);

    /// Stop in the silence after a block, at the start of the next block's
    /// pilot: the next listening read starts that block from its first pulse.
    void ParkAtNextBlock();

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

    /// Identity used by TTDPeripheralRegistry. Without it the base class
    /// returns PeripheralId::Count and the device cannot be indexed in a
    /// checkpoint's blob map.
    ttd::PeripheralId TTDPeripheralId() const override { return ttd::PeripheralId::Tape; }
    std::string TTDDeviceName() const override { return "Tape"; }
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

    /// The natural end of the tape, as its last pulse brings it about: the head past the last block,
    /// playback stopped (state Ended). A stop mid-block no longer gets there: it keeps the block
    void EndOfTape()
    {
        _currentTapeBlockIndex = _tapeBlocks.size();
        _currentTapeBlock = nullptr;
        _currentOffsetWithinPulse = 0;
        _currentPulseIdxInBlock = 0;
        stopPlayback();
    }

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