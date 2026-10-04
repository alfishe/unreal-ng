#pragma once

/// @file saa1099.h
/// @brief Philips SAA1099 six-voice stereo sound generator (tdd-saa1099.md).
///
/// Our own model, brought to the consensus of the reference implementations by
/// co-simulation (tools/verification/saa1099/README.md has the consensus table:
/// what SAASound, MAME, the MiSTer RTL, the Philips documentation and the real-chip
/// measurements say per behavior, and which one this module follows and why).
///
/// Worked example - middle A on voice 0, left only, full volume:
///   WriteAddress(t, 0x00); WriteData(t, 0x0F);   amplitude voice 0: left 15, right 0
///   WriteAddress(t, 0x08); WriteData(t, 227);    tone number voice 0
///   WriteAddress(t, 0x10); WriteData(t, 0x03);   octave voice 0 = 3
///   WriteAddress(t, 0x14); WriteData(t, 0x01);   tone enable voice 0
///   WriteAddress(t, 0x1C); WriteData(t, 0x01);   sound enable
/// The tone generator toggles every (511 - 227) << (8 - 3) = 9088 chip clocks, so at
/// 8 MHz the tone is 8 000 000 / (2 * 9088) = 440.1 Hz.
///
/// Time. Every call carries a monotonic timestamp on the host axis (hostTickRate
/// ticks per second, e.g. the emulator's audio T-states). An integer ratio
/// accumulator converts it to chip clocks without drift; its remainder is part of
/// the state. A timestamp that does not move forward advances nothing.
///
/// Output. Each generator event that changes the summed level of a side adds a step
/// into a blip_buf pair on the chip-clock axis (band-limited, like the GS and Covox
/// paths). The level is unipolar: silence is 0, as the chip's current-sink outputs.
///   HiFi       the mean level of each voice's pulse-density pattern (default)
///   Authentic  the pulse-density bit stream itself, one bit per two chip clocks
///              (the 62.5 kHz chopped output before the board filter)
/// Both modes have the same mean level.
///
/// Allocation happens in Configure only (the two blip buffers).

#include <cstddef>
#include <cstdint>

#include "debugger/ttd/ttdserializable.h"

struct blip_t;
class ModuleLogger;

enum class Saa1099RenderMode : uint8_t
{
    HiFi = 0,       ///< band-limited mean level of each voice's PDM pattern
    Authentic = 1   ///< the PDM bit stream (one bit per two chip clocks)
};

struct Saa1099Config
{
    uint32_t hostTickRate = 3500000;  ///< ticks per second of every timestamp passed in
    uint32_t chipClockHz = 8000000;   ///< MultiSound: 32 MHz / 4; SAM Coupe: 8 MHz
    uint32_t outputRate = 44100;      ///< stereo frames per second out of EndFrame
    Saa1099RenderMode renderMode = Saa1099RenderMode::HiFi;
    ModuleLogger* logger = nullptr;   ///< optional (MODULE_SOUND / SUBMODULE_SOUND_SAA)
};

