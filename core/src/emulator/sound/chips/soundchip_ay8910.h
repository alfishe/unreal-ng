#pragma once

#include <cmath>
#include "stdafx.h"

/// Information:
/// See:
///     https://en.wikipedia.org/wiki/General_Instrument_AY-3-8910
///     http://map.grauw.nl/resources/sound/generalinstrument_ay-3-8910.pdf
///     http://cpctech.cpc-live.com/docs/ay38912/psgspec.htm
///     https://www.cpcwiki.eu/index.php/Datasheet_AY-8913
///     http://www.armory.com/~rstevew/Public/SoundSynth/Novelty/AY3-8910/start.html
///     http://openmsx.org/doxygen/AY8910_8cc_source.html
///     https://github.com/mamedev/mame/blob/master/src/devices/sound/ay8910.cpp
///     http://spatula-city.org/~im14u2c/intv/jzintv-1.0-beta3/src/ay8910/ay8910.c
///     https://www.julien-nevo.com/arkostracker/index.php/ay-overview/

/// Base clock frequency is: 1.75MHz for Pentagon, 1.7734MHz for genuine ZX-Spectrum models
///


/// region <Types>

/// AY command registers enumeration
enum AYRegisterEnum : uint8_t
{
    AY_A_FINE = 0,
    AY_A_COARSE = 1,
    AY_B_FINE = 2,
    AY_B_COARSE = 3,
    AY_C_FINE = 4,
    AY_C_COARSE = 5,
    AY_NOISE_PERIOD = 6,
    AY_MIXER_CONTROL = 7,
    AY_A_VOLUME = 8,
    AY_B_VOLUME = 9,
    AY_C_VOLUME = 10,
    AY_ENVELOPE_PERIOD_FINE = 11,
    AY_ENVELOPE_PERIOD_COARSE = 12,
    AY_ENVELOPE_SHAPE = 13,
    AY_PORTA = 14,
    AY_PORTB = 15
};

/// AY tone generators enumeration
enum AYChannelsEnum : uint8_t
{
    AY_CHANNEL_A = 0,
    AY_CHANNEL_B = 1,
    AY_CHANNEL_C = 2
};

/// endregion </Types>

class SoundChip_AY8910
{
    /// region <Constants>
protected:
    // Pentagon 128K/1024K has 1.75MHz AY frequency
    // ZX-Spectrum 128K, +2, +3 has 1.7734MHz AY frequency
    static constexpr int AY_BASE_FREQUENCY = 1.75 * 1000000;
    //static constexpr int AY_BASE_FREQUENCY = 1.7734 * 1000000;


    // Audio sampling rate is 44100Hz
    static const int AY_SAMPLING_RATE = 44100;

    // How many AY cycles in each audio sample (at selected sampling rate / frequency)
    static constexpr double CYCLES_PER_SAMPLE = AY_BASE_FREQUENCY / AY_SAMPLING_RATE;

    // Chip-specific output amplitude (volume) logarithmic conversion table
    static const uint16_t _volumeTable[32];
    /// endregion </Constants>

    /// region <Nested classes>

    struct Registers
    {
        /// region <Constants>
    public:
        static const char* AYRegisterNames[16];
        /// endregion </Constants>

        uint8_t ChannelA_Fine;          // R0
        uint8_t ChannelA_Coarse;        // R1
        uint8_t ChannelB_Fine;          // R2
        uint8_t ChannelB_Coarse;        // R3
        uint8_t ChannelC_Fine;          // R4
        uint8_t ChannelC_Coarse;        // R5
        uint8_t Noise_Period;           // R6
        uint8_t Mixer_Control;          // R7
        uint8_t ChannelA_Amplitude;     // R8
        uint8_t ChannelB_Amplitude;     // R9
        uint8_t ChannelC_Amplitude;     // R10
        uint8_t EnvelopePeriod_Fine;    // R11
        uint8_t EnvelopePeriod_Coarse;  // R12
        uint8_t Envelope_Shape;         // R13
        uint8_t IOPortA_Datastore;      // R14
        uint8_t IOPortB_Datastore;      // R15
    };

