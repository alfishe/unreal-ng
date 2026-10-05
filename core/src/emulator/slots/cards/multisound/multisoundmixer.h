#pragma once

// ZX-MultiSound mixer: the board's weights (hardware-reference.md §4.5, the rev.A2 schematic) applied to every source,
// giving the seven stereo rows the card registers with SoundManager (architecture.md §5): MS SSG 1, MS SSG 2 (the two
// YM2203 SSG parts), MS FM 1, MS FM 2 (their FM parts), MS SAA, MS PCM (the four DACs: General Sound + SounDrive) and
// MS MIDI - the YM2203 pair per chip, as the TurboSound FM in the AY socket shows it. A row played at unity volume is
// what the board puts on its jack for that source.
//
// Units. The rows are int16 sample units (row value x INT16_MAX). Each source arrives in its module's own convention
// and is turned into a level by a calibration constant (kFmFullScale and the others below, each with its source), then
// multiplied by its board weight:
//
//   source        input                                          calibration          weight L / R
//   FM 1, FM 2    mono, DAC word / 32768 (Ym2203 decimator)       FmFullScale(trim)    1.000 / 1.000 (centre)
//   SSG A         mono per chip, YM2149 table level 0..1          kSsgChannelFullScale 0.417 / 0
//   SSG B         mono per chip, same                             kSsgChannelFullScale 0.213 / 0.213 (centre)
//   SSG C         mono per chip, same                             kSsgChannelFullScale 0     / 0.417
//   SAA L / R     stereo, Saa1099::EndFrame units                 kSaaUnit             0.833 / 0.833 (own side)
//   MIDI L / R    stereo, sam2695::Synth::Render units (+-1.0)    kMidiFullScale       1.000 / 1.000 (own side)
//   DAC L / R     stereo, MultiSoundDacs::EndFrame units          kDacUnit             0.208 / 0.208 (own side)
//   External      stereo, volts at J3 (no emulated source yet)    kLevelPerVolt        0.417 / 0.417 (own side)
//
// The SSG weights put A left, B in the centre at half weight, C right: the order the emulator calls ABC
// (AYStereoMode::ABC; hardware-reference.md §4.5 calls it "ACB with B in the centre").
//
// Board effects:
// - Inversion: the summing amplifiers invert every source alike, so the relative polarity is kept; not modeled.
// - AC coupling (option acCoupling, default on): every source except the external input passes a 10 uF capacitor into
//   its input resistor, a high-pass at 0.3-3.2 Hz. It removes the DC of the unipolar SSG and SAA outputs (as on the
//   board); below hearing otherwise.
// - Authentic mode: the SAA's 2-pole ladder (7.02 kHz) on the SAA row. The DAC's 1-pole RC is MultiSoundDacs'
//   Authentic mode (the DACs own their channels' timing); the YM3014B hold-cap corner is unknown and not modeled.
// The filter state is render-layer state: not in TTD, cleared by Reset.

#include <cstddef>
#include <cstdint>

#include "emulator/slots/cards/multisound/multisoundanalog.h"

struct MultiSoundMixerConfig
{
    uint32_t outputRate = 44100;
    MultiSoundRenderMode renderMode = MultiSoundRenderMode::HiFi;
    bool acCoupling = true;
};

/// FM loudness trim in dB over the YM2203 FM baseline, the TurboSound FM's [SOUND] TSFM_FmTrimDb: the same value, shown
/// and applied the same way on both boards. 7.4 dB is the real TSFM board's measurement (one FM carrier at TL 0 sits
/// 0.4-1.2 dB above one SSG channel at volume 15; shipped in every config)
inline constexpr double kMultiSoundDefaultFmTrimDb = 7.4;

struct MultiSoundMixerTrim
{
    double fmDb = kMultiSoundDefaultFmTrimDb;
};

/// One block of every source at the output rate; a null pointer is a silent source
struct MultiSoundMixerInput
{
    size_t frames = 0;
    const float* fm[2] = {};            ///< mono per chip
    const float* ssg[2][3] = {};        ///< mono per chip and channel A, B, C
    const int16_t* saa = nullptr;       ///< interleaved L / R
    const float* midi = nullptr;        ///< interleaved L / R
    const int16_t* dac = nullptr;       ///< interleaved L / R
    const float* external = nullptr;    ///< interleaved L / R
};

