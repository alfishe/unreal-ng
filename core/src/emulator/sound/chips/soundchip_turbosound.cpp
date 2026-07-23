#include "soundchip_turbosound.h"

#include "emulator/cpu/core.h"

/// region <Emulation events>
void SoundChip_TurboSound::handleFrameStart()
{
    _lastTStates = 0;
    _ayPLL = 0.0;
    _ayBufferIndex = 0;

    // Initialize render buffers (combined + per-chip)
    memset(_ayBuffer, 0x00, _ayAudioDescriptor.memoryBufferSizeInBytes);
    memset(_chip0Buffer, 0x00, _chip0AudioDescriptor.memoryBufferSizeInBytes);
    memset(_chip1Buffer, 0x00, _chip1AudioDescriptor.memoryBufferSizeInBytes);
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
        // Native-rate recording tap: active only during DSD capture.
        // Checked once per handleStep batch; per-tick cost is a plain bool.
        const bool tapActive = _nativeTap->isActive();

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

                    // Native-rate tap for DSD capture (pre-decimation, both chips summed)
                    if (tapActive)
                    {
                        _nativeTap->push(static_cast<float>(_chip0->mixedLeft() + _chip1->mixedLeft()),
                                         static_cast<float>(_chip0->mixedRight() + _chip1->mixedRight()));
                    }

                    // Feed mixed output to decimators
                    _chip0->decimatorLeft().feedSample(_chip0->mixedLeft());
                    _chip0->decimatorRight().feedSample(_chip0->mixedRight());
                    _chip1->decimatorLeft().feedSample(_chip1->mixedLeft());
                    _chip1->decimatorRight().feedSample(_chip1->mixedRight());
                }

                // Get decimated output per chip
                float c0L = _chip0->decimatorLeft().getOutput();
                float c0R = _chip0->decimatorRight().getOutput();
                float c1L = _chip1->decimatorLeft().getOutput();
                float c1R = _chip1->decimatorRight().getOutput();

                // Store per-chip buffers for registry-driven capture
                _chip0Buffer[_ayBufferIndex]     = static_cast<int16_t>(c0L * INT16_MAX);
                _chip0Buffer[_ayBufferIndex + 1] = static_cast<int16_t>(c0R * INT16_MAX);
                _chip1Buffer[_ayBufferIndex]     = static_cast<int16_t>(c1L * INT16_MAX);
                _chip1Buffer[_ayBufferIndex + 1] = static_cast<int16_t>(c1R * INT16_MAX);

                // Combined output
                leftSample = static_cast<int16_t>((c0L + c1L) * INT16_MAX);
                rightSample = static_cast<int16_t>((c0R + c1R) * INT16_MAX);
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

                    double l = _chip0->mixedLeft() + _chip1->mixedLeft();
                    double r = _chip0->mixedRight() + _chip1->mixedRight();

                    // Native-rate tap for DSD capture (pre-decimation)
                    if (tapActive)
                    {
                        _nativeTap->push(static_cast<float>(l), static_cast<float>(r));
                    }

                    // Accumulate samples
                    leftSum += l;
                    rightSum += r;
                    sampleCount++;
                }

                // Average (simple boxcar decimation)
                float c0L, c0R, c1L, c1R;
                if (sampleCount > 0)
                {
                    // Split the accumulated sums per chip for per-chip buffers
                    // In LQ mode we don't have separate accumulators, so approximate
                    // by using current chip output ratios
                    double total = leftSum + rightSum;
                    double r0 = (total > 0) ? (_chip0->mixedLeft() + _chip0->mixedRight()) /
                                              (_chip0->mixedLeft() + _chip0->mixedRight() +
                                               _chip1->mixedLeft() + _chip1->mixedRight() + 1e-9) : 0.5;

                    c0L = static_cast<float>((leftSum * r0) / sampleCount);
                    c0R = static_cast<float>((rightSum * r0) / sampleCount);
                    c1L = static_cast<float>((leftSum * (1.0 - r0)) / sampleCount);
                    c1R = static_cast<float>((rightSum * (1.0 - r0)) / sampleCount);

                    leftSample = static_cast<int16_t>((leftSum / sampleCount) * INT16_MAX);
                    rightSample = static_cast<int16_t>((rightSum / sampleCount) * INT16_MAX);
                }
                else
                {
                    // No ticks this sample - use previous value
                    c0L = static_cast<float>(_chip0->mixedLeft());
                    c0R = static_cast<float>(_chip0->mixedRight());
                    c1L = static_cast<float>(_chip1->mixedLeft());
                    c1R = static_cast<float>(_chip1->mixedRight());
                    leftSample = static_cast<int16_t>((c0L + c1L) * INT16_MAX);
                    rightSample = static_cast<int16_t>((c0R + c1R) * INT16_MAX);
                }

                // Store per-chip buffers
                _chip0Buffer[_ayBufferIndex]     = static_cast<int16_t>(c0L * INT16_MAX);
                _chip0Buffer[_ayBufferIndex + 1] = static_cast<int16_t>(c0R * INT16_MAX);
                _chip1Buffer[_ayBufferIndex]     = static_cast<int16_t>(c1L * INT16_MAX);
                _chip1Buffer[_ayBufferIndex + 1] = static_cast<int16_t>(c1R * INT16_MAX);
            }

            // Store samples in combined output buffer
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
                        _chip1Selected = true;  // Monitoring flag
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
