#include "soundchip_turbosound.h"

#include "emulator/cpu/core.h"

/// region <Emulation events>
void SoundChip_TurboSound::handleFrameStart()
{
    _lastTStates = 0;
    _ayPLL = 0.0;
    _ayBufferIndex = 0;

    // Initialize render buffers
    memset(_ayBuffer, 0x00, _ayAudioDescriptor.memoryBufferSizeInBytes);
}

void SoundChip_TurboSound::handleStep()
{
    size_t currentTStates = _context->pCore->GetZ80()->t;
    int32_t diff =  currentTStates - _lastTStates;

    if (diff > 0)
    {
        _ayPLL += diff * AUDIO_SAMPLE_TSTATE_INCREMENT;

        while (_ayPLL > 1.0 && _ayBufferIndex < AUDIO_SAMPLES_PER_VIDEO_FRAME * AUDIO_CHANNELS)
        {
            _ayPLL -= 1.0;

            int16_t leftSample;
            int16_t rightSample;

            // Start oversampling block for both chip FIR filters
            _chip0->firLeft().startOversamplingBlock();
            _chip0->firRight().startOversamplingBlock();
            _chip1->firLeft().startOversamplingBlock();
            _chip1->firRight().startOversamplingBlock();

            // Oversample and apply LPF FIR
            for (int j = FilterInterpolate::DECIMATE_FACTOR - 1; j >= 0; j--)
            {
                _x += _clockStep;

                if (_x >= 1.0)
                {
                    _x -= 1.0;

                    // Update both chips state synchronously
                    updateState(true);
                }

                _chip0->firLeft().recalculateInterpolationCoefficients(j, _chip0->mixedLeft());
                _chip0->firRight().recalculateInterpolationCoefficients(j, _chip0->mixedRight());
                _chip1->firLeft().recalculateInterpolationCoefficients(j, _chip1->mixedLeft());
                _chip1->firRight().recalculateInterpolationCoefficients(j, _chip1->mixedRight());
            }

            leftSample = (_chip0->firLeft().endOversamplingBlock()  + _chip1->firLeft().endOversamplingBlock()) * INT16_MAX;
            rightSample = (_chip0->firRight().endOversamplingBlock() + _chip1->firRight().endOversamplingBlock()) * INT16_MAX;

            // Store samples in output buffer
            _ayBuffer[_ayBufferIndex++] = leftSample;
            _ayBuffer[_ayBufferIndex++] = rightSample;
        }
    }

    _lastTStates = currentTStates;
}

void SoundChip_TurboSound::handleFrameEnd()
{
}

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

        PortDevice* device = this;
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