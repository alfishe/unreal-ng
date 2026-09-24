#pragma once

#include <stdafx.h>

#include <memory>
#include <vector>

#include "common/sound/filters/filter_decimator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/sound/audio.h"
#include "emulator/sound/chips/iturbosounddevice.h"
#include "emulator/sound/chips/tsfm/fm_word_queue.h"
#include "emulator/sound/chips/tsfm/ym2203_engine.h"
#include "emulator/sound/native_audio_tap.h"

/// @file soundchip_turbosoundfm.h
/// @brief TSFM - the TurboSound FM board (2 x YM2203) chip core (design §5).
///
/// The CPLD's three latches (TsfmBoard) select one of two YM2203 halves
/// (TsfmChip = SoundChip_AY8910 SSG + ymfm FM engine) for the AY-bus ports.
/// The core is advanced in T-states (syncTo/advanceChip) so it is in step
/// with the CPU at every instruction boundary - the invariant TTD
/// checkpointing relies on (§5.2). P4 delivers the core with a silent
/// output stage: the FM word queue and the SSG render buffers exist but
/// nothing is rendered into them; the output stage (§6) drains the queues.

/// The logic chip's three latches, clocked by a control word
/// (OUT #FFFD with value 0xF8..0xFF, design §5.1 / hardware-reference §3.2)
struct TsfmBoard
{
    uint8_t chip = 0;         // 0 = first chip (D1); bit 0 of the control word
    bool statusRead = false;  // true: IN #FFFD reads the status byte; bit 1 = 0
    bool fmEnabled = false;   // FM audio path enabled; bit 2 = 0 (muted)
};

/// Per-chip output-stage state (§6): the sample-and-hold value of the newest
/// consumed FM word, its mono 437.5 kHz decimator (slave of chip-0 SSG left,
/// §6.3), the LQ boxcar accumulator and the raw pre-mute DAC tap (§6.4).
/// Not TTD state — the §8.2 payload (P5) carries core state only.
struct TsfmOutputState
{
    double hold = 0.0;                    // newest FM word / 32768, held until the next word
    FilterDecimator decimator;            // 437.5 kHz -> core rate, HQ path
    double lqSum = 0.0;                   // LQ boxcar: sum of gated half-tick values
    uint32_t lqCount = 0;                 // LQ boxcar: half-ticks summed for this output sample
    std::shared_ptr<NativeAudioTap> nativeTap = std::make_shared<NativeAudioTap>();
};

/// One YM2203: the SSG half is the same SoundChip_AY8910 the legacy device
/// uses, the FM half is a vendored ymfm engine. Not copyable or movable -
/// ymfm holds references to the interface and the override adapter.
class TsfmChip
{
public:
    explicit TsfmChip(EmulatorContext* context)
        : ssg(context), fm(intf), ssgAdapter(ssg)
    {
        // §5.4: construction runs the machine-reset sequence. ymfm's
        // constructor does NOT call reset() - its register array is
        // uninitialised until reset() runs.
        fm.ssg_override(ssgAdapter);
        resetChip();

        // TTD save-path scratch (§8.2): reserved once so TTDSaveState never
        // allocates on its steady-state path (measured ymfm payload: 494 B).
        ttdScratch.reserve(1024);
    }

    TsfmChip(const TsfmChip&) = delete;
    TsfmChip& operator=(const TsfmChip&) = delete;

    /// Per-chip half of the reset sequence (§5.4)
    void resetChip()
    {
        ssg.reset();
        // ymfm resets FM registers, operators and status; the adapter's
        // ssg_reset() is a no-op, so the AY is not reset twice
        fm.reset();
        // ymfm's reset leaves the prescaler as it was; a real YM2203 reset
        // returns to /6
        fm.write_address(0x2D);
        address = 0;
        fmClockPhase = 0;
        fmKeyOn[0] = fmKeyOn[1] = fmKeyOn[2] = 0;
        intf.reset();
        ssg.setChipModel(AYChipModel::YM2149);
    }

    SoundChip_AY8910 ssg;      // SSG half, model YM2149
    Ym2203Interface intf;      // timers + busy, in T-states
    Ym2203Engine fm;           // ymfm::ym2203 subclass, patched
    SsgOverrideAdapter ssgAdapter;

    uint8_t address = 0;       // YM2203 address latch (8-bit)
    int32_t fmClockPhase = 0;  // T-states since the last FM sample, 0 .. 12*p-1

    // Key-on mask per FM channel as last written to register 0x28 (bits
    // 4-7 = slots S1,S2,S3,S4). Mirror for the state report
    // (DeviceState::FmChip); ymfm keeps the live key state privately.
    uint8_t fmKeyOn[3] = {0, 0, 0};

    // Output-side hand-off (not TTD state, §6)
    FmWordQueue words;

