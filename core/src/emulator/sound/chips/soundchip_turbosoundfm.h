#pragma once

#include <stdafx.h>

#include <memory>
#include <vector>

#include "common/sound/filters/filter_decimator.h"
#include "common/sound/filters/filterdcblocker.h"
#include "emulator/emulatorcontext.h"
#include "emulator/sound/audio.h"
#include "emulator/sound/chips/iturbosounddevice.h"
#include "emulator/sound/chips/tsfm/ym2203pair.h"
#include "emulator/sound/native_audio_tap.h"

/// @file soundchip_turbosoundfm.h
/// @brief TSFM - the TurboSound FM board (2 x YM2203) chip core (design §5).
///
/// The CPLD's three latches (TsfmBoard) select one of the two YM2203 of the
/// shared Ym2203Pair (tsfm/ym2203pair.h: chips, timers, busy, the T-state
/// core loop, the output-stage primitives) for the AY-bus ports. The board
/// keeps the #FFFD control-word parse, the FM mute and its stereo mix on top.
/// The core is advanced in T-states (syncTo) so it is in step with the CPU
/// at every instruction boundary - the invariant TTD checkpointing relies on
/// (§5.2). The pair runs at 1 : 1 here (YM master clock = audio T-state).

/// The logic chip's three latches, clocked by a control word
/// (OUT #FFFD with value 0xF8..0xFF, design §5.1 / hardware-reference §3.2)
struct TsfmBoard
{
    uint8_t chip = 0;         // 0 = first chip (D1); bit 0 of the control word
    bool statusRead = false;  // true: IN #FFFD reads the status byte; bit 1 = 0
    bool fmEnabled = false;   // FM audio path enabled; bit 2 = 0 (muted)
};

/// FM input rate (§6.3): the YM2203 sample clock at prescaler /6 is
/// PSG_CLOCK_RATE/4 = 437.5 kHz = exactly 2x the SSG generator rate
constexpr double kTsfmFmInputRate = kYm2203FmInputRate;

/// FM output coupling (schematic rev C): each YM3014 buffer output (FM1/FM2)
/// reaches the DA5 mixer through C14/C15 = 10 uF into two 24 k resistors to
/// the op-amp virtual grounds (R17||R18, R19||R20 = 12 k):
/// fc = 1 / (2 pi * 12 k * 10 uF) = 1.33 Hz (the pair's default fmCouplingHz)
constexpr double kTsfmFmCouplingHz = 1.0 / (2.0 * 3.14159265358979323846 * 12000.0 * 10e-6);

/// The chip and its output-stage state live in the shared pair
using TsfmChip = Ym2203Chip;
using TsfmOutputState = Ym2203OutputState;

class SoundChip_TurboSoundFM : public ITurboSoundDevice
{
    /// region <Fields>
protected:
    TsfmBoard _board;
    // The two YM2203 with their T-state core loop and output-stage state, at
    // 1 : 1 (YM master clock = audio T-state) with the board's FM coupling
    Ym2203Pair _pair;

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

    /// FM loudness baseline (§7.1): full-scale DAC word -> 0.30 in the mix
    static constexpr double kFmBaseGain = 0.30;
    /// Constant lag of the render cursor behind the word and SSG-write
    /// timeline (§6.2, see kTurboSoundRenderLagT)
    static constexpr int64_t kFmRenderLagT = Ym2203Pair::kRenderLag;

    size_t _coreRate = AUDIO_SAMPLING_RATE;
    // Anti-alias FIR tier, applied by setCoreRate ([SOUND] DecimatorQuality)
    FilterDecimator::Quality _decimatorQuality = FilterDecimator::Quality::Reference;
    bool _prescalerWarned = false;  // one §9.4 warning per device instance

    // Render loop state — the legacy SoundChip_TurboSound loop copied
    // verbatim (§11 bit-identity): the mixer-exact sample accumulator (see
    // SoundChip_TurboSound::_samplePhase), per-frame buffer cursor, T-state
    // axis base, LQ boxcar phase; the §6.2 FM cursor is the pair's render
    // cursor (continuous across frames, rebased with the words, kFmRenderLagT
    // behind)
    uint64_t _samplePhase = 0;
    size_t _ayBufferIndex = 0;
    uint32_t _lastTStates = 0;
    double _decimationPhase = 0.0;
    double _lqTicksPerSample = (double)(PSG_CLOCK_RATE / 8) / (double)AUDIO_SAMPLING_RATE;
    // Rate or quality switch: the render loop's position against the CPU
    // clock moved; re-anchor the cursor at the next frame start
    bool _renderReanchor = false;
    // LQ -> HQ switch or synthesis resumed: the output stage holds audio from
    // before the gap; flushed at the next frame start (emulation thread)
    bool _outputFlushPending = false;

    // FM gain = kFmBaseGain * 10^(trim/20) (§7.1); [SOUND] TSFM_FmTrimDb
    double _fmGain = kFmBaseGain;
    double _fmTrimDb = 0.0;