/// Everything automation surfaces show about the chip (Describe is their single source)
struct Saa1099Report
{
    struct Tone
    {
        uint8_t toneRegister = 0;    ///< #08-#0D as written
        uint8_t octaveRegister = 0;  ///< octave nibble as written
        uint8_t toneLatched = 0;     ///< the values the divider runs with (latched at a transition)
        uint8_t octaveLatched = 0;
        uint8_t level = 0;           ///< generator output (0 / 1)
        uint32_t halfPeriod = 0;     ///< chip clocks between transitions, latched values
        uint32_t clocksToTransition = 0;
        double frequencyHz = 0.0;    ///< latched values at the configured chip clock
    };
    struct Noise
    {
        uint8_t source = 0;          ///< 0-2 fixed rate (clock / 256, 512, 1024), 3 = tone generator 0 / 3
        uint32_t lfsr = 0;           ///< 18-bit shift register
        uint8_t output = 0;          ///< LFSR bit 0
        uint32_t clocksToShift = 0;  ///< fixed-rate divider (frozen in source 3)
    };
    struct Envelope
    {
        uint8_t controlRegister = 0; ///< #18 / #19 as last written
        bool enabled = false;        ///< D7 (direct-acting)
        bool resolution3Bit = false; ///< D4 (direct-acting)
        uint8_t shape = 0;           ///< D3-D1 in effect (buffered)
        bool invertRight = false;    ///< D0 in effect (buffered)
        bool externalClock = false;  ///< D5 in effect (buffered)
        uint8_t phase = 0;
        uint8_t position = 0;
        bool ended = false;          ///< a single-shot envelope reached its end (point 3)
        bool pending = false;        ///< a write waits for point 3 / 4
        uint8_t pendingValue = 0;
        uint8_t levelLeft = 0;
        uint8_t levelRight = 0;
    };
    struct Voice
    {
        uint8_t amplitudeLeft = 0;
        uint8_t amplitudeRight = 0;
        bool toneEnabled = false;
        bool noiseEnabled = false;
        bool envelopeShaped = false; ///< voice 2 / 5 with its envelope generator enabled
        uint16_t levelLeft = 0;      ///< mean output, PDM ones per 128 bits (0..120)
        uint16_t levelRight = 0;
    };

    uint8_t registers[32] = {};
    uint8_t addressLatch = 0;
    bool soundEnabled = false;       ///< #1C bit 0
    bool sync = false;               ///< #1C bit 1 (generators reset and held)
    bool clockEnabled = true;
    Saa1099RenderMode renderMode = Saa1099RenderMode::HiFi;
    uint32_t chipClockHz = 0;
    uint32_t hostTickRate = 0;
    uint32_t outputRate = 0;
    uint64_t chipClocks = 0;         ///< clocks the generators have run (the gate stops it)
    uint16_t outputLeft = 0;         ///< summed level, the units of Voice::level (0..720)
    uint16_t outputRight = 0;
    Tone tones[6];
    Noise noise[2];
    Envelope envelopes[2];
    Voice voices[6];
};

class Saa1099 : public ttd::TTDSerializable
{
public:
    /// Output gain: summed level (0..720 per side) to int16 sample units
    static constexpr int32_t kOutputGain = 40;
    /// Highest summed level of one side: six voices at 120
    static constexpr int32_t kMaxLevel = 720;
    /// TTD blob layout version (first byte of the blob)
    static constexpr uint8_t kStateVersion = 1;

    Saa1099() = default;
    ~Saa1099() override;
    Saa1099(const Saa1099&) = delete;
    Saa1099& operator=(const Saa1099&) = delete;

    /// Rates, mode and logger; allocates the output buffers (the only allocation),
    /// then a power-on reset at host time 0
    void Configure(const Saa1099Config& cfg);
    const Saa1099Config& Config() const { return _cfg; }

    /// Power-on state at host time t: registers 0, sound disabled, generators at
    /// their reset phase, noise seeds all ones, pending output dropped. The clock
    /// gate is a board signal and keeps its state
    void Reset(uint64_t t);

    /// Chip clock gate (MultiSound control byte bit 3): stopped, the counters freeze
    /// and the output holds its last level
    void SetClockEnabled(uint64_t t, bool enabled);
    bool ClockEnabled() const { return _s.clockEnabled; }

    /// A0 = 1: register address (5 bits). An address #18 / #19 is also the
    /// external clock strobe of envelope generator 0 / 1
    void WriteAddress(uint64_t t, uint8_t value);
    /// A0 = 0: data for the addressed register
    void WriteData(uint64_t t, uint8_t value);

    /// Advance the generators to host time t
    void Run(uint64_t t);

    /// Run to t, close the output frame and read `frames` stereo frames (interleaved
    /// L / R) into `stereo`. Frames the band-limited buffer cannot supply repeat the
    /// last sample. Returns the number of frames written (clamped to the buffer)
    size_t EndFrame(uint64_t t, int16_t* stereo, size_t frames);

    /// Output rate change at a frame boundary; chip state is kept, pending output dropped
    void SetOutputRate(uint32_t rate);
    /// Render mode change at a frame boundary
    void SetRenderMode(Saa1099RenderMode mode);

