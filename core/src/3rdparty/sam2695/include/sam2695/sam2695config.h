// libsam2695 - public configuration and the chip's constants.
//
// Every constant here is a property of the Dream SAM2695 taken from its datasheet
// (testdata/midi/docs/SAM2695.pdf, "June 22th 2015") or the Dream application note
// AN_2695 (SAM2195 -> SAM2695 migration). Page references are in README.md.
#pragma once

#include <cstddef>
#include <cstdint>

namespace sam2695
{

// Synthesis rate. AN_2695 "Specifications comparison chart": nominal sampling rate 37.5 kHz
// (9.6 MHz / 256). It agrees with the equalizer's top corner, 18.75 kHz = 37.5 kHz / 2 (datasheet p.15).
constexpr uint32_t kInternalRate = 37500;

// Serial MIDI IN (datasheet p.2 pin 16, MIDI 1.0): 31 250 baud, 8N1.
constexpr uint32_t kMidiBaud = 31250;
// The receiver samples the line with a 16x clock (a standard UART; the datasheet does not describe
// the receiver, see README "UART timing").
constexpr uint32_t kUartOversample = 16;

// Polyphony (datasheet p.1, §5 p.34-35): 64 voices without effects.
constexpr uint32_t kMaxPolyphony = 64;
// NRPN 375Fh power-up value: the table on p.35 marks 3Bh "Default" (reverb, chorus, spatial,
// 4-band EQ: 38 voices) and 45h "Reset All" restores it.
constexpr uint8_t kPowerUpEffectsWord = 0x3B;
constexpr uint8_t kResetAllEffectsWord = 0x45;

// Reset: "It takes around 50 ms before a MIDI IN or MPU message can be processed" (p.9), and the
// NRPN 375Fh = 45h reset stops the firmware for about 50 ms (p.35).
constexpr uint32_t kResetBusyMs = 50;

// Control rate: envelopes, LFOs, filter coefficients and modulators are updated on a fixed grid of
// this many internal samples (0.85 ms). The grid is absolute, so output never depends on how the
// host slices Run() calls.
constexpr uint32_t kControlBlock = 32;

// Voice slots beyond the polyphony limit, used only for the short fade of a stolen or exclusive-class
// voice (it no longer counts against the chip's polyphony).
constexpr uint32_t kFadeSlots = 16;
constexpr uint32_t kVoiceSlots = kMaxPolyphony + kFadeSlots;

// Bounded buffers (no allocation after Configure).
constexpr size_t kSysExCapacity = 128;     // longest chart message is 28 bytes (voice reserve)
constexpr uint32_t kMaxVoiceModulators = 64;

enum class Interpolation : uint8_t
{
    Linear,
    Cubic, // 4-point Hermite (Catmull-Rom)
    Sinc   // 8-tap Kaiser-windowed sinc (default)
};

struct SynthConfig
{
    uint64_t hostTickRate = 3500000;  // the time axis of every timestamped call
    uint32_t outputRate = 44100;      // Render() rate; 8000 .. 192000
    uint32_t polyphony = 0;           // 0 = the chip's own accounting (NRPN 375Fh); else a fixed limit 1..64
    Interpolation interpolation = Interpolation::Sinc;
    // Render mode. true: the chip's whole output path (reverb, chorus, spatial effect, equalizer, soft /
    // hard clipping). false: the dry mode - the voice mix with every gain (GM / master volume, codec)
    // but no effect and no clipping, a linear signal for analysis. The polyphony accounting of NRPN
    // 375Fh is the chip's in both modes.
    bool effects = true;
    // true: a reverb, chorus, spatial effect or equalizer whose whole state is +0.0 skips the blocks with
    // no input - output and state bit-identical to processing them (README "Idle effects"). false
    // processes every block (tests, A/B); the effects' tails end at the tail floor either way.
    bool skipIdleEffects = true;
    bool resetDelay = true;           // ignore MIDI for 50 ms after a reset, as the chip does
    uint32_t eventCapacity = 4096;    // queued MIDI bytes not yet rendered
    uint32_t streamFrames = 37500;    // internal-rate frames kept for Render() (1 s)
    float outputGain = 0.25f;         // the mixer's headroom: -12 dB for the voice sum, before clipping
};

// The chip's polyphony for an NRPN 375Fh value (datasheet §5, p.34-35).
uint32_t PolyphonyForEffectsWord(uint8_t word);

} // namespace sam2695
