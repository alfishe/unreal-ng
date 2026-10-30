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
/// With the core's synthesis skipped (turbo or sound off, no TTD) nothing but
/// a port access can observe the chips, and advancing them by a + b clocks
/// equals a, then b: the step only notes the CPU's position and the chips
/// catch up at the next port access, the frame end or a TTD recording start.

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

    /// FM loudness baseline (§7.1, ISSUES.md #1): full-scale DAC word -> 0.703 in the mix, the real board's measured
    /// level (one carrier at TL 0 sits 0.8 dB above one SSG channel at volume 15). Exactly 0.30 x 10^(7.4 / 20): the
    /// MiSTer-derived 0.30 plus the measured 7.4 dB, which the shipped inis carried as TSFM_FmTrimDb=7.4 before
    static constexpr double kFmBaseGain = 0.7032686445959766;
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

    // Deferred core sync (file comment): the CPU position the last step noted, and whether the chips still have
    // to catch up to it. Allowed while the core's synthesis is skipped, until a TTD recording starts mid-frame
    uint64_t _deferredT = 0;

    // HQ output stage: newest consecutive zeros fed to each SSG decimator (chip 0 L, R, chip 1 L, R); a silent one
    // skips its FIR (handleStep). Render-side, not TTD state
    size_t _ssgZeroRun[4] = {};
    bool _syncDeferred = false;
    bool _deferAllowed = true;
    // The step gate (ITurboSoundDevice::_stepDeadline): the lazy deferral of the step that set it holds until then, so
    // a step only notes its T and the deferral's bookkeeping is done once (settleSteps)
    // The quiet deferral: the gate holds until the next entry (a port, the frame end...) because the device is quiet at
    // rest - every output is +0.0 until a write; settleSteps then runs the step at the last noted T, which renders the
    // outputs due in one quiet run (renderQuietOutputs)
    bool _quietDeferral = false;
    std::vector<uint32_t> _quietOutputTicks;  // renderQuietOutputs: the ticks of each output of the run
    size_t _longestQuietRun = 0;              // the most outputs one quiet run took (diagnostics, not state)
    // With audio on: a step that completes no output sample defers its sync as well (handleStep). Off: every such step
    // syncs, as before (comparison tests)
    bool _lazySync = true;
    // Off: no step gate (comparisons; settleSteps)
    bool _stepGate = true;

    // TTD render pin (setTtdRenderPinned): the render mode it runs (quality and suppression: the recording's, taken
    // from the host at each frame start or from the blob on a load) and the host's own settings; not TTD state
    bool _ttdPinned = false;
    bool _ttdFollowsHost = false;
    bool _pinnedHQ = true;
    bool _pinnedSuppressed = false;
    bool _hostHQ = true;
    bool _hostSuppressed = false;
    bool _hostCoreSkipped = false;

    // FM gain = kFmBaseGain * 10^(trim/20) (§7.1); [SOUND] TSFM_FmLevelDb
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
        catchUp();
        _pair.syncTo(t);
    }

    /// The chips to the CPU position the last step noted (a no-op unless the
    /// step deferred it, see the file comment)
    void catchUp() override
    {
        settleSteps();
        if (_syncDeferred)
        {
            _syncDeferred = false;
            _pair.syncTo(_deferredT);
        }
    }

    /// Frame-relative T-state of the CPU right now (the port callbacks see
    /// the IORQ's own T-state; handleStep then advances to the instruction
    /// end - §5.3 note)
    uint64_t nowT() const;

    /// The steps the gate let through, booked as the lazy deferral books one (handleStep), and the gate closed: the
    /// next step goes the full way. Every entry other than handleStep starts here
    void settleSteps()
    {
        _stepDeadline = 0;
        if (!_stepPending)
        {
            _quietDeferral = false;
            return;
        }
        _stepPending = false;
        if (_quietDeferral)
        {
            // The step at the last noted T, as handleStep would have taken it; the entry that called this then acts on
            // the device, so the gate stays open after it
            _quietDeferral = false;
            stepAt(_pendingT);
            _stepDeadline = 0;
            _quietDeferral = false;
            return;
        }
        // The deferral's bookkeeping (x1 speed: the gate holds only then) for the last noted T, frame-clipped as the
        // step clips it; the steps before it added their part of the same sum
        const uint64_t frameT = uint64_t(_context->config.frame);
        const uint32_t scaled = uint32_t(std::min(_pendingT, frameT ? frameT : _pendingT));
        if (scaled > _lastTStates)
            _samplePhase += uint64_t(scaled - _lastTStates) * _coreRate;
        _lastTStates = scaled;
        _deferredT = _pendingT;
        _syncDeferred = true;
    }

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

    // The host's render settings. While TTD pins the render path (setTtdRenderPinned) the device runs the pinned
    // mode: a recording takes the host's quality and suppression at each frame start, a replay keeps the blob's
    void setHQEnabled(bool enabled) override
    {
        _hostHQ = enabled;
        if (!_ttdPinned)
            applyHQ(enabled);
    }

    void setSynthesisSuppressed(bool suppressed) override
    {
        _hostSuppressed = suppressed;
        if (!_ttdPinned)
            applySynthesisSuppressed(suppressed);
    }

    void setCoreSynthesisSkipped(bool skipped) override
    {
        _hostCoreSkipped = skipped;
        applyCoreSynthesisSkipped(skipped && !_ttdPinned);
        _deferAllowed = true;
    }

    /// Pinned in the mode the host renders now (a recording's start); a checkpoint load pins the blob's
    void setTtdRenderPinned(bool pinned, bool followsHost) override
    {
        _ttdFollowsHost = pinned && followsHost;
        if (pinned == _ttdPinned)
            return;
        _ttdPinned = pinned;
        _pinnedHQ = _hostHQ;
        _pinnedSuppressed = _hostSuppressed;
        applyRenderMode();
    }
    bool isTtdRenderPinned() const { return _ttdPinned; }
    bool isCoreSynthesisSkipped() const { return _pair.coreSynthesisSkipped(); }