    // Output stage (§6): hold register, decimator, LQ boxcar, raw DAC tap
    TsfmOutputState out;

    // TTD save-path scratch (§8.2): ymfm_saved_state serializes into a
    // vector via push_back; reserved in the constructor so TTDSaveState
    // never allocates on its steady-state path (measured payload: 494 B).
    std::vector<uint8_t> ttdScratch;
};

class SoundChip_TurboSoundFM : public ITurboSoundDevice
{
    /// region <Fields>
protected:
    TsfmBoard _board;
    std::unique_ptr<TsfmChip> _chips[2];

    uint64_t _syncedT = 0;        // core advanced to this frame-relative T-state (§5.2); not TTD state
    bool _adoptCpuClock = true;   // set by reset/restore: next sync adopts the CPU's T-state without advancing

    AudioFrameDescriptor _ayAudioDescriptor;
    int16_t* const _ayBuffer = (int16_t*)_ayAudioDescriptor.memoryBuffer;

    // Per-chip buffers for registry-driven mixing (AY 1 / AY 2 capture).
    // The SSG render loop (§6.2) fills them alongside _ayBuffer.
    AudioFrameDescriptor _chip0AudioDescriptor;
    AudioFrameDescriptor _chip1AudioDescriptor;
    int16_t* const _chip0Buffer = (int16_t*)_chip0AudioDescriptor.memoryBuffer;
    int16_t* const _chip1Buffer = (int16_t*)_chip1AudioDescriptor.memoryBuffer;

    // Per-chip FM-only buffers for registry-driven mixing (FM 1 / FM 2
    // capture, §6.4): centre-panned, already scaled by _fmGain
    AudioFrameDescriptor _fm0AudioDescriptor;
    AudioFrameDescriptor _fm1AudioDescriptor;
    int16_t* const _fm0Buffer = (int16_t*)_fm0AudioDescriptor.memoryBuffer;
    int16_t* const _fm1Buffer = (int16_t*)_fm1AudioDescriptor.memoryBuffer;

    /// FM input rate (§6.3): the YM2203 sample clock at prescaler /6 is
    /// PSG_CLOCK_RATE/4 = 437.5 kHz = exactly 2x the SSG generator rate
    static constexpr double kFmInputRate = static_cast<double>(PSG_CLOCK_RATE) / 4.0;
    /// FM loudness baseline (§7.1): full-scale DAC word -> 0.30 in the mix
    static constexpr double kFmBaseGain = 0.30;

    size_t _coreRate = AUDIO_SAMPLING_RATE;
    bool _hqEnabled = true;
    bool _synthesisSuppressed = false;
    bool _coreSynthesisSkipped = false;  // FM operator clocking frozen (sound off, no TTD); see ITurboSoundDevice
    bool _prescalerWarned = false;  // one §9.4 warning per device instance

    // Render loop state — the legacy SoundChip_TurboSound loop copied
    // verbatim (§11 bit-identity): the mixer-exact sample accumulator (see
    // SoundChip_TurboSound::_samplePhase), per-frame buffer cursor, T-state
    // axis base, LQ boxcar phase, plus the §6.2 FM cursor
    uint64_t _samplePhase = 0;
    size_t _ayBufferIndex = 0;
    uint32_t _lastTStates = 0;
    double _decimationPhase = 0.0;
    double _lqTicksPerSample = (double)(PSG_CLOCK_RATE / 8) / (double)AUDIO_SAMPLING_RATE;
    uint64_t _renderT = 0;  // SSG-tick cursor of the render loop (§6.2), frame-relative

    // FM gain = kFmBaseGain * 10^(trim/20) (§7.1); [SOUND] TSFM_FmTrimDb
    double _fmGain = kFmBaseGain;
    double _fmTrimDb = 0.0;

    // Native-rate recording tap (218.75 kHz SSG side). The FM taps
    // (out.nativeTap, §6.4) carry the raw pre-mute DAC stream per chip.
    std::shared_ptr<NativeAudioTap> _nativeTap = std::make_shared<NativeAudioTap>();

    // Activity tracking for HUD notification (matches legacy TurboSound logic)
    bool _frameHadActivity = false;    // Any register write this frame
    bool _chip0ActiveThisFrame = false; // Chip 0 had activity
    bool _chip1ActiveThisFrame = false; // Chip 1 had activity (TurboSound mode)
    bool _fmActiveThisFrame = false;    // FM registers (>=0x10) were written
    bool _wasActive = false;           // Activity state at last frame end
    bool _wasTurboSound = false;       // Was in TurboSound mode at last frame
    bool _wasFM = false;               // Was in FM mode at last frame
    /// endregion </Fields>

    /// region <Interfacing fields>
protected:
    bool _chipAttachedToPortDecoder = false;
    PortDecoder* _portDecoder = nullptr;
    /// endregion </Interfacing fields>

