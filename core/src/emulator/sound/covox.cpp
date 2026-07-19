#include "covox.h"
#include "emulator/emulatorcontext.h"
#include "emulator/cpu/z80.h"
#include "emulator/cpu/core.h"

Covox::Covox(EmulatorContext* context)
    : _context(context)
{
    reset();
}

void Covox::reset()
{
    for (int i = 0; i < 4; i++)
        _dacValue[i] = 0x80;  // Midpoint = silence
    _prevFrameTState = 0;
    _dcAccumL = _dcAccumR = 0.0f;
    memset(_buffer, 0, _audioDescriptor.memoryBufferSizeInBytes);
}

void Covox::handleFrameStart()
{
    _prevFrameTState = 0;
    memset(_buffer, 0, _audioDescriptor.memoryBufferSizeInBytes);
}

void Covox::handleFrameEnd()
{
    // Render any remaining samples from the last write to end of frame
    CONFIG& config = _context->config;
    renderToBuffer(config.frame);

    // Optional DC offset removal
    if (_dcRemovalEnabled)
    {
        for (size_t i = 0; i < SAMPLES_PER_FRAME; i++)
        {
            float l = static_cast<float>(_buffer[i * 2]);
            float r = static_cast<float>(_buffer[i * 2 + 1]);

            _dcAccumL = _dcAccumL * DC_COEF + l * (1.0f - DC_COEF);
            _dcAccumR = _dcAccumR * DC_COEF + r * (1.0f - DC_COEF);

            _buffer[i * 2]     = static_cast<int16_t>(std::clamp(l - _dcAccumL, -32768.0f, 32767.0f));
            _buffer[i * 2 + 1] = static_cast<int16_t>(std::clamp(r - _dcAccumR, -32768.0f, 32767.0f));
        }
    }
}

uint8_t Covox::portDeviceInMethod([[maybe_unused]] uint16_t port)
{
    // Covox is write-only; reading returns floating bus or 0xFF
    return 0xFF;
}

Covox::Channel Covox::portToChannel(uint16_t port)
{
    uint8_t lowByte = port & 0xFF;
    switch (lowByte)
    {
        case 0xF1: return Channel::LeftA;
        case 0xF3: return Channel::LeftB;
        case 0xF9: return Channel::RightA;
        case 0xFB: return Channel::RightB;
        default:   return Channel::RightB;  // Fallback for mono COVOX compatibility
    }
}

void Covox::portDeviceOutMethod(uint16_t port, uint8_t value)
{
    // COVOX/SOUNDRIVE ports: #F1, #F3, #F9, #FB (bits[7:4]=1111, bit2=0, bit0=1)
    uint8_t lowByte = port & 0xFF;
    if ((lowByte & PORT_MASK) != PORT_MATCH)
        return;

    // Render samples from previous write to current t-state
    uint32_t currentTState = _context->pCore->GetZ80()->t;
    renderToBuffer(currentTState);

    // Update the appropriate channel's DAC value
    Channel ch = portToChannel(port);
    _dacValue[static_cast<int>(ch)] = value;
}

void Covox::renderToBuffer(uint32_t frameTState)
{
    CONFIG& config = _context->config;

    // Handle frame wraparound
    if (_prevFrameTState > frameTState)
    {
        _prevFrameTState = 0;
    }

    // Scale by speed multiplier
    uint8_t speedMultiplier = _context->emulatorState.current_z80_frequency_multiplier;
    uint32_t scaledFrame = config.frame * speedMultiplier;

    // Prevent division by zero
    if (scaledFrame == 0)
        return;

    size_t prevIndex = (static_cast<uint64_t>(_prevFrameTState) * SAMPLES_PER_FRAME) / scaledFrame;
    size_t currIndex = (static_cast<uint64_t>(frameTState) * SAMPLES_PER_FRAME) / scaledFrame;

    // Clamp to buffer bounds
    if (prevIndex >= SAMPLES_PER_FRAME)
        prevIndex = SAMPLES_PER_FRAME - 1;
    if (currIndex >= SAMPLES_PER_FRAME)
        currIndex = SAMPLES_PER_FRAME - 1;

    // Get samples for all 4 channels
    int16_t sampleLA = _channelMute[0] ? 0 : dacToSample(_dacValue[0]);
    int16_t sampleLB = _channelMute[1] ? 0 : dacToSample(_dacValue[1]);
    int16_t sampleRA = _channelMute[2] ? 0 : dacToSample(_dacValue[2]);
    int16_t sampleRB = _channelMute[3] ? 0 : dacToSample(_dacValue[3]);

    int16_t left, right;

    // Check if left channels have any actual audio (not at midpoint)
    // If all left channels are silent, treat port #FB as mono -> both channels
    bool leftHasAudio = (_dacValue[0] != 0x80) || (_dacValue[1] != 0x80);
    bool rightAHasAudio = (_dacValue[2] != 0x80);

    if (!leftHasAudio && !rightAHasAudio)
    {
        // Mono COVOX mode: only RightB (#FB) has audio, output to both channels
        left = sampleRB;
        right = sampleRB;
    }
    else
    {
        // Stereo SOUNDRIVE mode: Left = LeftA + LeftB, Right = RightA + RightB
        int32_t leftMix = sampleLA + sampleLB;
        int32_t rightMix = sampleRA + sampleRB;
        left = static_cast<int16_t>(std::clamp(leftMix, -32768, 32767));
        right = static_cast<int16_t>(std::clamp(rightMix, -32768, 32767));
    }

    // Fill gap with current mixed value (zero-order hold)
    // Include currIndex when rendering final frame segment
    for (size_t i = prevIndex; i <= currIndex && i < SAMPLES_PER_FRAME; i++)
    {
        _buffer[i * 2]     = left;
        _buffer[i * 2 + 1] = right;
    }

    _prevFrameTState = frameTState;
}

int16_t Covox::dacToSample(uint8_t value) const
{
    // Unsigned 8-bit DAC, centered: 0x80 = 0, 0x00 = -32768, 0xFF = +32512
    // This matches the hardware: writing 0x80 produces silence
    return static_cast<int16_t>((static_cast<int>(value) - 128) * 256);
}
