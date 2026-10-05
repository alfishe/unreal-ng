#pragma once

// ZX-MultiSound (UzixLS) card: the CPLD logic, two YM2203, the SAA1099, the General Sound, the shared DACs, the MIDI line
// and the General MIDI synthesizer on one board (docs/inprogress/2026-10-03-zx-multisound/architecture.md §1, MS-3).
//
// Self-contained: no slot framework, no SoundManager, no machine. The card is driven through explicit calls with times
// on its own axis, so a thin slot adapter (ICard, docs/inprogress/2026-10-03-zx-bus-slots/architecture.md §3.2) can
// wrap it later; tdd-integration.md "MS-4 slot adapter" describes that adapter.
//
// Worked example (requirements.md §1, a TSFM + SAA tune), host times t0 < t1 < ...:
//   Out(#FFFD, #F7, t0)   control byte: chip 2 (U10) selected, register read mode, FM muted (bit 2), SAA clock on
//                         (bit 3 = 0); the byte also reaches U10 as an address write
//   Out(#FFFD, #07, t1); Out(#BFFD, #38, t2)    U10 register 7 (SSG mixer)
//   Out(#1FF, #1C, t3);  Out(#FF, #01, t4)      SAA register #1C = 1 (sound enable)
//   FrameEnd(t5)                                the five rows MS FM, MS SSG, MS SAA, MS DAC, MS MIDI
//
// Time. Every time is a host tick on the card axis: hostTickRate ticks per second (the emulator's audio T-states,
// AudioTstate: 3.5 MHz on a Pentagon, 3.5469 MHz on a 128K), absolute and monotonic (a slot adapter adds its frame
// base to the machine's frame-relative t). Each module converts it to its own clock:
//   YM2203 pair  3.5 MHz master clock (the card's own DDS), ratio masterClockHz : hostTickRate (Ym2203Pair)
//   SAA1099      8 MHz, ratio inside Saa1099
//   GS Z80       16 MHz / INT 12 MHz / 321, 48 MHz card units (GSProfile::MultiSound); the GS reads this axis
//                through IGSHostClock, frame-relative to the last FrameStart (as it reads the machine's Z80 otherwise)
//   DACs, SAM2695, MIDI line   the host axis itself
//
// Ownership decisions (the MS-2 open items):
//   - GS mailbox: SoundChip_GeneralSound owns it (#B3 / #BB latches and both flags). MultiSoundLogic decides whether a
//     host cycle is a GS cycle (decode, DIP, IORQGE); the value and the flag side effects come from the GS. The
//     logic's own GS latches (data, command, page, output, flags, its DAC copy) are not consulted by the card: the GS
//     CPU's port accesses go to the GS, so only the GS sees both sides. One owner, one TTD blob holding it.
//   - SounDrive and the GS volume register: a SounDrive write goes to MultiSoundDacs (sample + volume 63, ordered by
//     strobe end) and to the GS's shared volume register (sharedVolumeWrite), after the GS has run to the write's time,
//     so GS port #0B reads volume 3 bit 5 as on the board.
//   - Event times: a host write's DAC event ends at the t of its Out call (the emulator's port access time); a GS
//     event ends at its instruction's start converted to host ticks (truncated; the GS reports per instruction). The
//     card runs the GS to t before it submits a host event at t, so both timelines have reached t when the DACs run.

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "emulator/slots/cards/multisound/multisoundanalog.h"
#include "emulator/slots/cards/multisound/multisounddacs.h"
#include "emulator/slots/cards/multisound/multisoundlogic.h"
#include "emulator/slots/cards/multisound/multisoundmixer.h"
#include "emulator/sound/chips/gs/gsprofile.h"
#include "emulator/sound/chips/saa1099/saa1099.h"
#include "emulator/sound/midi/midiline.h"

namespace ttd
{
struct TTDTimeField;
}

class EmulatorContext;
class SoundChip_GeneralSound;
class Ym2203Pair;

namespace sam2695
{
class ISoundBank;
class Synth;
struct SynthReport;
}

/// The five stereo rows (architecture.md §5); the board weights are applied before them
enum class MultiSoundRow : uint8_t
{
    Fm = 0,     ///< MS FM: both YM2203 FM outputs
    Ssg,        ///< MS SSG: both YM2203 SSG parts, A left, B centre, C right
    Saa,        ///< MS SAA
    Dac,        ///< MS DAC: GS + SounDrive, channels 0-1 left, 2-3 right
    Midi,       ///< MS MIDI: the SAM2695
    Count
};

