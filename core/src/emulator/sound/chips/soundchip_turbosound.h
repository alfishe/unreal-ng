#pragma once

#include <stdafx.h>

#include <memory>

#include "common/sound/filters/filter_interpolate.h"
#include "emulator/emulatorcontext.h"
#include "emulator/sound/audio.h"
#include "emulator/sound/chips/soundchip_ay8910.h"
#include "emulator/sound/chips/ssgwritequeue.h"
#include "emulator/sound/native_audio_tap.h"
#include "emulator/sound/chips/iturbosounddevice.h"  // ITurboSoundDevice (TSFM design §3.3)

class SoundChip_TurboSound : public ITurboSoundDevice
{
    /// region <Fields>
protected:
    SoundChip_AY8910* _chip0 = nullptr;
    SoundChip_AY8910* _chip1 = nullptr;

    SoundChip_AY8910* _currentChip = nullptr;

    AudioFrameDescriptor _ayAudioDescriptor;                               // Audio descriptor for combined AY output
    int16_t* const _ayBuffer = (int16_t*)_ayAudioDescriptor.memoryBuffer;  // Shortcut to it's sample buffer

    // Per-chip buffers for registry-driven mixing (AY 1 / AY 2 capture)
    AudioFrameDescriptor _chip0AudioDescriptor;
    AudioFrameDescriptor _chip1AudioDescriptor;
    int16_t* const _chip0Buffer = (int16_t*)_chip0AudioDescriptor.memoryBuffer;
    int16_t* const _chip1Buffer = (int16_t*)_chip1AudioDescriptor.memoryBuffer;

    /// region <AY emulation>
    // Initialized at declaration: reset() re-derives them, but a freshly
    // constructed chip must be renderable BEFORE the first reset() - garbage
    // phase rendered clamped full-size frames until reset was called.
    //
    // _samplePhase is the same exact integer accumulator SoundManager uses
    // to decide how many samples it mixes per frame (T-states x core rate,
    // one output sample per CPU_CLOCK_RATE). Both start at 0 on reset() /
    // setCoreRate(), the device feeds it T-states clipped at the frame
    // boundary (handleStep), so the device renders exactly the count the
    // mixer consumes on every frame. The previous free-running double PLL
    // drifted against the mixer's accumulator and every disagreeing frame
    // left a zero sample (or dropped one) in the mix - an audible click
    // train (2026-09-13, FrameSampleCount_Test).
    uint64_t _samplePhase = 0;
    size_t _ayBufferIndex = 0;
    uint32_t _lastTStates = 0;

    // AY clock (SetPsgClock): PSG_CLOCK_RATE unless the machine switches it
    // (the Profi in hi-res: 1.5 MHz). _psgClock is what the generators run at
    // now; _psgClockRequested the latest request, which may still wait on the
    // render timeline as a clock marker in _ssgWrites[0] (applied when the
    // render cursor reaches its T-state, like a register write)
    uint32_t _psgClock = static_cast<uint32_t>(PSG_CLOCK_RATE);
    uint32_t _psgClockRequested = static_cast<uint32_t>(PSG_CLOCK_RATE);
    // One generator tick (8 AY clocks) on the base-T timeline: _tickT whole T
    // plus _tickSubT / kRenderSubUnits. 16 + 0 at 1.75 MHz (the fast path: the
    // fraction is never touched), 18 + 80/120 at 1.5 MHz. 120 sub-units make
    // every clock whose tick is a multiple of 1/2, 1/3, 1/4, 1/5, 1/6, 1/8 T
    // exact; any other clock rounds its tick to 1/120 T (the pitch still
    // follows the exact clock - only the tick a write lands on is rounded)
    static constexpr uint32_t kRenderSubUnits = 120;
    int64_t _tickT = 16;
    uint32_t _tickSubT = 0;
    uint32_t _renderSub = 0;  // the render cursor's fraction, in 1/kRenderSubUnits T

    // Native clock decimation (like amiga-paula PWM renderer)
    // Generators tick at _psgClock / 8, we decimate to _coreRate
    double _decimationPhase = 0.0;
    double _decimationStep = (double)(PSG_CLOCK_RATE / 8) /
                             (double)(AUDIO_SAMPLING_RATE * FilterInterpolate::DECIMATE_FACTOR);