/// The rows, interleaved L / R, `frames` each; a null pointer skips the row
struct MultiSoundMixerOutput
{
    int16_t* fm[2] = {};                ///< per chip, centred
    int16_t* ssg[2] = {};               ///< per chip: A left, B centre, C right
    int16_t* saa = nullptr;
    int16_t* dac = nullptr;             ///< the PCM row
    int16_t* midi = nullptr;
    int16_t* external = nullptr;        ///< no row yet: the board's line input has no emulated source
};

class MultiSoundMixer
{
public:
    // Calibration: module units to a level (1.0 = INT16_MAX), before the board weight.
    //
    // FM: the TurboSound FM module's level, which the real TSFM board's measurement set (one carrier at TL 0 sits
    // 0.8 dB above one SSG channel at volume 15): kFmBaseGain 0.30 x 10^(TSFM_FmTrimDb / 20), the trim 7.4 dB by
    // default (docs/inprogress/2026-09-10-turbosound-fm/ISSUES.md #1, tsfm-tdd.md §7.1). The MultiSound's FM path has
    // weight 1.0, so an MS FM row equals the TSFM's FM row of the same chip at the same trim. The trim is live
    // (SetFmTrimDb, the audio settings' FM trim); the volt-based sources below are placed against the DEFAULT level -
    // the board's own balance, which the user's FM trim does not move
    static constexpr double kFmBaseGain = 0.30;          // SoundChip_TurboSoundFM::kFmBaseGain
    static constexpr double kFmFullScale = 0.70327;      // at the default trim: 0.30 x 10^(7.4 / 20) = 0.703269
    static double FmFullScale(double trimDb);
    // SSG: the emulator's SSG channel at full volume is 0..0.30 (soundchip_ay8910: table level x pan 0.9 / 3), the
    // level the TSFM measurement above was taken against. The TSFM board gives FM and SSG A equal weights, so 0.30 is
    // the SSG's chip-level swing in the same units as kFmFullScale
    static constexpr double kSsgChannelFullScale = 0.30;
    // Volts: the YM3014B swings +-Vdd / 4 = +-1.25 V at full scale (datasheet formula; TSFM hardware-reference.md §5.2),
    // which is kFmFullScale. Sources with a known swing (the DACs, the line input) go through this
    static constexpr double kFmFullScaleVolts = 1.25;
    static constexpr double kLevelPerVolt = kFmFullScale / kFmFullScaleVolts;
    // DAC: one channel at the transfer's full scale moves its pin by half the 5 V swing (mean 0.5 + 0.5 x 1)
    static constexpr int32_t kDacChannelFullScale = 128 * 64;     // MultiSoundDacs::kChannelFullScale
    static constexpr double kDacUnit = (MultiSoundBoard::kDacSwingVolts / 2.0) * kLevelPerVolt / kDacChannelFullScale;
    // SAA and MIDI: the absolute output levels of the SAA1099 (Iref 10k) and the SAM2695 are not in the schematic or
    // the datasheets we have (hardware-reference.md §7): the modules' own full-scale conventions are taken as the
    // Thevenin level (SAA: one voice at full = 4800 units = 0.146) and the pin level (MIDI: +-1.0). Unmeasured
    static constexpr double kSaaUnit = 1.0 / 32767.0;
    static constexpr double kMidiFullScale = 1.0;

    MultiSoundMixer() = default;

    void Configure(const MultiSoundMixerConfig& cfg);
    const MultiSoundMixerConfig& Config() const { return _cfg; }

    /// Clears the filter history (the coupling capacitors discharged)
    void Reset();

    void SetOutputRate(uint32_t rate);
    void SetRenderMode(MultiSoundRenderMode mode);

    /// FM trim in dB (the TSFM's TSFM_FmTrimDb), applied from the next block
    void SetFmTrimDb(double db);
    double FmTrimDb() const { return _trim.fmDb; }

    /// Mixes one block: every row of `out` gets `in.frames` stereo frames
    void Mix(const MultiSoundMixerInput& in, const MultiSoundMixerOutput& out);

private:
    void DesignFilters();

    MultiSoundMixerConfig _cfg;
    MultiSoundMixerTrim _trim;
    double _fmLevel = kFmFullScale;             // FmFullScale(_trim.fmDb)

    // Coupling high-passes, one per capacitor (per chip: the rows are per chip)
    MultiSoundRcFilter _couplingFm[2];
    MultiSoundRcFilter _couplingSsgSide[2][2];  // per chip: A (L), C (R)
    MultiSoundRcFilter _couplingSsgCenter[2];   // per chip: B
    MultiSoundRcFilter _couplingSaa[2];
    MultiSoundRcFilter _couplingMidi[2];
    MultiSoundRcFilter _couplingDac[2];
    MultiSoundRcFilter _saaLadder[2];
};
