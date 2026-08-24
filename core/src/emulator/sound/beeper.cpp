#include "beeper.h"

#include "3rdparty/blip_buf/blip_buf.h"
#include "emulator/emulatorcontext.h"
#include "emulator/sound/soundmanager.h"

/// region <Constructors / destructors>

Beeper::Beeper(EmulatorContext* context, size_t clockRate, size_t samplingRate, int16_t* outputBuffer)
    : _context(context)
    , _outputBuffer(outputBuffer)
    , _clockRate(clockRate)
    , _samplingRate(samplingRate)
{
    // Allocate blip_buf accumulators for left and right channels.
    // Size must accommodate the largest possible frame (long-frame machines
    // like Pentagon produce >SAMPLES_PER_FRAME samples; speed multipliers
    // scale the frame duration further) plus a small margin
    // for rounding at frame boundaries.
    _blipL = blip_new(MAX_SAMPLES_PER_FRAME + 64);
    _blipR = blip_new(MAX_SAMPLES_PER_FRAME + 64);

    // Set input clock rate → output sample rate conversion.
    // blip_buf will internally compute the fractional ratio and use
    // it to place sinc kernels at sub-sample precision.
    blip_set_rates(_blipL, static_cast<double>(_clockRate), static_cast<double>(_samplingRate));
    blip_set_rates(_blipR, static_cast<double>(_clockRate), static_cast<double>(_samplingRate));
}

void Beeper::setSampleRate(size_t samplingRate)
{
    _samplingRate = samplingRate;
    blip_set_rates(_blipL, static_cast<double>(_clockRate), static_cast<double>(_samplingRate));
    blip_set_rates(_blipR, static_cast<double>(_clockRate), static_cast<double>(_samplingRate));
    blip_clear(_blipL);
    blip_clear(_blipR);
}

void Beeper::setClockRate(size_t clockRate)
{
    _clockRate = clockRate;
    blip_set_rates(_blipL, static_cast<double>(_clockRate), static_cast<double>(_samplingRate));
    blip_set_rates(_blipR, static_cast<double>(_clockRate), static_cast<double>(_samplingRate));
    blip_clear(_blipL);
    blip_clear(_blipR);
}

Beeper::~Beeper()
{
    blip_delete(_blipL);
    blip_delete(_blipR);
    _blipL = nullptr;
    _blipR = nullptr;
}

/// endregion </Constructors / destructors>

/// region <Methods>

void Beeper::reset()
{
    _portFEState = 0xFF;  // Force initial state change on first port #FE write
    _lastPortFEAmplitude = DAC_LEVEL_00;  // Baseline EAR=0 MIC=0 level
    _lastTapeAmplitude = 0;

    if (_blipL) blip_clear(_blipL);
    if (_blipR) blip_clear(_blipR);
}

void Beeper::handleFrameStart()
{
    // Nothing to do — blip_buf accumulates deltas across the frame.
    // The output buffer will be filled in handleFrameEnd().
}

void Beeper::handlePortOut(uint8_t value, uint32_t frameTState)
{
    // Extract EAR (bit 4) and MIC (bit 3) bits
    uint8_t earMicBits = value & 0b0001'1000;

    // Only process if the output state actually changed
    if (earMicBits == _portFEState)
        return;

    _portFEState = earMicBits;

    // Look up the DAC amplitude for this EAR/MIC combination
    int32_t newAmplitude = dacAmplitude(earMicBits);

    // Compute the delta from the previous Port #FE amplitude
    int32_t delta = newAmplitude - _lastPortFEAmplitude;
    _lastPortFEAmplitude = newAmplitude;

    if (delta != 0)
    {
        // Insert a band-limited step at the exact T-state position.
        // blip_buf will convolve this with a windowed sinc kernel
        // at sub-sample precision, producing alias-free output.
        blip_add_delta(_blipL, frameTState, delta);
        blip_add_delta(_blipR, frameTState, delta);
    }
}

void Beeper::handleTapeAudio(int32_t amplitude, uint32_t frameTState)
{
    // Tape playback feeds input audio through the beeper circuit.
    // Tracked with independent amplitude history to avoid baseline conflicts with Port #FE OUT.
    int32_t delta = amplitude - _lastTapeAmplitude;
    _lastTapeAmplitude = amplitude;

    if (delta != 0)
    {
        blip_add_delta(_blipL, frameTState, delta);
        blip_add_delta(_blipR, frameTState, delta);
    }
}

void Beeper::handleFrameEnd(uint32_t frameDuration)
{
    if (!_blipL || !_blipR || !_outputBuffer || frameDuration == 0)
        return;

    // Close the frame — makes all deltas before frameDuration available
    // as output samples. Also resets the time origin for the next frame.
    blip_end_frame(_blipL, frameDuration);
    blip_end_frame(_blipR, frameDuration);

    int avail = blip_samples_avail(_blipL);
    if (avail > MAX_SAMPLES_PER_FRAME)
        avail = MAX_SAMPLES_PER_FRAME;

    if (avail > 0)
    {
        blip_read_samples(_blipL, &_outputBuffer[0], avail, 1 /* stereo stride */);
        blip_read_samples(_blipR, &_outputBuffer[1], avail, 1 /* stereo stride */);
    }

    _lastSamplesRead = avail;
}

/// endregion </Methods>

/// region <Helper methods>

int32_t Beeper::dacAmplitude(uint8_t earMicBits)
{
    // Map the 4 combinations of EAR (bit 4) and MIC (bit 3)
    // to their respective DAC output levels.
    //
    // Levels derived from ZX Spectrum ULA Issue 2 output voltages:
    //   0.39V, 0.73V, 3.66V, 3.79V (EAR/MIC ratio ≈ 9.6:1)
    switch (earMicBits)
    {
        case 0b0000'0000: return DAC_LEVEL_00;  // EAR=0 MIC=0
        case 0b0000'1000: return DAC_LEVEL_01;  // EAR=0 MIC=1
        case 0b0001'0000: return DAC_LEVEL_10;  // EAR=1 MIC=0
        case 0b0001'1000: return DAC_LEVEL_11;  // EAR=1 MIC=1
        default:          return DAC_LEVEL_00;  // Should not happen
    }
}

/// endregion </Helper methods>
