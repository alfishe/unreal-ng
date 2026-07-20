#pragma once

#include <stdafx.h>

#include <memory>

#include "common/sound/filters/filter_interpolate.h"
#include "emulator/emulatorcontext.h"
#include "emulator/sound/audio.h"
#include "emulator/sound/chips/soundchip_ay8910.h"
#include "emulator/sound/native_audio_tap.h"

class SoundChip_TurboSound : public PortDecoder, public PortDevice
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
    double _ayPLL;
    size_t _ayBufferIndex;
    uint32_t _lastTStates;

    // Native clock decimation (like amiga-paula PWM renderer)
    // Generators tick at PSG_CLOCK_RATE, we decimate to AUDIO_SAMPLING_RATE
    double _decimationPhase;
    double _decimationStep;

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
    void reset()
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
        _decimationStep = (double)(PSG_CLOCK_RATE / 8) / (double)(AUDIO_SAMPLING_RATE * FilterInterpolate::DECIMATE_FACTOR);

        // Reset decimators for native clock mode
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
    uint8_t portDeviceInMethod(uint16_t port);
    void portDeviceOutMethod(uint16_t port, uint8_t value);
    /// endregion </PortDevice interface methods>

    /// region <Ports interaction>
public:
    bool attachToPorts(PortDecoder* decoder);
    void detachFromPorts();
    /// endregion </Ports interaction>
};