    /// region <Test access>
public:
    /// Board latches (tests, §12.1)
    const TsfmBoard& board() const
    {
        return _board;
    }

    /// Chip `i` (0 = first = the 0xFE chip)
    TsfmChip* chip(int index) const
    {
        return (index == 0 || index == 1) ? _chips[index].get() : nullptr;
    }

    /// T-state the core has been advanced to (frame-relative)
    uint64_t syncedT() const
    {
        return _syncedT;
    }

    /// True once the one-per-instance §9.4 prescaler warning has fired
    /// (tests assert a 0x2F -> 0x2D init frame stays silent)
    bool prescalerWarned() const
    {
        return _prescalerWarned;
    }

    /// Per-chip output-stage state (tests, §12.4: hold/decimator drivers)
    TsfmOutputState* outputState(int index)
    {
        TsfmChip* c = chip(index);
        return c ? &c->out : nullptr;
    }
    /// endregion </Test access>

    /// region <Constructors / destructor>
public:
    SoundChip_TurboSoundFM(EmulatorContext* context) : ITurboSoundDevice(context)
    {
        _chips[0] = std::make_unique<TsfmChip>(_context);
        _chips[1] = std::make_unique<TsfmChip>(_context);
        // Design all six decimators and attach the FM slaves (§6.3), and
        // apply the configured FM trim (§7.1)
        setCoreRate(_coreRate);
        setFmTrimDb(_context->config.sound.tsfmFmTrimDb);
    }

    virtual ~SoundChip_TurboSoundFM()
    {
        detachFromPorts();
    }
    /// endregion </Constructors / destructor>

    /// region <Core loop (§5.2)>
public:
    /// Advance the core to frame-relative T-state t. The first call after
    /// construction/reset adopts t without advancing (nothing to simulate).
    void syncTo(uint64_t t);

    /// Frame-relative T-state of the CPU right now (the port callbacks see
    /// the IORQ's own T-state; handleStep then advances to the instruction
    /// end - §5.3 note)
    uint64_t nowT() const;

    /// One FM half-tick of chip `chipIndex` ending at half-tick boundary h
    /// (§6.2): consume every word that landed by h into the hold register
    /// (the tap sees the raw pre-mute value), then feed the gated hold —
    /// board mute grounds the DAC data line — to the HQ decimator or the LQ
    /// boxcar accumulator. Public: §12.4 drives half-tick sequences directly
    void fmHalfTick(int chipIndex, uint64_t h);

protected:
    void advanceChip(TsfmChip& c, int32_t delta, uint64_t t0);

    /// LQ boxcar output of one chip's FM hold stream: average of the summed
    /// half-ticks (or the current hold when no half-tick landed on this
    /// output sample), resetting the accumulator
    double fmLqSample(int chipIndex);
    /// endregion </Core loop>

    /// region <Methods>
public:
    void reset() override;

    void updateState(bool bypassPrescaler = false)
    {
        _chips[0]->ssg.updateState(bypassPrescaler);
        _chips[1]->ssg.updateState(bypassPrescaler);
    }

    void setHQEnabled(bool enabled) override
    {
        _hqEnabled = enabled;
    }

    void setSynthesisSuppressed(bool suppressed) override
    {
        _synthesisSuppressed = suppressed;
    }

    void setCoreSynthesisSkipped(bool skipped) override
    {
        _coreSynthesisSkipped = skipped;
    }
    bool isCoreSynthesisSkipped() const { return _coreSynthesisSkipped; }

    /// Redesigns all six decimators for the rate (§6.3): four SSG ones at
    /// the generator rate (identical to the legacy device — bit-identity,
    /// §11), two FM ones at kFmInputRate in slave mode under chip-0 SSG left
    void setCoreRate(size_t rate) override
    {
        _coreRate = rate;
        _samplePhase = 0;  // in step with SoundManager::applyCoreRate
        _lqTicksPerSample = (double)(PSG_CLOCK_RATE / 8) / (double)rate;

        _chips[0]->ssg.decimatorLeft().configure((double)rate);
        _chips[0]->ssg.decimatorRight().configure((double)rate);
        _chips[1]->ssg.decimatorLeft().configure((double)rate);
        _chips[1]->ssg.decimatorRight().configure((double)rate);
        _chips[0]->out.decimator.configure((double)rate, FilterDecimator::Quality::Reference, false, kFmInputRate);
        _chips[1]->out.decimator.configure((double)rate, FilterDecimator::Quality::Reference, false, kFmInputRate);

        // FM decimators run in slave mode: chip-0 SSG left gates the output
        // cadence of every stream (§6.3)
        _chips[0]->out.decimator.attachMaster(&_chips[0]->ssg.decimatorLeft());
        _chips[1]->out.decimator.attachMaster(&_chips[0]->ssg.decimatorLeft());
    }

