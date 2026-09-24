#include "covox.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "3rdparty/blip_buf/blip_buf.h"
#include "3rdparty/message-center/messagecenter.h"
#include "emulator/emulatorcontext.h"
#include "emulator/cpu/z80.h"
#include "emulator/cpu/core.h"
#include "emulator/notifications.h"
#include "emulator/platform.h"

/// region <Constructors / destructors>

Covox::Covox(EmulatorContext* context, size_t sampleRate)
    : _context(context)
    , _sampleRate(sampleRate)
{
    // Allocate blip_buf accumulators for stereo output
    _blipL = blip_new(MAX_SAMPLES_PER_FRAME + 64);
    _blipR = blip_new(MAX_SAMPLES_PER_FRAME + 64);

    // Set input clock rate → output sample rate conversion
    blip_set_rates(_blipL, static_cast<double>(_clockRate), static_cast<double>(_sampleRate));
    blip_set_rates(_blipR, static_cast<double>(_clockRate), static_cast<double>(_sampleRate));

    // Keep the DC blocker cutoff in Hz constant across core rates
    _dcCoefEff = static_cast<float>(std::pow(DC_COEF, 44100.0 / static_cast<double>(_sampleRate)));

    reset();
}

void Covox::setSampleRate(size_t sampleRate)
{
    _sampleRate = sampleRate;
    blip_set_rates(_blipL, static_cast<double>(_clockRate), static_cast<double>(_sampleRate));
    blip_set_rates(_blipR, static_cast<double>(_clockRate), static_cast<double>(_sampleRate));
    if (_blipL) blip_clear(_blipL);
    if (_blipR) blip_clear(_blipR);

    // Keep the DC blocker cutoff in Hz constant across core rates
    _dcCoefEff = static_cast<float>(std::pow(DC_COEF, 44100.0 / static_cast<double>(_sampleRate)));
}

void Covox::setClockRate(size_t clockRate)
{
    _clockRate = clockRate;
    blip_set_rates(_blipL, static_cast<double>(_clockRate), static_cast<double>(_sampleRate));
    blip_set_rates(_blipR, static_cast<double>(_clockRate), static_cast<double>(_sampleRate));
    if (_blipL) blip_clear(_blipL);
    if (_blipR) blip_clear(_blipR);
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
    {
        _dacValue[i] = 0x80;  // Midpoint = silence
        _writtenThisFrame[i] = false;
        _staleFrameCount[i] = 0;
    }

    _lastL = 0;
    _lastR = 0;
    _dcAccumL = _dcAccumR = 0.0f;

    if (_blipL) blip_clear(_blipL);
    if (_blipR) blip_clear(_blipR);

    memset(_buffer, 0, _audioDescriptor.memoryBufferSizeInBytes);
}

void Covox::handleFrameStart()
{
    // Reset activity tracking for the new frame
    _frameHadActivity = false;

    // Stale-channel decay: a channel that went a whole frame without any
    // write extends its idle streak; once it crosses the threshold, silence
    // it (see the member comment in covox.h for why this is needed)
    for (int i = 0; i < 4; i++)
    {
        if (_writtenThisFrame[i])
        {
            _staleFrameCount[i] = 0;
        }
        else
        {
            _staleFrameCount[i]++;
            if (_staleFrameCount[i] >= STALE_CHANNEL_FRAMES && _dacValue[i] != 0x80)
            {
                decayStaleChannel(static_cast<Channel>(i));
            }
        }
        _writtenThisFrame[i] = false;
    }
}

void Covox::handleFrameEnd(size_t expectedSamples)
{
    CONFIG& config = _context->config;
    uint8_t speedMultiplier = _context->emulatorState.HostSpeedMultiplier();  // hardware turbo excluded (see SoundManager)
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
            std::round(frameDuration * (double)_sampleRate / (double)CPU_CLOCK_RATE));
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

            _dcAccumL = _dcAccumL * _dcCoefEff + l * (1.0f - _dcCoefEff);
            _dcAccumR = _dcAccumR * _dcCoefEff + r * (1.0f - _dcCoefEff);

            _buffer[i * 2]     = static_cast<int16_t>(std::clamp(l - _dcAccumL, -32768.0f, 32767.0f));
            _buffer[i * 2 + 1] = static_cast<int16_t>(std::clamp(r - _dcAccumR, -32768.0f, 32767.0f));
        }
    }

    // Post notification while active (to refresh HUD TTL) or on state change
    if (_frameHadActivity || _frameHadActivity != _wasActive)
    {
        _wasActive = _frameHadActivity;
        MessageCenter::DefaultMessageCenter().Post(
            NC_AUDIO_ACTIVITY, new AudioActivityPayload(_context->emulatorId, AudioSource::Covox, _wasActive));
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
        case 0x0F: return Channel::LeftA;
        case 0x1F: return Channel::LeftB;
        case 0x4F: return Channel::RightA;
        case 0x5F: return Channel::RightB;
        default:   return Channel::RightB;  // Fallback for mono COVOX compatibility
    }
}

