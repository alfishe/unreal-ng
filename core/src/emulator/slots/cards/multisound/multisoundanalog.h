#pragma once

// ZX-MultiSound analog board: the component values of the mixer and the output filters (rev.A2 schematic, identical
// in rev.A1), the weights derived from them, and the filter used to model the RC networks.
//
// Source: pcb/rev.A2/zx-multisound.kicad_sch at github.com/UzixLS/zx-multisound commit d7f3ac2; the netlist and its
// reading are summarized in docs/inprogress/2026-10-03-zx-multisound/hardware-reference.md §4.5. Designators are
// rev.A2's.
//
// The output stage is two inverting summing amplifiers (U1, LM358) with 10k feedback. A source of voltage V into an
// input resistor Rin adds -V x Rf / Rin to its side. Every source is inverted the same way, so their relative polarity
// is kept and the overall inversion is not audible: it is not modeled (all weights are positive).

#include <cstdint>

enum class MultiSoundRenderMode : uint8_t
{
    HiFi = 0,       ///< ideal: weights only, no board low-pass filters
    Authentic = 1   ///< adds the board's RC low-pass networks (DAC 1-pole ~16.3 kHz, SAA 2-pole ~7.0 kHz)
};

namespace MultiSoundBoard
{
// Output stage
inline constexpr double kFeedbackOhms = 10e3;           // R25 (L), R15 (R)

// Mixer inputs
inline constexpr double kFmInputOhms = 10e3;            // R18 / R24 (FM1 L / R), R20 / R21 (FM2 L / R)
inline constexpr double kSsgSideInputOhms = 24e3;       // R13, R14 (SSG A of both chips -> L); R28, R29 (SSG C -> R)
inline constexpr double kSsgCenterInputOhms = 47e3;     // R16, R17 (SSG B -> L); R27, R26 (SSG B -> R)
inline constexpr double kSaaInputOhms = 10e3;           // R4 (L), R32 (R)
inline constexpr double kMidiInputOhms = 10e3;          // R2 (L), R33 (R)
inline constexpr double kDacInputOhms = 47e3;           // R6, R12 (DAC 0, 1 -> L); R30, R31 (DAC 2, 3 -> R)
inline constexpr double kExternalInputOhms = 24e3;      // R1 (J3 IN_L), R34 (J3 IN_R)
inline constexpr double kCouplingFarads = 10e-6;        // C10, C2, C1, C3 ...: every source except J3 is AC-coupled

// DAC output network: CPLD pin -> U14 74HCT245 (+5VA_1) -> 1k -> node with 10n to ground -> 47k into the summing node
inline constexpr double kDacSwingVolts = 5.0;           // U14 output swing (0 V / +5 V)
inline constexpr double kDacSeriesOhms = 1e3;           // R50, R45, R44, R43
inline constexpr double kDacShuntFarads = 10e-9;        // C47, C32, C31, C30

// SAA1099 output network (per side): current output with a 1k pull-up (Thevenin source I x 1k), 10n to ground,
// 1k, 10n to ground, then the 10k mixer input
inline constexpr double kSaaSourceOhms = 1e3;           // R46 (L), R47 (R)
inline constexpr double kSaaFirstShuntFarads = 10e-9;   // C43, C44
inline constexpr double kSaaSeriesOhms = 1e3;           // R48, R49
inline constexpr double kSaaSecondShuntFarads = 10e-9;  // C45, C46

// Weights, from the source's node voltage to the output (gain = Rf / Rin)
inline constexpr double kWeightFm = kFeedbackOhms / kFmInputOhms;                      // 1.000, both sides
inline constexpr double kWeightSsgSide = kFeedbackOhms / kSsgSideInputOhms;            // 0.417: A -> L, C -> R
inline constexpr double kWeightSsgCenter = kFeedbackOhms / kSsgCenterInputOhms;        // 0.213: B -> L and R
inline constexpr double kWeightMidi = kFeedbackOhms / kMidiInputOhms;                  // 1.000: L -> L, R -> R
inline constexpr double kWeightExternal = kFeedbackOhms / kExternalInputOhms;          // 0.417: L -> L, R -> R
/// DAC pin voltage to output: the 1k series resistor is part of the input path (10k / 48k = 0.208)
inline constexpr double kWeightDac = kFeedbackOhms / (kDacSeriesOhms + kDacInputOhms);
/// SAA Thevenin voltage (I x 1k) to output in the pass band (DC): 10k / (1k + 1k + 10k) = 0.833. The hardware
/// reference's 0.825 is the same network at 1 kHz, where the 2-pole low-pass already takes 0.09 dB
inline constexpr double kWeightSaa = kFeedbackOhms / (kSaaSourceOhms + kSaaSeriesOhms + kSaaInputOhms);

/// -3 dB corner of a DAC channel's RC: 1 / (2 pi (1k || 47k) 10n) = 16.25 kHz
double DacCornerHz();

/// SAA output ladder as an analog prototype normalized to unit DC gain: H(s) = b(s) / a(s), coefficients of s^0..s^2
void SaaLadder(double (&b)[3], double (&a)[3]);

/// -3 dB corner of the SAA ladder (7.02 kHz; -10.3 dB at 20 kHz)
double SaaCornerHz();

/// High-pass corner of a coupling capacitor into an input resistance: 1 / (2 pi R 10u)
double CouplingCornerHz(double inputOhms);
} // namespace MultiSoundBoard

/// One RC network as a digital biquad: the analog prototype H(s) = (b0 + b1 s + b2 s^2) / (a0 + a1 s + a2 s^2) through
/// the bilinear transform, prewarped so the response at prewarpHz is the analog one exactly (the corner stays where the
/// components put it). A prewarp frequency above 0.45 x the sample rate is clamped there (the network then acts
/// slightly earlier than on the board, e.g. the 16.3 kHz DAC corner at a 32 kHz output rate). Double precision.
class MultiSoundRcFilter
{
public:
    MultiSoundRcFilter() = default;

    void Design(const double (&b)[3], const double (&a)[3], double prewarpHz, double sampleRate);

    static MultiSoundRcFilter LowPass1(double cornerHz, double sampleRate);
    static MultiSoundRcFilter HighPass1(double cornerHz, double sampleRate);

    double Process(double x)
    {
        const double y = _b0 * x + _b1 * _x1 + _b2 * _x2 - _a1 * _y1 - _a2 * _y2;
        _x2 = _x1;
        _x1 = x;
        _y2 = _y1;
        _y1 = y;
        return y;
    }

    /// Clears the history (the network discharged)
    void Reset() { _x1 = _x2 = _y1 = _y2 = 0.0; }

    /// Magnitude of the digital response at hz (tests, Describe)
    double Magnitude(double hz, double sampleRate) const;

private:
    double _b0 = 1.0, _b1 = 0.0, _b2 = 0.0, _a1 = 0.0, _a2 = 0.0;
    double _x1 = 0.0, _x2 = 0.0, _y1 = 0.0, _y2 = 0.0;
};