struct MultiSoundCardConfig
{
    /// DIP functions and firmware options. gsRam is fixed at construction (it sizes the GS RAM)
    MultiSoundOptions options;
    /// Card axis: ticks per second of every time passed in
    uint32_t hostTickRate = 3500000;
    /// Row rate (stereo frames per second)
    uint32_t outputRate = 44100;
    MultiSoundRenderMode renderMode = MultiSoundRenderMode::HiFi;
    /// GS firmware (resolved like [ROM] entries: working dir, executable dir, resources)
    std::string gsRomPath = GSProfile::kMultiSoundRomPath;
    /// [MIDI] Bank= (resolved like the ROM); a missing or unreadable bank leaves the synthesizer silent ("no bank")
    std::string midiBankPath = "midi/generaluser-gs.sf2";
    /// A bank object instead of the file (tests, one bank shared by several cards); overrides midiBankPath
    std::shared_ptr<const sam2695::ISoundBank> midiBank;
};

/// Everything automation surfaces show about the card (Describe is their single source)
struct MultiSoundCardReport
{
    MultiSoundOptions options;
    /// The CPLD latches; the GS fields (data, command, page, output, flags) are the GS's, the mailbox owner's
    MultiSoundLatches latches;

    struct Ym
    {
        uint8_t address = 0;            ///< address latch
        uint8_t status = 0;             ///< busy | timer B | timer A
        uint8_t ssgRegisters[16] = {};
        uint8_t fmKeyOn[3] = {};        ///< key-on slot mask per FM channel as last written to #28
    };
    Ym ym[2];                           ///< [0] = U4 (chip select 0, the MIDI pin), [1] = U10
    uint64_t ymRatioPhase = 0;

    Saa1099Report saa;

    struct Gs
    {
        bool romLoaded = false;
        size_t ramKB = 0;
        uint8_t page = 0;
        uint8_t status = 0;             ///< as the host reads #BB
        uint8_t dataFromHost = 0;
        uint8_t dataToHost = 0;
        uint8_t commandFromHost = 0;
        bool firmwareReady = false;     ///< the firmware has written its four volumes (POST done)
        uint64_t cpuSteps = 0;
        uint64_t dacFetches = 0;
    } gs;

    std::array<MultiSoundDacState, 4> dac{};
    size_t dacPendingEvents = 0;
    uint64_t dacLateEvents = 0;

    MidiLineReport midiLine;
    struct Midi
    {
        bool bankLoaded = false;
        std::string bankStatus;         ///< "loaded" or "no bank"
        std::string bankName;
        std::string bankSource;         ///< the resolved file, or "(supplied)"
        std::string bankError;          ///< why there is no bank
        uint64_t bytesReceived = 0;
        uint64_t framingErrors = 0;
        uint32_t activeVoices = 0;
    } midi;

    uint64_t time = 0;                  ///< the latest time the card was driven to
};

class MultiSoundCard : private IGSHostClock, private IGSDacSink
{
public:
    static constexpr const char* kCardId = "multisound";
    static constexpr const char* kDisplayName = "ZX-MultiSound (UzixLS)";
    /// YM2203 master clock: 32 MHz x 7 / 64 from the card's DDS (average; the jitter is not modeled)
    static constexpr uint32_t kYmMasterClockHz = 3500000;
    /// The YM2203 whose IOA2 drives the SAM2695 MIDI IN: U4, chip select 0 (hardware-reference.md §4.1, §4.4)
    static constexpr int kMidiChip = 0;
    /// Host DAC strobe ends at the Out call's time
    static constexpr uint64_t kHostStrobeEndOffset = 0;
    /// FM mute changes kept per frame for the FM render (more collapse into the last one)
    static constexpr size_t kMaxFmMuteChanges = 256;

    MultiSoundCard(EmulatorContext* context, const MultiSoundCardConfig& config = MultiSoundCardConfig{});
    ~MultiSoundCard() override;
    MultiSoundCard(const MultiSoundCard&) = delete;
    MultiSoundCard& operator=(const MultiSoundCard&) = delete;