    size_t getCoreRate() const override
    {
        return _coreRate;
    }

    /// FM loudness trim in dB relative to the 0.30 hardware-derived default
    /// (§7.1); live-adjustable from the Audio Settings dialog
    void setFmTrimDb(double db) override
    {
        _fmTrimDb = db;
        _fmGain = kFmBaseGain * std::pow(10.0, db / 20.0);
    }

    double fmTrimDb() const override
    {
        return _fmTrimDb;
    }

    bool hasFm() const override
    {
        return true;
    }

    std::shared_ptr<NativeAudioTap> getNativeTap() const override
    {
        return _nativeTap;
    }

    std::shared_ptr<NativeAudioTap> getFmNativeTap(int chipIndex) const override
    {
        TsfmChip* c = chip(chipIndex);
        return c ? c->out.nativeTap : nullptr;
    }
    /// endregion </Methods>

    /// region <Properties>
public:
    uint16_t* getAudioBuffer()
    {
        return (uint16_t*)_ayBuffer;
    }

    size_t getRenderedSamplesThisFrame() const override
    {
        return _ayBufferIndex / AUDIO_CHANNELS;
    }

    int16_t* getChipBuffer(int index) override
    {
        if (index == 0)
            return _chip0Buffer;
        if (index == 1)
            return _chip1Buffer;
        return nullptr;
    }

    int16_t* getFmBuffer(int index) override
    {
        if (index == 0)
            return _fm0Buffer;
        if (index == 1)
            return _fm1Buffer;
        return nullptr;
    }

    SoundChip_AY8910* getChip(int index) const override
    {
        TsfmChip* c = chip(index);
        return c ? &c->ssg : nullptr;
    }

    int getChipCount() const override
    {
        return 2;
    }
    /// endregion </Properties>

    /// region <Emulation events>
public:
    void handleFrameStart() override;
    void handleStep() override;
    void handleFrameEnd() override;
    /// endregion </Emulation events>

    /// region <Automation tap (MCP M7j)>
public:
    void setLogSink(AYLogSink sink, void* context) override
    {
        _logSink = sink;
        _logSinkContext = context;
    }

protected:
    AYLogSink _logSink = nullptr;
    void* _logSinkContext = nullptr;
    /// endregion </Automation tap>

    /// region <PortDevice interface methods>
public:
    uint8_t portDeviceInMethod(uint16_t port) override;
    void portDeviceOutMethod(uint16_t port, uint8_t value) override;
    /// endregion </PortDevice interface methods>

    /// region <Ports interaction>
public:
    bool attachToPorts(PortDecoder* decoder) override;
    void detachFromPorts() override;
    /// endregion </Ports interaction>

    /// region <TTDSerializable interface>
    /// §8.2 layout (v3), 1190 bytes, PeripheralId::TSFM = 4:
    ///   u8 version (= 3)
    ///   u8 board (chip | statusRead<<1 | fmEnabled<<2)
    ///   f64 samplePhase, f64 decimationPhase        (v2: render-loop PLL/LQ phase)
    ///   f64 x4: chip{0,1}.ssg.decimator{Left,Right}().phase()  (v3: per-decimator
    ///     resampling phase - all four of these are tick-gating accumulators,
    ///     not "just" output buffering: they decide how many generator ticks
    ///     land before the next output sample, so they must be restored to
    ///     their exact historical value for forward-replay determinism, not
    ///     reset to zero or left at whatever the live device held pre-seek)
    ///   per chip x2:
    ///     u8  address
    ///     i32 fmClockPhase
    ///     i32 timerRemaining[2]   (-1 = stopped)
    ///     i32 busyRemaining
    ///     u16 ymfmSize            (= 494, asserted)
    ///     u8[ymfmSize] ymfm::ym2203::save_restore() payload
    ///     u8[57] SoundChip_AY8910::TTDSaveState() payload (SSG half)
    /// Everything else in the output stage (decimator FIR history/content,
    /// hold register, LQ boxcar accumulator, word queues, DC path, native
    /// taps) is genuinely just rendering cache - NOT part of TTD state, and
    /// TTDLoadState actively FLUSHES it to silence on restore (not merely
    /// "doesn't touch it") to avoid mixing stale pre-seek audio content with
    /// the freshly-restored generator output, which produced an audible
    /// click. Same policy as every other TTD-registered device otherwise.
public:
    size_t TTDStateSize() const override;
    void TTDSaveState(uint8_t* dst) const override;
    void TTDLoadState(const uint8_t* src) override;
    uint64_t TTDHashState() const override;

    ttd::PeripheralId TTDPeripheralId() const override
    {
        return ttd::PeripheralId::TSFM;
    }

    std::string TTDDeviceName() const override
    {
        return "TSFM";
    }
    /// endregion </TTDSerializable interface>
};
