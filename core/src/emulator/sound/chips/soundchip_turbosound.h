#pragma once

#include <stdafx.h>

#include <memory>

#include "common/sound/filters/filter_interpolate.h"
#include "emulator/emulatorcontext.h"
#include "emulator/sound/audio.h"
#include "emulator/sound/chips/soundchip_ay8910.h"
#include "emulator/sound/native_audio_tap.h"
#include "debugger/ttd/ttd_serializable.h"  // TTDSerializable (P1.5 peripheral serializer)

class SoundChip_TurboSound : public PortDecoder, public PortDevice, public ttd::TTDSerializable
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
    // _ayPLL rendered clamped full-size frames until reset was called
    double _ayPLL = 0.0;
    size_t _ayBufferIndex = 0;
    uint32_t _lastTStates = 0;

    // Native clock decimation (like amiga-paula PWM renderer)
    // Generators tick at PSG_CLOCK_RATE, we decimate to _coreRate
    double _decimationPhase = 0.0;
    double _decimationStep = (double)(PSG_CLOCK_RATE / 8) /
                             (double)(AUDIO_SAMPLING_RATE * FilterInterpolate::DECIMATE_FACTOR);

    // Core output rate (multirate plan phase 6): output samples per T-state
    // for the free-running sample PLL, and the LQ boxcar tick ratio. Set via
    // setCoreRate(); defaults preserve legacy 44100 behavior.
    size_t _coreRate = AUDIO_SAMPLING_RATE;
    double _sampleTStateIncrement = AUDIO_SAMPLE_TSTATE_INCREMENT;
    double _lqTicksPerSample = (double)(PSG_CLOCK_RATE / 8) / (double)AUDIO_SAMPLING_RATE;

    // Z80 frequency multiplier (turbo / speed control). Z80::t already
    // counts multiplied cycles, so the PLL increment is divided by the same
    // factor to keep the output sample rate realtime (AY pitch unchanged -
    // the PSG clock is fixed, not CPU-derived)
    uint8_t _frequencyMultiplier = 1;

    // HQ DSP flag (FIR filters vs simple averaging)
    bool _hqEnabled = true;

    // Native-rate recording tap (218.75 kHz, pre-decimation).
    // shared_ptr so a DSD encoder worker can outlive this chip safely.
    std::shared_ptr<NativeAudioTap> _nativeTap = std::make_shared<NativeAudioTap>();
    /// endregion </AY emulation>

    /// endregion </Fields>

    /// region <Interfacing fields>
protected:
    bool _chipAttachedToPortDecoder = false;
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
    size_t getRenderedSamplesThisFrame() const
    {
        return _ayBufferIndex / AUDIO_CHANNELS;
    }

    int16_t* getChipBuffer(int index)
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
    SoundChip_AY8910* getChip(int index) const
    {
        if (index == 0)
            return _chip0;
        if (index == 1)
            return _chip1;

        MLOGWARNING("Invalid chip index: %d", index);
        return nullptr;
    }

    int getChipCount() const
    {
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
    SoundChip_TurboSound(EmulatorContext* context) : PortDecoder(context)
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

        // Reset internal state
        _lastTStates = 0;
        _ayPLL = 0.0;
        _ayBufferIndex = 0;

        // Native clock decimation setup
        // AY generators run at PSG_CLOCK_RATE / 8 (~218.75 kHz for 1.75 MHz clock)
        // The /8 prescaler is handled here, not inside updateState()
        // For HQ mode, we feed DECIMATE_FACTOR sub-samples per output sample to the FIR
        _decimationPhase = 0.0;
        // Effective generator rate = PSG_CLOCK_RATE / 8
        // _decimationStep = how many generator ticks per FIR sub-sample
        _decimationStep = (double)(PSG_CLOCK_RATE / 8) / (double)(_coreRate * FilterInterpolate::DECIMATE_FACTOR);

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
    void setHQEnabled(bool enabled)
    {
        _hqEnabled = enabled;
    }

    /// Set the core output rate (multirate plan phase 6): recomputes the
    /// sample PLL increment and decimation ratios and redesigns the HQ
    /// anti-alias FIRs. Call at construction / sound stack rebuild only -
    /// changing rate mid-frame would glitch the free-running PLL phase.
    void setCoreRate(size_t rate)
    {
        _coreRate = rate;
        _sampleTStateIncrement = (double)rate / ((double)CPU_CLOCK_RATE * _frequencyMultiplier);
        _lqTicksPerSample = (double)(PSG_CLOCK_RATE / 8) / (double)rate;
        _decimationStep = (double)(PSG_CLOCK_RATE / 8) / (double)(rate * FilterInterpolate::DECIMATE_FACTOR);

        _chip0->decimatorLeft().configure((double)rate);
        _chip0->decimatorRight().configure((double)rate);
        _chip1->decimatorLeft().configure((double)rate);
        _chip1->decimatorRight().configure((double)rate);
    }

    /// Track the Z80 frequency multiplier (turbo switches): the sample PLL
    /// consumes already-multiplied t-states (Z80::t), so the increment must
    /// shrink by the same factor. Frame boundary only - changing it mid-frame
    /// would glitch the free-running PLL phase
    void setFrequencyMultiplier(uint8_t multiplier)
    {
        _frequencyMultiplier = multiplier ? multiplier : 1;
        _sampleTStateIncrement =
            (double)_coreRate / ((double)CPU_CLOCK_RATE * _frequencyMultiplier);
    }

    size_t getCoreRate() const
    {
        return _coreRate;
    }

    /// Native-rate recording tap (for DSD capture bypassing 44.1 kHz decimation)
    std::shared_ptr<NativeAudioTap> getNativeTap() const
    {
        return _nativeTap;
    }
    /// endregion </Methods>

    /// region <Emulation events>
public:
    void handleFrameStart();
    void handleStep();
    void handleFrameEnd();
    /// endregion </Emulation events>

    /// region <PortDevice interface methods>
public:
    uint8_t portDeviceInMethod(uint16_t port) override;
    void portDeviceOutMethod(uint16_t port, uint8_t value) override;
    /// endregion </PortDevice interface methods>

    /// region <Ports interaction>
public:
    bool attachToPorts(PortDecoder* decoder);
    void detachFromPorts();
    /// endregion </Ports interaction>

public:
    /// region <TTDSerializable interface (P1.5 - parent TDD 6.4)>
    /// Each child SoundChip_AY8910 serializes itself via its own TTDSerializable.
    size_t TTDStateSize() const override;
    void   TTDSaveState(uint8_t* dst) const override;
    void   TTDLoadState(const uint8_t* src) override;
    /// endregion </TTDSerializable interface>
};