    const MultiSoundCardConfig& Config() const { return _config; }

    /// DIP functions and the control mask, live (the CPLD reads its DIP inputs continuously). gsRam stays the
    /// construction value
    void SetOptions(const MultiSoundOptions& options);
    const MultiSoundOptions& Options() const { return _logic.Options(); }

    /// Row rate change at a frame boundary (every module's output side)
    void SetOutputRate(uint32_t rate);
    /// HiFi / Authentic (SAA PDM stream + board filters) at a frame boundary
    void SetRenderMode(MultiSoundRenderMode mode);

    /// region <Bus>
    /// IORQGE for an I/O cycle at this port (direction-independent, the RTL decodes the address only)
    bool Iorqge(uint16_t port) const { return _logic.Iorqge(port); }

    /// Every M1 cycle (opcode, prefix, interrupt acknowledge): the ROM-fetch lock of the SAA and SounDrive ports
    void M1(uint16_t address) { _logic.OnM1(address); }

    /// An I/O write cycle at time t
    void Out(uint16_t port, uint8_t value, uint64_t t);

    /// An I/O read cycle at time t; drives = false when the card stays off the data bus (it may still assert
    /// IORQGE: the #BFFD family)
    uint8_t In(uint16_t port, uint64_t t, bool& drives);

    /// Read without side effects (debugger); #FF when the card does not drive
    uint8_t Peek(uint16_t port, bool& drives) const;
    uint8_t Peek(uint16_t port) const
    {
        bool drives = false;
        return Peek(port, drives);
    }

    /// Bus /RESET at time t: the CPLD reset branches, both YM2203, the SAA1099, the GS (CPU, mailbox, volumes), the
    /// DACs and the SAM2695 with its MIDI line (all share the board reset)
    void BusReset(uint64_t t);
    /// endregion </Bus>

    /// region <Frames>
    /// A host frame starts at t and lasts frameTicks (the GS plans its frame with it; the other modules need no
    /// frame)
    void FrameStart(uint64_t t, uint64_t frameTicks);

    /// Runs every module to t and renders the rows from the previous FrameEnd up to t. frames = 0: the card counts
    /// the output samples itself from the time (a fractional remainder carries over). Returns the frames rendered
    /// into every row. t should be FrameStart's t + frameTicks: the GS runs to that nominal end
    size_t FrameEnd(uint64_t t, size_t frames = 0);

    /// A row of the last FrameEnd: interleaved stereo int16, RowFrames() frames
    const int16_t* Row(MultiSoundRow row) const { return _rows[static_cast<size_t>(row)].data(); }
    size_t RowFrames() const { return _rowFrames; }
    /// endregion </Frames>

    /// region <Report>
    void Describe(MultiSoundCardReport& out) const;
    /// The synthesizer's own report (channels, programs, voices, UART counters)
    void DescribeSynth(sam2695::SynthReport& out) const;
    /// endregion </Report>

    /// region <Modules (tests, automation, the slot adapter's TTD set in MS-5)>
    const MultiSoundLogic& Logic() const { return _logic; }
    Ym2203Pair& Ym() { return *_ym; }
    const Ym2203Pair& Ym() const { return *_ym; }
    Saa1099& Saa() { return _saa; }
    const Saa1099& Saa() const { return _saa; }
    SoundChip_GeneralSound& Gs() { return *_gs; }
    const SoundChip_GeneralSound& Gs() const { return *_gs; }
    MultiSoundDacs& Dacs() { return _dacs; }
    const MultiSoundDacs& Dacs() const { return _dacs; }
    MidiLine& MidiIn() { return _midiLine; }
    const MidiLine& MidiIn() const { return _midiLine; }
    sam2695::Synth& Synth() { return *_synth; }
    const sam2695::Synth& Synth() const { return *_synth; }
    const MultiSoundMixer& Mixer() const { return _mixer; }
    bool MidiBankLoaded() const { return _bankLoaded; }
    /// endregion </Modules>