    // Native-rate recording tap (218.75 kHz SSG side). The FM taps
    // (out.nativeTap, §6.4) carry the raw pre-mute DAC stream per chip.
    std::shared_ptr<NativeAudioTap> _nativeTap = std::make_shared<NativeAudioTap>();

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
        return _pair.chip(index);
    }

    /// The shared YM2203 pair (tests, the state report)
    Ym2203Pair& pair()
    {
        return _pair;
    }

    /// T-state the core has been advanced to (frame-relative)
    uint64_t syncedT() const
    {
        return _pair.syncedT();
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
    SoundChip_TurboSoundFM(EmulatorContext* context)
        : ITurboSoundDevice(context),
          _pair(context, Ym2203PairConfig{static_cast<uint32_t>(CPU_CLOCK_RATE), static_cast<uint32_t>(CPU_CLOCK_RATE),
                                          kTsfmFmCouplingHz})
    {
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
    void syncTo(uint64_t t)
    {
        _pair.syncTo(t);
    }

    /// Frame-relative T-state of the CPU right now (the port callbacks see
    /// the IORQ's own T-state; handleStep then advances to the instruction
    /// end - §5.3 note)
    uint64_t nowT() const;

    /// One FM half-tick of chip `chipIndex` ending at half-tick boundary h
    /// (§6.2): consume every word that landed by h into the hold register
    /// (the tap sees the raw pre-mute value), then feed the gated hold —
    /// board mute grounds the DAC data line — to the HQ decimator or the LQ
    /// boxcar accumulator. Public: §12.4 drives half-tick sequences directly
    void fmHalfTick(int chipIndex, int64_t h)
    {
        _pair.fmHalfTick(chipIndex, h, _board.fmEnabled);
    }
    /// endregion </Core loop>

    /// region <Methods>
public:
    void reset() override;

    void updateState(bool bypassPrescaler = false)
    {
        _pair.updateState(bypassPrescaler);
    }

    void setHQEnabled(bool enabled) override
    {
        if (enabled != _pair.hqEnabled())
            _renderReanchor = true;
        if (enabled && !_pair.hqEnabled())
            _outputFlushPending = true;  // the HQ decimators were not fed in LQ
        _pair.setHQEnabled(enabled);
    }

    void setSynthesisSuppressed(bool suppressed) override
    {
        if (!suppressed && _pair.synthesisSuppressed())
            _outputFlushPending = true;  // nothing was rendered while suppressed
        _pair.setSynthesisSuppressed(suppressed);
    }

    void setCoreSynthesisSkipped(bool skipped) override
    {
        _pair.setCoreSynthesisSkipped(skipped);
    }
    bool isCoreSynthesisSkipped() const { return _pair.coreSynthesisSkipped(); }

    /// Redesigns all six decimators for the rate (§6.3): four SSG ones at
    /// the generator rate (identical to the legacy device — bit-identity,
    /// §11), two FM ones at kTsfmFmInputRate in slave mode under chip-0 SSG left
    void setDecimatorQuality(FilterDecimator::Quality quality) override
    {
        _decimatorQuality = quality;
    }

    void setCoreRate(size_t rate) override
    {
        _coreRate = rate;
        _samplePhase = 0;  // in step with SoundManager::applyCoreRate
        _renderReanchor = true;
        _lqTicksPerSample = (double)(PSG_CLOCK_RATE / 8) / (double)rate;

        // FM decimators run in slave mode: chip-0 SSG left gates the output
        // cadence of every stream (§6.3)
        _pair.configureDecimators(rate, _decimatorQuality);
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

    int getSelectedChip() const override { return _board.chip; }

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
    /// §8.2 layout (v5), 2008 bytes, PeripheralId::TSFM = 4:
    ///   u8 version (= 5)
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
    ///     u8[73] SoundChip_AY8910::TTDSaveState() payload (SSG half)
    ///   v4 timeline tail (relative to the synced position; rebuilt around
    ///   the restored CPU position):
    ///     i64 render cursor offset (decides the tick of every SSG write)
    ///     per chip x2: u8 pending SSG write count, then
    ///       SsgWriteQueue::kCapacity x {i32 t offset, u8 reg, u8 value}
    ///   v5 frame-progress tail (the checkpoint is inside a started frame,
    ///   mid-frame for a recording's baseline):
    ///     u32 lastTStates (scaled frame position rendered up to)
    ///     u32 ayBufferIndex (samples produced this frame, x AUDIO_CHANNELS)
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

    /// ymfm's counters that advance with the FM clock, per chip (the pair's
    /// Ym2203Pair::TTDTimeFields): the blob's chips follow its 50-byte header
    /// (version, board byte, sample phase, five decimator phases). The engine
    /// stores each as its residual from a line
    static constexpr uint16_t kTsfmStateHeaderSize = 1 + 1 + 8 + 8 + 4 * 8;
    ttd::TTDDeviceDescriptor TTDDescribe() const override
    {
        ttd::TTDDeviceDescriptor d = ttd::TTDSerializable::TTDDescribe();
        d.runsBehindCpu = true;
        Ym2203Pair::TTDTimeFields(d.timeFields, kTsfmStateHeaderSize);
        return d;
    }

    /// Synced: the pair has been advanced to the CPU's T-state, or adopts the
    /// CPU's position at its next sync (after a reset or restore)
    bool TTDSyncedTime(int64_t& offset) const override
    {
        return _pair.TTDSyncedTime(nowT(), offset);
    }

    std::string TTDDeviceName() const override
    {
        return "TSFM";
    }
    /// endregion </TTDSerializable interface>
};
