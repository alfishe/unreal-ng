#include "covox.h"

#include <algorithm>
#include <cmath>

#include "3rdparty/blip_buf/blip_buf.h"
#include "emulator/emulatorcontext.h"
#include "emulator/cpu/z80.h"
#include "emulator/cpu/core.h"

/// region <Constructors / destructors>

Covox::Covox(EmulatorContext* context)
    : _context(context)
{
    // Allocate blip_buf accumulators for stereo output
    _blipL = blip_new(MAX_SAMPLES_PER_FRAME + 64);
    _blipR = blip_new(MAX_SAMPLES_PER_FRAME + 64);

    // Set input clock rate → output sample rate conversion
    blip_set_rates(_blipL, static_cast<double>(CPU_CLOCK_RATE), static_cast<double>(AUDIO_SAMPLING_RATE));
    blip_set_rates(_blipR, static_cast<double>(CPU_CLOCK_RATE), static_cast<double>(AUDIO_SAMPLING_RATE));

    reset();
}

Covox::~Covox()
{
    blip_delete(_blipL);
    blip_delete(_blipR);
    _blipL = nullptr;
    _blipR = nullptr;
}

/// endregion </Constructors / destructors>

/// region <Frame lifecycle>

void Covox::reset()
{
    for (int i = 0; i < 4; i++)
        _dacValue[i] = 0x80;  // Midpoint = silence

    _lastL = 0;
    _lastR = 0;
    _dcAccumL = _dcAccumR = 0.0f;

    if (_blipL) blip_clear(_blipL);
    if (_blipR) blip_clear(_blipR);

    memset(_buffer, 0, _audioDescriptor.memoryBufferSizeInBytes);
}

void Covox::handleFrameStart()
{
    // Nothing to do — blip_buf accumulates deltas across the frame.
    // The buffer will be filled in handleFrameEnd().
}

void Covox::handleFrameEnd(size_t expectedSamples)
{
    CONFIG& config = _context->config;
    uint8_t speedMultiplier = _context->emulatorState.current_z80_frequency_multiplier;
    uint32_t frameDuration = config.frame * speedMultiplier;

    if (frameDuration == 0)
        return;

    // Close the frame — convert accumulated deltas to output samples
    blip_end_frame(_blipL, frameDuration);
    blip_end_frame(_blipR, frameDuration);

    // Actual samples for this frame - must match what SoundManager mixes.
    // Preferred: the exact count from SoundManager's sample accumulator
    // (alternates e.g. 903/904 on Pentagon). Fallback: local rounding.
    int samplesThisFrame;
    if (expectedSamples > 0)
    {
        samplesThisFrame = static_cast<int>(expectedSamples);
    }
    else
    {
        samplesThisFrame = static_cast<int>(
            std::round(frameDuration * (double)AUDIO_SAMPLING_RATE / (double)CPU_CLOCK_RATE));
    }
    samplesThisFrame = std::clamp(samplesThisFrame, 0, (int)MAX_SAMPLES_PER_FRAME);

    // Read out band-limited samples into the interleaved stereo buffer
    int samplesL = blip_read_samples(_blipL, &_buffer[0], samplesThisFrame, 1 /* stereo stride */);
    int samplesR = blip_read_samples(_blipR, &_buffer[1], samplesThisFrame, 1 /* stereo stride */);

    // Zero-fill any shortfall (defensive)
    for (int i = samplesL; i < samplesThisFrame; i++)
        _buffer[i * 2] = 0;
    for (int i = samplesR; i < samplesThisFrame; i++)
        _buffer[i * 2 + 1] = 0;

    // Optional DC offset removal (high-pass filter)
    if (_dcRemovalEnabled)
    {
        for (int i = 0; i < samplesThisFrame; i++)
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

/// endregion </Frame lifecycle>

/// region <Port interface>

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

    uint32_t currentTState = (_context && _context->pCore && _context->pCore->GetZ80())
                             ? _context->pCore->GetZ80()->t
                             : 0;

    // Update the DAC value for this channel
    Channel ch = portToChannel(port);
    _dacValue[static_cast<int>(ch)] = value;

    // Compute new stereo amplitudes from all 4 channels
    int32_t newL, newR;
    computeStereoAmplitudes(newL, newR);

    // Compute deltas from previous state
    int32_t deltaL = newL - _lastL;
    int32_t deltaR = newR - _lastR;

    // Insert band-limited steps at the exact T-state position
    if (deltaL != 0)
        blip_add_delta(_blipL, currentTState, deltaL);
    if (deltaR != 0)
        blip_add_delta(_blipR, currentTState, deltaR);

    // Update tracked state
    _lastL = newL;
    _lastR = newR;
}

/// endregion </Port interface>

/// region <Helper methods>

void Covox::computeStereoAmplitudes(int32_t& outL, int32_t& outR) const
{
    // Always use stereo mixing formula — no mono heuristic.
    // Hardware sums two channels per side through resistors.
    //
    // For mono COVOX (only RightB written), LeftA/LeftB/RightA stay at 0x80
    // (midpoint), so their contribution is zero and the mix naturally produces
    // RightB on both sides when using the default mono routing.
    //
    // Each channel: (value - 128) gives signed range [-128, +127].
    // Sum of two channels: [-256, +254].
    // Multiply by 128 (half of 256): gives [-32768, +32512] — fits int16
    // with no clipping when both channels are at full amplitude.
    int32_t la = _channelMute[0] ? 0 : (static_cast<int32_t>(_dacValue[0]) - 128);
    int32_t lb = _channelMute[1] ? 0 : (static_cast<int32_t>(_dacValue[1]) - 128);
    int32_t ra = _channelMute[2] ? 0 : (static_cast<int32_t>(_dacValue[2]) - 128);
    int32_t rb = _channelMute[3] ? 0 : (static_cast<int32_t>(_dacValue[3]) - 128);

    outL = (la + lb) * 128;
    outR = (ra + rb) * 128;
}

/// endregion </Helper methods>