    /// region <Time travel (MS-5, tdd-integration.md §4)>
    /// The card's own state (the slot adapter's PeripheralId::MultiSound blob carries it after its time base). Layout
    /// (kTtdVersion 1, little-endian, fixed size):
    ///   0     u1  version
    ///   1     u8  now, u8 frame base, u8 frame ticks, u8 rendered-to, u8 output-sample accumulator (card axis)
    ///   41    u1  FM muted at the render cursor; u2 FM mute changes pending, then kMaxFmMuteChanges x (u8 t, u1 muted)
    ///   2348  CPLD latches (11 x u1: chip select, read mode, FM mute, SAA clock, ROM lock, GS data / command / page /
    ///         output, data flag, command flag), 4 x DAC registers (u1 sample, u1 volume)
    ///   2367  u8  the YM2203 pair's synced time, then the pair's blob (Ym2203Pair::TTDSaveState)
    ///   then  the MIDI line (MidiLine::TTDSaveState), the shared DACs (MultiSoundDacs::TTDSaveState)
    /// The SAA1099, the SAM2695 and the board's GS have blobs of their own. Render layers (the mixer's filters, the
    /// modules' output buffers, the last MIDI level) are not state: a load resets them, so the audio after a restore
    /// does not depend on what played before it
    static constexpr uint8_t kTtdVersion = 1;
    size_t TtdStateSize() const;
    void TtdSave(uint8_t* dst) const;
    /// False when the blob has another layout (nothing is changed)
    bool TtdLoad(const uint8_t* src);
    /// The counters that advance with time, for the engine's descriptor (`offset`: where the card's blob starts)
    void TtdTimeFields(std::vector<ttd::TTDTimeField>& out, uint16_t offset) const;
    /// The YM2203 pair caught up to the card axis position `now` (or adopts it at its next sync)
    bool TtdSynced(uint64_t now, int64_t& offset) const;
    /// endregion </Time travel>

private:
    // IGSHostClock: the card axis, frame-relative to the last FrameStart
    uint64_t GsHostTacts() const override { return _now > _frameBase ? _now - _frameBase : 0; }
    uint32_t GsHostTickRate() const override { return _config.hostTickRate; }
    uint64_t GsHostFrameTacts() const override { return _frameTicks; }

    // IGSDacSink: GS times are frame-relative (the GS's host axis), the DACs' are absolute
    void GsSample(uint64_t time, int channel, uint8_t value) override { _dacs.GsSample(_frameBase + time, channel, value); }
    void GsVolume(uint64_t time, int channel, uint8_t volume) override { _dacs.GsVolume(_frameBase + time, channel, volume); }

    void ApplyControl(uint64_t t);
    void LoadMidiBank();
    void RenderYm(size_t frames, uint64_t t);
    void RenderYmBlock(size_t offset, size_t frames, bool fmEnabled);

    struct FmMuteChange
    {
        uint64_t t;
        bool muted;
    };

    MultiSoundCardConfig _config;
    EmulatorContext* _context;

    MultiSoundLogic _logic;
    std::unique_ptr<Ym2203Pair> _ym;
    Saa1099 _saa;
    MultiSoundDacs _dacs;
    std::unique_ptr<SoundChip_GeneralSound> _gs;
    std::unique_ptr<sam2695::Synth> _synth;
    MidiLine _midiLine;
    MultiSoundMixer _mixer;

    // Bank
    bool _bankLoaded = false;
    std::string _bankName;
    std::string _bankSource;
    std::string _bankError;

    // Time
    uint64_t _now = 0;              // latest time the card was driven to
    uint64_t _frameBase = 0;        // last FrameStart
    uint64_t _frameTicks = 0;       // its length
    uint64_t _renderedTo = 0;       // last FrameEnd: the rows cover (previous, this]
    uint64_t _frameAccumulator = 0; // output samples x hostTickRate not yet rendered (frames = 0)

    // FM mute (the board's FM*_ENA lines): state at the render cursor and the changes since
    bool _fmMutedRendered = true;
    std::vector<FmMuteChange> _fmMuteChanges;

    // Render buffers (allocated once, MAX_SAMPLES_PER_FRAME frames)
    std::vector<float> _fm[2];
    std::vector<float> _ssg[2][3];
    std::vector<int16_t> _saaOut;
    std::vector<int16_t> _dacOut;
    std::vector<float> _midiOut;
    float _midiLast[2] = {};
    std::array<std::vector<int16_t>, static_cast<size_t>(MultiSoundRow::Count)> _rows;
    size_t _rowFrames = 0;
};