    union AYRegisters
    {
        uint8_t reg[16];
        Registers named;
    };

    class RegisterAddressDecoder
    {
        /// region <Fields>
    protected:
        AYRegisters& _registers;
        /// endregion </Fields>

        /// region <Constructors / Destructors>
    public:
        RegisterAddressDecoder() = delete;
        RegisterAddressDecoder(const RegisterAddressDecoder&) = delete;
        RegisterAddressDecoder(AYRegisters& registers);
        virtual ~RegisterAddressDecoder();
        /// endregion </Constructors / Destructors>

        /// region <Methods>
    public:
        void reset();
        /// endregion </Methods>
    };

    /// <b>Information:</b>
    /// Produce the basic square wave tone frequencies for each channel (A, B, C)
    ///
    /// The frequency of each square wave generated by the three Tone Generators
    /// (one each for Channels A, B, and C) is obtained in the PSG by first
    /// counting down the input clock by 16, then by further counting down the
    /// result by the programmed 12-bit Tone Period value. Each 12-bit value
    /// is obtained in the PSG by combining the contents of relative Coarse and
    /// Fine Tune registers. (R1 + R0 for Channel A, R3 + R2 for Channel B,
    /// R5 + R4 for Channel C)
    ///
    /// For 1.75MHz clock that means:
    /// Highest frequency = 109375Hz (1.75MHz / 16)
    /// Lowest frequency = 26.7Hz (1.75MHz / 65536)
    class ToneGenerator
    {
        /// region <Fields>
    protected:
        uint16_t _period;   // Tone Generator channel period (in AY clock cycles)
        uint8_t _amplitude; // Tone Generator channel amplitude / volume

        uint32_t _counter;
        bool _outPulse;
        uint16_t _out;
        /// endregion </Fields>

        /// region <Constructors / Destructors>
    public:
        ToneGenerator();
        /// endregion </Constructors / Destructors>

        /// region <Methods>
    public:
        void reset();
        void setPeriod(uint8_t fine, uint8_t coarse);
        void setVolume(uint8_t amplitude);
        uint16_t render(size_t time);
        /// endregion </Methods>
    };

    /// <b>Information:</b>
    /// Produces a frequency modulated pseudo-random pulse-width square wave output
    ///
    /// The frequency of the noise source is obtained in the PSG by first counting
    /// down the input clock by 16, then by further counting down the result by
    /// the programmed 5-bit Noise Period (R6) value. This 5-bit value consists of
    /// the lower 5-bits (B4-B0) of register R6
    ///
    /// The Random Number Generator of the 8910 is a 17-bit shift register.
    /// The input to the shift register is bit0 XOR bit3, (bit0 is the output)
    class NoiseGenerator
    {
        /// region <Fields>
    protected:
        uint8_t _period;
        uint32_t _randomSeed;
        /// endregion </Fields>

        /// region <Constructors / Destructors>
        public:
            NoiseGenerator();
        /// endregion </Constructors / Destructors>

        /// region <Methods>
    public:
        void reset();
        void setPeriod(uint8_t period);
        uint32_t getNextRandom();
        /// endregion </Methods>
    };

    /// <b>Information:</b>
    /// Produces an envelope pattern which can be used to amplitude modulate the
    /// output of each Mixer (for channels A, B, C)
    ///
    /// The frequency of the envelope is obtained in the PSG by first counting down
    /// the input clock by 256, then by further counting down the result by the
    /// programmed 16-bit Envelope Period value. This 16-bit value is obtained in
    /// the PSG by combining the contents of the Envelope Coarse (R12) and
    /// Fine Tune (R11) registers
    ///
    /// For 1.75MHz clock that means:
    /// Highest frequency = 6836Hz (1.75MHz / 256)
    /// Lowest frequency = 0.1045 (1.75MHz / 16777216)
    class EnvelopeGenerator
    {
        /// region <Types>
        enum EnvelopeBlockTypeEnum : uint8_t
        {
            Envelope_Decay = 0,     // Volume goes down
            Envelope_Attack = 1,    // Volume goes up
            Envelope_StayLow = 2,   // Volume stays low
            Envelope_StayHigh = 3   // Volume stays high
        };
        /// endregion </Types>