    void Describe(Saa1099Report& out) const;

    /// Summed output level of each side right now (0..kMaxLevel)
    int32_t OutputLeft() const { return _s.outLeft; }
    int32_t OutputRight() const { return _s.outRight; }
    uint64_t ChipClocks() const { return _s.chipClock; }

    /// region <TTDSerializable>
    /// Fixed-size blob: version byte, registers, generator counters and levels, LFSRs,
    /// envelope state, the clock-ratio accumulator phase and the time axis. Output
    /// buffers are host-side: a load drops them and restarts the frame with one step
    /// from silence to the restored level
    size_t TTDStateSize() const override;
    void TTDSaveState(uint8_t* dst) const override;
    void TTDLoadState(const uint8_t* src) override;
    std::string TTDDeviceName() const override { return "SAA1099"; }
    ttd::PeripheralId TTDPeripheralId() const override { return ttd::PeripheralId::Saa1099; }
    uint64_t TTDHashState() const override;
    /// endregion </TTDSerializable>

private:
    struct EnvelopeState
    {
        uint8_t control = 0;      // last written control byte
        uint8_t enabled = 0;
        uint8_t res3 = 0;
        uint8_t shape = 0;
        uint8_t invert = 0;
        uint8_t external = 0;
        uint8_t phase = 0;
        uint8_t position = 0;
        uint8_t ended = 1;
        uint8_t pending = 0;
        uint8_t pendingValue = 0;
    };

    struct State
    {
        uint8_t regs[32] = {};
        uint8_t address = 0;
        uint8_t soundEnable = 0;
        uint8_t sync = 0;
        uint8_t clockEnabled = 1;

        uint32_t toneCounter[6] = {};
        uint8_t toneLevel[6] = {};
        uint8_t toneFreq[6] = {};   // latched at the last transition
        uint8_t toneOct[6] = {};

        uint32_t noiseLfsr[2] = {};
        uint32_t noiseCounter[2] = {};

        EnvelopeState env[2];

        uint64_t hostTime = 0;      // last host timestamp reached
        uint64_t ratioRemainder = 0; // accumulator phase, < hostTickRate
        uint64_t chipClock = 0;     // gated: clocks the generators have run
        uint64_t ungatedClock = 0;  // chip-clock axis of the output, runs while gated
        uint64_t frameStart = 0;    // ungatedClock at the start of the output frame

        int32_t outLeft = 0;        // summed level now
        int32_t outRight = 0;
        int32_t emittedLeft = 0;    // level the output buffer holds (sample units)
        int32_t emittedRight = 0;
    };

    void PowerOn();
    void Advance(uint64_t chipClocks);
    void StepGenerators(uint64_t clocks);
    void ToneTransition(int i);
    void NoiseShift(int n);
    void EnvelopeWrite(int e, uint8_t value);
    void EnvelopeApply(int e, uint8_t value);
    void EnvelopeClock(int e);
    void EnvelopeLevels(int e, uint8_t& left, uint8_t& right) const;
    bool EnvelopeShapes(int voice) const;
    void SyncWrite(uint8_t value);
    void RefreshVoices();
    void ComputeOutput(int32_t& left, int32_t& right) const;
    void UpdateOutput();
    void Emit(int32_t left, int32_t right);

    static uint32_t HalfPeriod(uint8_t tone, uint8_t octave);

    Saa1099Config _cfg;
    State _s;

    // Derived per-voice output description (rebuilt by RefreshVoices after any
    // state change; not serialized): the PDM pattern of each side and the
    // gates of the even / odd PDM periods
    uint64_t _voiceMask[6][2] = {};
    uint8_t _gateEven[6] = {};
    uint8_t _gateOdd[6] = {};
    uint16_t _voiceLevel[6][2] = {};

    blip_t* _blipLeft = nullptr;
    blip_t* _blipRight = nullptr;
    uint32_t _frameCapacityClocks = 0;
    int16_t _lastSample[2] = {};
};
