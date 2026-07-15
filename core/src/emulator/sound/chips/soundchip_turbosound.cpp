#include "soundchip_turbosound.h"

#include "emulator/cpu/core.h"

/// region <Emulation events>
void SoundChip_TurboSound::handleFrameStart()
{
    _lastTStates = 0;
    _ayPLL = 0.0;
    _ayBufferIndex = 0;

    // Reset per-frame activity counters for monitoring
    _chip0->resetActivityCounters();
    _chip1->resetActivityCounters();

    // Initialize render buffers
    memset(_ayBuffer, 0x00, _ayAudioDescriptor.memoryBufferSizeInBytes);
}

/// @brief Generate audio samples synchronized to CPU t-states
///
/// ## Native Clock Architecture
///
/// AY-3-8910 runs at PSG_CLOCK_RATE (1.75 MHz for Pentagon, 1.7734 MHz for Spectrum 128).
/// Internally, the chip divides this by 16 for tone/noise generators (~109.375 kHz effective rate).
///
/// Previous implementation used 64x oversampling at 44100*64 = 2.8224 MHz, which is
/// asynchronous to the chip clock. This caused:
/// - Timing jitter (±0.18 µs per event)
/// - FM sidebands from beating between the two frequencies
/// - Subtle "impurity" on high notes
///
/// New implementation:
/// - Generators tick at true PSG_CLOCK_RATE (with internal /16 prescaler)
/// - Fractional decimation (PSG_CLOCK_RATE → 44100 Hz) using phase accumulator
/// - Same approach as amiga-paula PWM renderer
///
/// Benefits:
/// - Zero jitter on generator events
/// - ~37% less CPU (1.77 MHz < 2.82 MHz)
/// - Correct relationship between chip clock and generator periods
void SoundChip_TurboSound::handleStep()
{
    size_t currentTStates = _context->pCore->GetZ80()->t;

    // Scale t-states by speed multiplier for correct AY audio pitch
    uint8_t speedMultiplier = _context->emulatorState.current_z80_frequency_multiplier;
    size_t scaledCurrentTStates = currentTStates * speedMultiplier;

    int32_t diff = scaledCurrentTStates - _lastTStates;

    if (diff > 0)
    {
        _ayPLL += diff * AUDIO_SAMPLE_TSTATE_INCREMENT;

        while (_ayPLL > 1.0 && _ayBufferIndex < AUDIO_SAMPLES_PER_VIDEO_FRAME * AUDIO_CHANNELS)
        {
            _ayPLL -= 1.0;

            int16_t leftSample;
            int16_t rightSample;

            if (_hqEnabled)
            {
                // ========== HIGH QUALITY MODE ==========
                // Native clock rendering + FIR decimation
                //
                // Generators tick at PSG_CLOCK_RATE/8 = 218.75 kHz
                // FIR decimates to 44.1 kHz (~4.96:1 ratio)

                // Feed generator samples to decimator until we have an output
                while (!_chip0->decimatorLeft().hasOutput())
                {
                    // Tick generators (bypass internal prescaler)
                    updateState(true);

                    // Feed mixed output to decimators
                    _chip0->decimatorLeft().feedSample(_chip0->mixedLeft());
                    _chip0->decimatorRight().feedSample(_chip0->mixedRight());
                    _chip1->decimatorLeft().feedSample(_chip1->mixedLeft());
                    _chip1->decimatorRight().feedSample(_chip1->mixedRight());
                }

                // Get decimated output
                leftSample = (_chip0->decimatorLeft().getOutput() +
                              _chip1->decimatorLeft().getOutput()) * INT16_MAX;
                rightSample = (_chip0->decimatorRight().getOutput() +
                               _chip1->decimatorRight().getOutput()) * INT16_MAX;
            }
            else
            {
                // ========== LOW QUALITY MODE ==========
                // Native clock rendering + simple averaging (no FIR)
                // Faster but may have aliasing on high frequencies

                double leftSum = 0.0;
                double rightSum = 0.0;
                int sampleCount = 0;

                // Run generator ticks for this output sample period
                // Generator rate = PSG_CLOCK_RATE / 8 (~218.75 kHz)
                // Ticks per output sample = (PSG_CLOCK_RATE / 8) / AUDIO_SAMPLING_RATE ≈ 4.96
                double ticksPerSample = (double)(PSG_CLOCK_RATE / 8) / (double)AUDIO_SAMPLING_RATE;
                _decimationPhase += ticksPerSample;

                while (_decimationPhase >= 1.0)
                {
                    _decimationPhase -= 1.0;

                    // Tick generators directly (bypass internal prescaler)
                    updateState(true);

                    // Accumulate samples
                    leftSum += _chip0->mixedLeft() + _chip1->mixedLeft();
                    rightSum += _chip0->mixedRight() + _chip1->mixedRight();
                    sampleCount++;
                }

                // Average (simple boxcar decimation)
                if (sampleCount > 0)
                {
                    leftSample = (leftSum / sampleCount) * INT16_MAX;
                    rightSample = (rightSum / sampleCount) * INT16_MAX;
                }
                else
                {
                    // No ticks this sample - use previous value
                    leftSample = (_chip0->mixedLeft() + _chip1->mixedLeft()) * INT16_MAX;
                    rightSample = (_chip0->mixedRight() + _chip1->mixedRight()) * INT16_MAX;
                }
            }

            // Store samples in output buffer
            _ayBuffer[_ayBufferIndex++] = leftSample;
            _ayBuffer[_ayBufferIndex++] = rightSample;
        }
    }

    _lastTStates = scaledCurrentTStates;
}

void SoundChip_TurboSound::handleFrameEnd() {}

/// endregion </Emulation events>

/// region <PortDevice interface methods>
uint8_t SoundChip_TurboSound::portDeviceInMethod(uint16_t port)
{
    uint8_t result = _currentChip->portDeviceInMethod(port);

    return result;
}

void SoundChip_TurboSound::portDeviceOutMethod(uint16_t port, uint8_t value)
{
    switch (port)
    {
        case PORT_FFFD:
            if (value > 0x0F)
            {
                /// region <Attempt to switch active chip>
                switch (value)
                {
                    case 0xFF:
                        _currentChip = _chip0;
                        break;
                    case 0xFE:
                        _currentChip = _chip1;
                        _chip1Selected = true;
                        break;
                    default:
                        break;
                }
                /// endregion </Attempt to switch active chip>
            }
            _currentChip->setRegister(value);
            break;
        case PORT_BFFD:
            _currentChip->writeCurrentRegister(value);
            break;
        default:
            break;
    }
}
/// endregion </PortDevice interface methods>

/// region <Ports interaction>
bool SoundChip_TurboSound::attachToPorts(PortDecoder* decoder)
{
    bool result = false;

    if (decoder)
    {
        _portDecoder = decoder;

        [[maybe_unused]] PortDevice* device = this;
        result = decoder->RegisterPortHandler(0xBFFD, this);
        result &= decoder->RegisterPortHandler(0xFFFD, this);

        if (result)
        {
            _chipAttachedToPortDecoder = true;
        }
    }

    return result;
}

void SoundChip_TurboSound::detachFromPorts()
{
    if (_portDecoder && _chipAttachedToPortDecoder)
    {
        _portDecoder->UnregisterPortHandler(0xBFFD);
        _portDecoder->UnregisterPortHandler(0xFFFD);

        _chipAttachedToPortDecoder = false;
    }
}
/// endregion </Ports interaction>