        /// region <Constants>
    protected:
        static const uint8_t ENVELOPE_SHAPE_COUNT = 16;
        static const uint8_t ENVELOPE_SHAPE_BLOCKS = 3;
        static const uint8_t ENVELOPE_COUNTER_BITS = 5;
        static const uint8_t ENVELOPE_COUNTER_MAX = 1 << ENVELOPE_COUNTER_BITS;

        static const uint8_t _envelopeShapes[ENVELOPE_SHAPE_COUNT][ENVELOPE_SHAPE_BLOCKS];
        static int16_t _envelopeWaves[ENVELOPE_COUNTER_MAX][ENVELOPE_SHAPE_BLOCKS];

        static bool _initialized;
        /// endregion </Constants>

        /// region <Fields>
    protected:
        uint16_t _period;
        uint8_t _shape;
        /// endregion </Fields

        /// region <Constructors / Destructors>
    public:
        EnvelopeGenerator();
        virtual ~EnvelopeGenerator();
        /// endregion </Constructors / Destructors>

        /// region <Methods>
    public:
        void reset();
        void setPeriod(uint8_t fine, uint8_t coarse);
        void setShape(uint8_t shape);
        /// endregion </Methods>

        /// region <Helper methods>
    protected:
        void generateEnvelopeShapes();
        /// endregion </Helper methods>
    };

    class AmplificationControl
    {
        /// region <Methods>
    public:
        void reset();
        /// endregion </Methods>
    };

    /// endregion </Nested classes>

    /// region <Fields>
protected:
    // AY8910 registers
    AYRegisters _registers;

    // Register address decoder (handle bus interactions)
    RegisterAddressDecoder _decoder;

    // 3x Tone generators (A,B,C) + 1x Noise Generator + 1x Envelope Generator
    ToneGenerator _toneGenerators[3];
    NoiseGenerator _noiseGenerator;
    EnvelopeGenerator _envelopeGenerator;

    /// endregion </Fields>

    /// region <Constructors / Destructors>
public:
    SoundChip_AY8910();
    virtual ~SoundChip_AY8910();
    /// endregion </Constructors / Destructors>

    /// region <Methods>
public:
    void reset();

    uint8_t readRegister(uint8_t regAddr);
    void writeRegister(uint8_t regAddr, uint8_t value, size_t time);

    void render();
    /// endregion </Methods>
};

//
// Code Under Test (CUT) wrappers to allow access to protected and private properties and methods for unit testing / benchmark purposes
//
#ifdef _CODE_UNDER_TEST

// Expose all internal classes to public
class SoundChip_AY8910CUT : public SoundChip_AY8910
{
public:
    using SoundChip_AY8910::AYRegisters;
    using SoundChip_AY8910::ToneGenerator;
    using SoundChip_AY8910::NoiseGenerator;
    using SoundChip_AY8910::EnvelopeGenerator;
};

// Derive CUT class from publicized inner class and make all required fields public as well
class ToneGeneratorCUT : public SoundChip_AY8910CUT::ToneGenerator
{

};

// Derive CUT class from publicized inner class and make all required fields public as well
class NoiseGeneratorCUT: public SoundChip_AY8910CUT::NoiseGenerator
{

};

// Derive CUT class from publicized inner class and make all required fields public as well
class EnvelopeGeneratorCUT : public SoundChip_AY8910CUT::EnvelopeGenerator
{
public:
    using SoundChip_AY8910CUT::EnvelopeGenerator::_initialized;
    using SoundChip_AY8910CUT::EnvelopeGenerator::_envelopeShapes;
    using SoundChip_AY8910CUT::EnvelopeGenerator::_envelopeWaves;
};

#endif // _CODE_UNDER_TEST