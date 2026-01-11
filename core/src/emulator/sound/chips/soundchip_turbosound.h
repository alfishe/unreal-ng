#pragma once

#include <stdafx.h>

#include "common/sound/filters/filter_interpolate.h"
#include "emulator/emulatorcontext.h"
#include "emulator/sound/audio.h"
#include "emulator/sound/chips/soundchip_ay8910.h"

class SoundChip_TurboSound : public PortDecoder, public PortDevice
{
    /// region <Fields>
protected:
    SoundChip_AY8910* _chip0 = nullptr;
    SoundChip_AY8910* _chip1 = nullptr;

    SoundChip_AY8910* _currentChip = _chip0;

    AudioFrameDescriptor _ayAudioDescriptor;                               // Audio descriptor for AY
    int16_t* const _ayBuffer = (int16_t*)_ayAudioDescriptor.memoryBuffer;  // Shortcut to it's sample buffer

    /// region <AY emulation>
    double _ayPLL;
    size_t _ayBufferIndex;
    uint32_t _lastTStates;

    double _clockStep;
    double _x;

    // HQ DSP flag (FIR filters + oversampling)
    bool _hqEnabled = true;
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

        _x = 0.0;
        double oversample_stream_rate =
            AUDIO_SAMPLING_RATE * 8 *
            FilterInterpolate::DECIMATE_FACTOR;  // 2822400 bits per second for 44100Hz sample rate
        _clockStep = PSG_CLOCK_RATE / oversample_stream_rate;

        // Set FIR parameters
        _chip0->firLeft().setRates(PSG_CLOCK_RATE, AUDIO_SAMPLING_RATE);
        _chip0->firRight().setRates(PSG_CLOCK_RATE, AUDIO_SAMPLING_RATE);
        _chip1->firLeft().setRates(PSG_CLOCK_RATE, AUDIO_SAMPLING_RATE);
        _chip1->firRight().setRates(PSG_CLOCK_RATE, AUDIO_SAMPLING_RATE);
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