private:
    void applyRenderMode()
    {
        applyHQ(_ttdPinned ? _pinnedHQ : _hostHQ);
        applySynthesisSuppressed(_ttdPinned ? _pinnedSuppressed : _hostSuppressed);
        applyCoreSynthesisSkipped(_hostCoreSkipped && !_ttdPinned);
    }
    void applyHQ(bool enabled)
    {
        settleSteps();
        if (enabled != _pair.hqEnabled())
            _renderReanchor = true;
        if (enabled && !_pair.hqEnabled())
            _outputFlushPending = true;  // the HQ decimators were not fed in LQ
        _pair.setHQEnabled(enabled);
    }
    void applySynthesisSuppressed(bool suppressed)
    {
        settleSteps();
        if (!suppressed && _pair.synthesisSuppressed())
            _outputFlushPending = true;  // nothing was rendered while suppressed
        _pair.setSynthesisSuppressed(suppressed);
    }
    void applyCoreSynthesisSkipped(bool skipped)
    {
        catchUp();  // the deferred clocks run with the flag they were noted under
        _pair.setCoreSynthesisSkipped(skipped);
    }
public:
    /// Tests: off = the chips are brought to the CPU after every instruction while audio renders
    void setLazySync(bool on)
    {
        catchUp();
        _lazySync = on;
    }
    /// The most outputs one quiet run took (renderQuietOutputs): past the outputs of a step or two only while asleep
    size_t longestQuietRun() const { return _longestQuietRun; }
    /// Tests: off = every step through the full deferral path (the step gate of handleStep closed)
    void setStepGate(bool on)
    {
        catchUp();
        _stepGate = on;
    }

    /// Redesigns all six decimators for the rate (§6.3): four SSG ones at
    /// the generator rate (identical to the legacy device — bit-identity,
    /// §11), two FM ones at kTsfmFmInputRate in slave mode under chip-0 SSG left
    void setDecimatorQuality(FilterDecimator::Quality quality) override
    {
        _decimatorQuality = quality;
    }

    void setCoreRate(size_t rate) override
    {
        settleSteps();
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

    /// FM loudness trim in dB relative to the measured hardware level (§7.1,
    /// 0 = the board); live-adjustable from the Audio Settings dialog
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
        settleSteps();
        return (uint16_t*)_ayBuffer;
    }

    size_t getRenderedSamplesThisFrame() const override
    {
        const_cast<SoundChip_TurboSoundFM*>(this)->settleSteps();
        return _ayBufferIndex / AUDIO_CHANNELS;
    }

    uint64_t getSamplePhase() const override
    {
        const_cast<SoundChip_TurboSoundFM*>(this)->settleSteps();
        return _samplePhase;
    }
    void followSamplePhase(uint64_t phase) override
    {
        settleSteps();
        _samplePhase = phase;
    }

    int16_t* getChipBuffer(int index) override
    {
        settleSteps();
        if (index == 0)
            return _chip0Buffer;
        if (index == 1)
            return _chip1Buffer;
        return nullptr;
    }

    int16_t* getFmBuffer(int index) override
    {
        settleSteps();
        if (index == 0)
            return _fm0Buffer;
        if (index == 1)
            return _fm1Buffer;
        return nullptr;
    }

    int getSelectedChip() const override { return _board.chip; }
    int getBusReadRegister() const override
    {
        // As portDeviceInMethod: the status byte in status mode, #FF while an FM address is latched
        const Ym2203Chip* c = _pair.chip(_board.chip);
        return !_board.statusRead && c != nullptr && c->address < 0x10 ? c->address : -1;
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

private:
    /// handleStep's full way for a step that ended at audio T `now`
    void stepAt(uint64_t now);
    /// The outputs due (as many as the sample phase and the buffer hold) while the pair is quiet at rest over them, in
    /// one run, HQ; the count it took (0: none). Quiet at rest: both SSGs silentAtRest, the four SSG decimator histories
    /// zeros, each chip's FM output stage at rest (Ym2203Pair::renderQuiet), and no SSG write due at any of the run's
    /// ticks, no FM word off the hold at any of their half-ticks. Every output is then +0.0 on every row: the
    /// generators, the words consumed, the render cursor and the decimator positions and phases move as the ticks would
    size_t renderQuietOutputs();
    /// Every output stays +0.0 until something enters the device: quiet at rest with nothing queued and both FM halves
    /// idle, holding 0 (the gate may then hold until the next entry, handleStep). HQ, x1, the lazy sync on
    bool sleepsThroughSilence() const;

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
    ///   u8 board (chip | statusRead<<1 | fmEnabled<<2 | renderReanchor<<3: a
    ///     re-anchor of the render cursor pending for the next frame start - only
    ///     a capture inside a frame sees it set; 0 in blobs written before |
    ///     lqRender<<4: the render path was LQ, which a load pins; 0 = HQ in
    ///     blobs written before | adoptsClock<<5: the pair adopts the CPU's
    ///     clock at its next sync, a capture right after a reset |
    ///     suppressed<<6: the frame renders no output (sound off, turbo), which
    ///     a load pins; 0 in blobs written before)
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

    /// The recording's first capture sees the chips at the CPU: no deferred
    /// sync from here to the frame start that re-reads the TTD state. The
    /// render path is pinned from the baseline on, in the mode the sound
    /// manager has now (the manager pins it again from the next frame start)
    void TTDRecordingStarted() override;
    /// endregion </TTDSerializable interface>
};