void Covox::portDeviceOutMethod(uint16_t port, uint8_t value)
{
    // COVOX/SOUNDRIVE mode-2 mirror ports: #F1, #F3, #F9, #FB (bits[7:4]=1111,
    // bit2=0, bit0=1). Mode-1 primary ports: #0F, #1F, #4F, #5F (bit7=0,
    // bit5=1, bits[3:0]=1111) - the caller (PortDecoder_Pentagon128) only
    // forwards these once TR-DOS has released the aliased Beta128 addresses
    uint8_t lowByte = port & 0xFF;
    bool isMode2 = (lowByte & PORT_MASK) == PORT_MATCH;
    bool isMode1 = (lowByte & PORT_MASK_MODE1) == PORT_MATCH_MODE1;
    if (!isMode2 && !isMode1)
        return;

    uint32_t currentTState = (_context && _context->pCore && _context->pCore->GetZ80())
                             ? _context->emulatorState.AudioTstate(_context->pCore->GetZ80()->t)
                             : 0;

    // Update the DAC value for this channel
    Channel ch = portToChannel(port);
    _dacValue[static_cast<int>(ch)] = value;
    _writtenThisFrame[static_cast<int>(ch)] = true;
    _staleFrameCount[static_cast<int>(ch)] = 0;

    // Compute new stereo amplitudes from all 4 channels
    int32_t newL, newR;
    computeStereoAmplitudes(newL, newR);

    // Compute deltas from previous state
    int32_t deltaL = newL - _lastL;
    int32_t deltaR = newR - _lastR;

    // Insert band-limited steps at the exact T-state position (not while turbo
    // suppresses synthesis - the DAC state above is still tracked)
    if (!_synthesisSuppressed)
    {
        if (deltaL != 0)
        {
            blip_add_delta(_blipL, currentTState, deltaL);
            _frameHadActivity = true;
        }
        if (deltaR != 0)
        {
            blip_add_delta(_blipR, currentTState, deltaR);
            _frameHadActivity = true;
        }
    }

    // Update tracked state
    _lastL = newL;
    _lastR = newR;
}

void Covox::decayStaleChannel(Channel ch)
{
    _dacValue[static_cast<int>(ch)] = 0x80;

    int32_t newL, newR;
    computeStereoAmplitudes(newL, newR);

    int32_t deltaL = newL - _lastL;
    int32_t deltaR = newR - _lastR;

    // T-state 0: called from handleFrameStart, before any real write this
    // frame, so the synthetic step lands at the very start of the frame
    if (!_synthesisSuppressed)
    {
        if (deltaL != 0)
        {
            blip_add_delta(_blipL, 0, deltaL);
            _frameHadActivity = true;
        }
        if (deltaR != 0)
        {
            blip_add_delta(_blipR, 0, deltaR);
            _frameHadActivity = true;
        }
    }

    _lastL = newL;
    _lastR = newR;
}

void Covox::setSynthesisSuppressed(bool suppressed)
{
    if (_synthesisSuppressed == suppressed)
        return;
    _synthesisSuppressed = suppressed;
    if (!suppressed)
    {
        if (_blipL) blip_clear(_blipL);
        if (_blipR) blip_clear(_blipR);
    }
}

/// endregion </Port interface>

/// region <Helper methods>

void Covox::computeStereoAmplitudes(int32_t& outL, int32_t& outR) const
{
    // Each channel: (value - 128) gives signed range [-128, +127].
    // Sum of two channels: [-256, +254].
    // Multiply by 128 (half of 256): gives [-32768, +32512] — fits int16
    // with no clipping when both channels are at full amplitude.
    int32_t la = _channelMute[0] ? 0 : (static_cast<int32_t>(_dacValue[0]) - 128);
    int32_t lb = _channelMute[1] ? 0 : (static_cast<int32_t>(_dacValue[1]) - 128);
    int32_t ra = _channelMute[2] ? 0 : (static_cast<int32_t>(_dacValue[2]) - 128);
    int32_t rb = _channelMute[3] ? 0 : (static_cast<int32_t>(_dacValue[3]) - 128);

    // Mono COVOX compatibility: classic single-DAC covox software only ever
    // drives one of the four channels (traditionally #FB/RightB, but the
    // SoundDrive 1.05 "mode 1" primary set has the same single-DAC digi
    // players wired to #1F/LeftB instead - see balldreams2.sna). Center
    // whichever single channel is active to both speakers so mono playback
    // doesn't get silently panned hard to one side; true SoundDrive stereo
    // (2+ channels active) still sums independently per side below.
    int32_t soleActive = 0;
    int activeChannels = 0;
    if (la != 0) { soleActive = la; activeChannels++; }
    if (lb != 0) { soleActive = lb; activeChannels++; }
    if (ra != 0) { soleActive = ra; activeChannels++; }
    if (rb != 0) { soleActive = rb; activeChannels++; }

    if (activeChannels <= 1)
    {
        // Silence, or exactly one channel driven: center it on both speakers
        outL = soleActive * 128;
        outR = soleActive * 128;
    }
    else
    {
        // Stereo Soundrive: L = LeftA + LeftB, R = RightA + RightB
        outL = (la + lb) * 128;
        outR = (ra + rb) * 128;
    }
}

/// endregion </Helper methods>

/// region <TTDSerializable (P1.5 - parent TDD 6.4)>
//
// Layout: 4 bytes - the four DAC latches (_dacValue[0..3]).
// The DAC latches are the only CPU-visible machine state (set via OUT to
// ports 0xF1/0xF3/0xF9/0xFB, or their #0F/#1F/#4F/#5F mode-1 aliases).
// Everything else is host-side audio pipeline, including the stale-channel
// decay countdown (_staleFrameCount/_writtenThisFrame) - it is a host-only
// mixing heuristic with no hardware equivalent, so a TTD load resets it to
// "just written" for all channels. Worst case: a channel captured mid-decay
// gets up to STALE_CHANNEL_FRAMES extra frames before re-centering after the
// jump - inaudible in practice and never affects _dacValue itself.

static constexpr size_t kCovoxStateSize = 4;
static_assert(kCovoxStateSize == 4, "Covox state size drift");

size_t Covox::TTDStateSize() const
{
    return kCovoxStateSize;
}

void Covox::TTDSaveState(uint8_t* dst) const
{
    std::memcpy(dst, _dacValue, 4);
}

void Covox::TTDLoadState(const uint8_t* src)
{
    std::memcpy(_dacValue, src, 4);
}
/// endregion </TTDSerializable>