    // Core output rate (multirate plan phase 6): the sample accumulator's
    // per-T-state increment, and the LQ boxcar tick ratio. Set via
    // setCoreRate(); defaults preserve legacy 44100 behavior.
    size_t _coreRate = AUDIO_SAMPLING_RATE;
    double _lqTicksPerSample = (double)(PSG_CLOCK_RATE / 8) / (double)AUDIO_SAMPLING_RATE;

    // HQ DSP flag (FIR filters vs simple averaging)
    bool _hqEnabled = true;
    // Anti-alias FIR tier, applied by setCoreRate ([SOUND] DecimatorQuality)
    FilterDecimator::Quality _decimatorQuality = FilterDecimator::Quality::Reference;

    // Output-stage suppression (design §6.1): pushed once per frame by the
    // manager; gates rendering only (the legacy device has no separate core)
    bool _synthesisSuppressed = false;

    // LQ -> HQ switch or synthesis resumed: the decimator histories hold audio
    // from before the gap; cleared at the next frame start (ISSUES #7)
    bool _outputFlushPending = false;

    // Render cursor on the T-state timeline (the same scheme as TSFM's, so the
    // two stay bit-identical): continuous across frames, rebased with the
    // pending writes, kTurboSoundRenderLagT behind them, +16 T per SSG tick.
    // Re-anchored on a rate or quality switch and when far off its lag
    int64_t _renderT = -kTurboSoundRenderLagT;
    bool _renderReanchor = false;
    // Last T-state seen (handleStep, port write): the frame rebase delta and
    // the TTD base. false until the first one after reset / restore
    int64_t _lastSeenT = 0;
    bool _seenT = false;
    // SSG register writes timed to their T-state, per chip (0 = _chip0, the
    // 0xFF chip; 1 = _chip1): the CPU sees a write at once, the generators on
    // the tick it falls in (SsgWriteQueue). TTD state
    SsgWriteQueue _ssgWrites[2];

    // Native-rate recording tap (218.75 kHz, pre-decimation).
    // shared_ptr so a DSD encoder worker can outlive this chip safely.
    std::shared_ptr<NativeAudioTap> _nativeTap = std::make_shared<NativeAudioTap>();

    /// endregion </AY emulation>

    /// endregion </Fields>

    /// region <Interfacing fields>
protected:
    bool _chipAttachedToPortDecoder = false;
    /// One AY only ([SOUND] TurboSound=Single): #FE / #FF written to #FFFD select an invalid register
    /// of the one chip instead of switching chips (a lone AY ignores register numbers above 15)
    bool _singleChip = false;
    PortDecoder* _portDecoder = nullptr;
    /// endregion </Interfacing fields>

    /// region <Properties>
public:
    uint16_t* getAudioBuffer()
    {
        return (uint16_t*)_ayBuffer;
    }

    // Per-chip buffer access for registry-driven mixing / capture
    /// Number of stereo sample pairs rendered into the frame buffers so far
    /// this frame (diagnostics / adaptivity tests)
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
    const int16_t* getChipBuffer(int index) const
    {
        if (index == 0)
            return _chip0Buffer;
        if (index == 1)
            return _chip1Buffer;
        return nullptr;
    }

    // Chip access for monitoring purposes
    SoundChip_AY8910* getChip(int index) const override
    {
        if (index == 0)
            return _chip0;
        if (index == 1)
            return _chip1;

        MLOGWARNING("Invalid chip index: %d", index);
        return nullptr;
    }

    int getChipCount() const override
    {
        if (_singleChip)
            return 1;
        int count = 0;
        if (_chip0)
            count++;

        if (_chip1)
            count++;

        return count;
    }
    /// endregion </Properties>

    /// region <Constructors / destructor>
public:
    void setSingleChip(bool single)
    {
        _singleChip = single;
        if (single)
            _currentChip = _chip0;
    }
    bool isSingleChip() const { return _singleChip; }

