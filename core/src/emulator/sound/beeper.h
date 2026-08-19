#pragma once

#include <cstdint>
#include <cstddef>
#include "emulator/sound/audio.h"
#include "common/modulelogger.h"

// Forward declarations
class EmulatorContext;
struct blip_t;

/// Band-limited 1-bit beeper sound synthesizer using blip_buf.
///
/// Each port #FE write with a changed EAR/MIC state records a delta
/// at the exact T-state position into a pair of blip_buf accumulators
/// (left/right). At frame end, blip_buf produces alias-free 44.1 kHz
/// output via windowed-sinc interpolation — no oversampling needed.
///
/// DAC levels derived from the ZX Spectrum ULA output circuit:
///   Issue 2: EAR=0/MIC=0 → 0.39V, EAR=0/MIC=1 → 0.73V,
///            EAR=1/MIC=0 → 3.66V, EAR=1/MIC=1 → 3.79V
///   Issue 3: EAR=0/MIC=0 → 0.34V, EAR=0/MIC=1 → 0.66V,
///            EAR=1/MIC=0 → 3.56V, EAR=1/MIC=1 → 3.70V
class Beeper
{
    /// region <ModuleLogger definitions for Module/Submodule>
public:
    const PlatformModulesEnum _MODULE = PlatformModulesEnum::MODULE_SOUND;
    const uint16_t _SUBMODULE = PlatformSoundSubmodulesEnum::SUBMODULE_SOUND_BEEPER;
    /// endregion </ModuleLogger definitions for Module/Submodule>

    /// region <Constants>
public:
    // DAC output levels (int32 amplitude units for blip_buf deltas).
    // Derived from ZX Spectrum ULA Issue 2 output voltages:
    //   EAR=0 MIC=0 → 0.39V,  EAR=0 MIC=1 → 0.73V
    //   EAR=1 MIC=0 → 3.66V,  EAR=1 MIC=1 → 3.79V
    //
    // Full voltage range (3.79 - 0.39 = 3.40V) mapped to 16000 units (-8000..+8000).
    // EAR swing ≈ 96% of full range, MIC swing ≈ 10% — ratio 9.6:1.
    static constexpr int32_t DAC_LEVEL_00 = -8000;  // EAR=0 MIC=0  (0.39V)
    static constexpr int32_t DAC_LEVEL_01 = -6400;  // EAR=0 MIC=1  (0.73V, MIC delta = +1600)
    static constexpr int32_t DAC_LEVEL_10 = +7400;  // EAR=1 MIC=0  (3.66V, EAR delta = +15400)
    static constexpr int32_t DAC_LEVEL_11 = +8000;  // EAR=1 MIC=1  (3.79V, MIC delta = +600)
    /// endregion </Constants>

    /// region <Fields>
protected:
    EmulatorContext* _context;

    // blip_buf accumulators (one per stereo channel)
    blip_t* _blipL = nullptr;
    blip_t* _blipR = nullptr;

    // Last amplitudes written to blip_buf (tracked independently for Port #FE OUT vs Tape IN)
    int32_t _lastPortFEAmplitude = DAC_LEVEL_00;
    int32_t _lastTapeAmplitude = 0;

    // Previous port #FE state (bits 3-4: MIC/EAR)
    uint8_t _portFEState = 0;

    // Output buffer — points into SoundManager's beeper AudioFrameDescriptor
    int16_t* _outputBuffer = nullptr;
    int _lastSamplesRead = 0;  // Samples delivered on last handleFrameEnd

    // Clock rate and sample rate (passed at construction)
    size_t _clockRate;
    size_t _samplingRate;
    /// endregion </Fields>

    /// region Constructors / destructors>
public:
    Beeper() = delete;
    Beeper(EmulatorContext* context, size_t clockRate, size_t samplingRate, int16_t* outputBuffer);
    virtual ~Beeper();
    /// endregion </Constructors / destructors>

    /// region <Methods>
public:
    void reset();

    /// Live core-rate change (device reroute with CoreRate=auto): re-point
    /// the blip_buf resampler at the new output rate and clear pending deltas
    void setSampleRate(size_t samplingRate);

    /// Called at the start of each video frame.
    void handleFrameStart();

    /// Called on each OUT to port #FE. Records a band-limited delta
    /// at the exact T-state position.
    /// @param value  Full port #FE byte (bits 3-4 = MIC/EAR)
    /// @param frameTState  T-state counter within the current frame
    void handlePortOut(uint8_t value, uint32_t frameTState);

    /// Called when tape playback sends audio through the beeper circuit.
    /// Accepts a raw amplitude (already filtered by tape) and inserts
    /// a delta at the given T-state position.
    /// @param amplitude  Signed amplitude value
    /// @param frameTState  T-state counter within the current frame
    void handleTapeAudio(int32_t amplitude, uint32_t frameTState);

    /// Called at the end of each video frame. Finalizes the blip_buf
    /// accumulator and reads out SAMPLES_PER_FRAME band-limited samples
    /// into the output buffer.
    /// @param frameDuration  Total T-states in this frame (config.frame * speed_multiplier)
    void handleFrameEnd(uint32_t frameDuration);

    /// Number of samples blip_buf delivered on the last handleFrameEnd -
    /// cross-checked by SoundManager against its exact sample accumulator
    int getLastSamplesRead() const { return _lastSamplesRead; }
    /// endregion</Methods>

    /// region <Helper methods>
protected:
    /// Look up DAC amplitude from EAR and MIC bits.
    /// @param earMicBits  Bits 3-4 of port #FE value (masked)
    static int32_t dacAmplitude(uint8_t earMicBits);
    /// endregion </Helper methods>
};
