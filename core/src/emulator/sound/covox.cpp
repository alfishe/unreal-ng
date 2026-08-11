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
    _dcAccumL = _dcAccumR = 0.0f;
    memset(_buffer, 0, _audioDescriptor.memoryBufferSizeInBytes);
}

void Covox::handleFrameStart()
{
    // Pre-fill the entire buffer with the current held DAC value.
    // This implements proper zero-order hold: if no writes occur,
    // the output continues at the last known level.
    int16_t left, right;
    computeMixedOutput(left, right);
    for (size_t i = 0; i < SAMPLES_PER_FRAME; i++)
    {
        _buffer[i * 2]     = left;
        _buffer[i * 2 + 1] = right;
    }
}

void Covox::handleFrameEnd()
{
    // No rendering needed here - the buffer is always fully populated.
    // handleFrameStart pre-fills with held value, and each port write
    // forward-fills from the write point to frame end.

    // Optional DC offset removal (high-pass filter to remove DC bias)
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

    CONFIG& config = _context->config;
    uint8_t speedMultiplier = _context->emulatorState.current_z80_frequency_multiplier;
    uint32_t scaledFrame = config.frame * speedMultiplier;

    if (scaledFrame == 0)
        return;

    uint32_t currentTState = _context->pCore->GetZ80()->t;

    // Compute first affected sample using ceiling division.
    // A write at time T changes output starting at T, so it affects
    // the first sample whose time interval includes or follows T.
    //
    // Sample i covers [i * scaledFrame / N, (i+1) * scaledFrame / N).
    // First affected sample = ceil(T * N / scaledFrame).
    size_t firstAffected;
    if (currentTState == 0)
    {
        firstAffected = 0;
    }
    else
    {
        uint64_t numerator = static_cast<uint64_t>(currentTState) * SAMPLES_PER_FRAME;
        firstAffected = (numerator + scaledFrame - 1) / scaledFrame;
    }

    if (firstAffected >= SAMPLES_PER_FRAME)
        return;

    // Update the DAC value BEFORE rendering - the new value takes effect at this write
    Channel ch = portToChannel(port);
    _dacValue[static_cast<int>(ch)] = value;

    // Forward-fill from this sample to end of frame with the new value
    renderFromSample(firstAffected);
}

void Covox::computeMixedOutput(int16_t& left, int16_t& right) const
{
    // Always use stereo mixing formula - hardware sums channels on each side.
    // For mono COVOX (only RightB written), LeftA/LeftB/RightA stay at 0x80 (silence),
    // so the mix naturally produces RightB on both channels when other channels are silent.
    int16_t sampleLA = _channelMute[0] ? 0 : dacToSample(_dacValue[0]);
    int16_t sampleLB = _channelMute[1] ? 0 : dacToSample(_dacValue[1]);
    int16_t sampleRA = _channelMute[2] ? 0 : dacToSample(_dacValue[2]);
    int16_t sampleRB = _channelMute[3] ? 0 : dacToSample(_dacValue[3]);

    // Check if left channels are at midpoint (silence) - use RightB for both in that case
    bool leftSilent = (_dacValue[0] == 0x80) && (_dacValue[1] == 0x80);
    bool rightASilent = (_dacValue[2] == 0x80);

    if (leftSilent && rightASilent)
    {
        // Mono COVOX: only RightB has audio, output to both channels
        left = sampleRB;
        right = sampleRB;
    }
    else
    {
        // Stereo SOUNDRIVE: Left = LeftA + LeftB, Right = RightA + RightB
        int32_t leftMix = sampleLA + sampleLB;
        int32_t rightMix = sampleRA + sampleRB;
        left = static_cast<int16_t>(std::clamp(leftMix, -32768, 32767));
        right = static_cast<int16_t>(std::clamp(rightMix, -32768, 32767));
    }
}

void Covox::renderFromSample(size_t startSample)
{
    if (startSample >= SAMPLES_PER_FRAME)
        return;

    int16_t left, right;
    computeMixedOutput(left, right);

    for (size_t i = startSample; i < SAMPLES_PER_FRAME; i++)
    {
        _buffer[i * 2]     = left;
        _buffer[i * 2 + 1] = right;
    }
}

int16_t Covox::dacToSample(uint8_t value) const
{
    // Unsigned 8-bit DAC, centered: 0x80 = 0, 0x00 = -32768, 0xFF = +32512
    return static_cast<int16_t>((static_cast<int>(value) - 128) * 256);
}