    SoundChip_TurboSound(EmulatorContext* context) : ITurboSoundDevice(context)
    {
        _chip0 = new SoundChip_AY8910(_context);
        _chip1 = new SoundChip_AY8910(_context);
        _currentChip = _chip0;  // Initialize after chips are created
    }

    virtual ~SoundChip_TurboSound()
    {
        if (_chip0)
        {
            _chip0->detachFromPorts();
            delete _chip0;
        }

        if (_chip1)
        {
            _chip1->detachFromPorts();
            delete _chip1;
        }
    }
    /// endregion </Constructors / destructor>

    /// region <Methods>
public:
    void reset() override
    {
        _chip0->reset();
        _chip1->reset();

        // Set Chip0 active by default
        _currentChip = _chip0;

        // Reset internal state (the sample accumulator restarts in step with
        // SoundManager::reset(), which zeroes its own)
        _lastTStates = 0;
        _samplePhase = 0;
        _ayBufferIndex = 0;

        // Native clock decimation setup
        // AY generators run at PSG_CLOCK_RATE / 8 (~218.75 kHz for 1.75 MHz clock)
        // The /8 prescaler is handled here, not inside updateState()
        // For HQ mode, we feed DECIMATE_FACTOR sub-samples per output sample to the FIR
        _decimationPhase = 0.0;
        _outputFlushPending = false;
        _renderT = -kTurboSoundRenderLagT;
        _renderSub = 0;
        _renderReanchor = false;
        _seenT = false;
        // The AY clock is the board's, not the chip's: a reset keeps the
        // machine's latest request (a clock marker still queued takes effect
        // now, the queue is dropped below)
        if (_psgClock != _psgClockRequested)
            applyPsgClock(_psgClockRequested);
        _ssgWrites[0].clear();
        _ssgWrites[1].clear();
        // Effective generator rate = _psgClock / 8
        // _decimationStep = how many generator ticks per FIR sub-sample
        _decimationStep = generatorRate() / (double)(_coreRate * FilterInterpolate::DECIMATE_FACTOR);

        // Reset decimators for native clock mode (state only - their
        // rate-designed coefficients from setCoreRate() are preserved)
        _chip0->decimatorLeft().reset();
        _chip0->decimatorRight().reset();
        _chip1->decimatorLeft().reset();
        _chip1->decimatorRight().reset();
    }

    void updateState(bool bypassPrescaler = false)
    {
        _chip0->updateState(bypassPrescaler);
        _chip1->updateState(bypassPrescaler);
    }

    // Feature cache update
    void setHQEnabled(bool enabled) override
    {
        if (enabled != _hqEnabled)
            _renderReanchor = true;
        if (enabled && !_hqEnabled)
            _outputFlushPending = true;  // the HQ decimators were not fed in LQ
        _hqEnabled = enabled;
    }

    void setSynthesisSuppressed(bool suppressed) override
    {
        if (!suppressed && _synthesisSuppressed)
            _outputFlushPending = true;  // nothing was rendered while suppressed
        _synthesisSuppressed = suppressed;
    }

    /// Set the core output rate (multirate plan phase 6): restarts the
    /// sample accumulator (SoundManager::applyCoreRate restarts its own at
    /// the same frame boundary), recomputes the decimation ratios and
    /// redesigns the HQ anti-alias FIRs. Call at construction / sound stack
    /// rebuild only, at a frame boundary.
    void setDecimatorQuality(FilterDecimator::Quality quality) override
    {
        _decimatorQuality = quality;
    }

    void setCoreRate(size_t rate) override
    {
        _coreRate = rate;
        _samplePhase = 0;
        _renderReanchor = true;
        _lqTicksPerSample = generatorRate() / (double)rate;
        _decimationStep = generatorRate() / (double)(rate * FilterInterpolate::DECIMATE_FACTOR);

        const double inputRate = generatorRate();
        _chip0->decimatorLeft().configure((double)rate, _decimatorQuality, false, inputRate);
        _chip0->decimatorRight().configure((double)rate, _decimatorQuality, false, inputRate);
        _chip1->decimatorLeft().configure((double)rate, _decimatorQuality, false, inputRate);
        _chip1->decimatorRight().configure((double)rate, _decimatorQuality, false, inputRate);
    }

    /// AY clock at run time (ITurboSoundDevice::SetPsgClock): rounded to
    /// 100 Hz, kMinPsgClock..kMaxPsgClock (false outside). Queued at the
    /// current T-state as a clock marker on chip 0's write queue, so it
    /// reaches the generators exactly there - writes before it render at the
    /// old clock, the tick period, LQ boxcar ratio and HQ decimator input
    /// rate change on the tick it falls in, the FIR history carries on (no
    /// click). Applied at once while synthesis is suppressed
    bool SetPsgClock(uint32_t hz) override;
    uint32_t GetPsgClock() const override
    {
        return _psgClock;
    }
    /// The latest request, a switch still queued on the render timeline included
    uint32_t GetRequestedPsgClock() const
    {
        return _psgClockRequested;
    }

    static constexpr uint32_t kPsgClockStepHz = 100;
    static constexpr uint32_t kMinPsgClock = 100'000;
    /// A clock marker carries the clock in 15 bits of 100 Hz
    static constexpr uint32_t kMaxPsgClock = 0x7FFF * kPsgClockStepHz;

    /// Track the Z80 frequency multiplier (turbo switches): the sample PLL
    /// consumes already-multiplied t-states (Z80::t), so the increment must
    /// shrink by the same factor. Frame boundary only - changing it mid-frame
    /// would glitch the free-running PLL phase
    size_t getCoreRate() const override
    {
        return _coreRate;
    }

    /// Native-rate recording tap (for DSD capture bypassing 44.1 kHz decimation)
    std::shared_ptr<NativeAudioTap> getNativeTap() const override
    {
        return _nativeTap;
    }
    /// endregion </Methods>

    /// region <Emulation events>
public:
    void handleFrameStart() override;
    void handleStep() override;
    void handleFrameEnd() override;
    /// endregion </Emulation events>

    /// region <Automation tap (MCP M7j)>
public:
    /// Install/remove the AY port-write log tap. Inert while sink == nullptr.
    /// Called with the emulation thread parked (analyzer activation path).
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

public:
    /// region <TTDSerializable interface (P1.5 - parent TDD 6.4)>
    /// Each child SoundChip_AY8910 serializes itself via its own TTDSerializable.
    size_t TTDStateSize() const override;
private:
    int64_t nowT() const;
    /// Timed SSG register write at the current T-state (see SsgWriteQueue);
    /// applied at once while synthesis is suppressed
    void queueSsgWrite(int chipIndex, uint8_t reg, uint8_t value);
    /// One dequeued entry: an SSG register write, or a clock marker
    void applySsgWrite(int chipIndex, const SsgWrite& write);
    /// Switch the generators to hz now (tick period, LQ ratio, HQ decimator input rate)
    void applyPsgClock(uint32_t hz);
    /// The generator (tick) rate: the AY clock / 8, 218.75 kHz by default
    double generatorRate() const
    {
        return static_cast<double>(_psgClock) / 8.0;
    }
    /// One generator tick on the render cursor
    void advanceRenderCursor()
    {
        _renderT += _tickT;
        if (_tickSubT != 0) [[unlikely]]
        {
            _renderSub += _tickSubT;
            if (_renderSub >= kRenderSubUnits)
            {
                _renderSub -= kRenderSubUnits;
                ++_renderT;
            }
        }
    }
    /// Apply every pending SSG write timed at or before t (render cursor)
    void applySsgWrites(int64_t t);
    /// Apply every pending SSG write now (nothing will tick them in)
    void applyAllSsgWrites();
public:
    void   TTDSaveState(uint8_t* dst) const override;
    void   TTDLoadState(const uint8_t* src) override;

    /// Identity used by TTDPeripheralRegistry. Without it the base class
    /// returns PeripheralId::Count and the device cannot be indexed in a
    /// checkpoint's blob map.
    ttd::PeripheralId TTDPeripheralId() const override { return ttd::PeripheralId::TurboSound; }
    std::string TTDDeviceName() const override { return "TurboSound"; }
    /// endregion </TTDSerializable interface>
};
