#include "stdafx.h"
#include "soundchip_ay8910.h"

/// region <Constants>

// Volume->voltage amplitude conversion table
// The AY-3-8910 has 16 levels in a logarithmic scale (3dB per step)
// YM2149 has 32 levels, the 16 extra levels are only used for envelope volumes
//
// Values normalized to unsigned 16 bits [0.0: 1.0] => [0x0000: 0xFFFF]
const uint16_t SoundChip_AY8910::_volumeTable[32] =
{
    0x0000, 0x0000,
    0x0340, 0x0340,
    0x04C0, 0x04C0,
    0x06F2, 0x06F2,
    0x0A44, 0x0A44,
    0x0F13, 0x0F13,
    0x1510, 0x1510,
    0x227E, 0x227E,
    0x289F, 0x289F,
    0x414E, 0x414E,
    0x5B21, 0x5B21,
    0x7258, 0x7258,
    0x905E, 0x905E,
    0xB550, 0xB550,
    0xD7A0, 0xD7A0,
    0xFFFF, 0xFFFF
};

/// endregion </Constants>

/// region <Nested classes>

/// region <Registers>

const char* SoundChip_AY8910::Registers::AYRegisterNames[16]
{
    "[Reg]  R0 - Channel A - fine tune",        // R0
    "[Reg]  R1 - Channel A - coarse tune",      // R1
    "[Reg]  R2 - Channel B - fine tune",        // R2
    "[Reg]  R3 - Channel B - coarse tune",      // R3
    "[Reg]  R4 - Channel C - fine tune",        // R4
    "[Reg]  R5 - Channel C - coarse tune",      // R5
    "[Reg]  R6 - Noise period",                 // R6
    "[Reg]  R7 - Mixer Control Enable",         // R7
    "[Reg]  R8 - Channel A - Amplitude",        // R8
    "[Reg]  R9 - Channel B - Amplitude",        // R9
    "[Reg]  R10 - Channel C - Amplitude",       // R10
    "[Reg]  R11 - Envelope period - fine",      // R11
    "[Reg]  R12 - Envelope period - coarse",    // R12
    "[Reg]  R13 - Envelope shape",              // R13
    "[Reg]  R14 - I/O Port A data store",       // R14
    "[Reg]  R15 - I/O Port B data store"        // R15
};

/// endregion </Registers>

/// region <RegisterAddressDecoder>

/// region <Constructors / Destructors>

SoundChip_AY8910::RegisterAddressDecoder::RegisterAddressDecoder(AYRegisters& registers) : _registers(registers)
{
}

SoundChip_AY8910::RegisterAddressDecoder::~RegisterAddressDecoder()
{
}

/// endregion </Constructors / Destructors>

/// region <Methods>

void SoundChip_AY8910::RegisterAddressDecoder::reset()
{
    // Reset contents for all registers
    memset(&_registers, 0, sizeof(AYRegisters));

    // Mute all channels. R7 - Mixer Control register has active low (0) signals
    _registers.reg[AY_MIXER_CONTROL] = 0xFF;
}

/// endregion </Methods>

/// endregion </RegisterAddressDecoder>

/// region <ToneGenerator>

SoundChip_AY8910::ToneGenerator::ToneGenerator()
{
    reset();
}

void SoundChip_AY8910::ToneGenerator::reset()
{
    _period = 0x0000;
    _amplitude = 0x00;

    _counter = 0x0000'0000;
}

/// The frequency of each square wave generated by the three Tone Generators
/// (one each for Channels A, B, and C) is obtained in the PSG by first
/// counting down the input clock by 16, then by further counting down the
/// result by the programmed 12-bit Tone Period value. Each 12-bit value
/// is obtained in the PSG by combining the contents of relative Coarse and
/// Fine Tune registers. (R1 + R0 for Channel A, R3 + R2 for Channel B,
/// R5 + R4 for Channel C)
void SoundChip_AY8910::ToneGenerator::setPeriod(uint8_t fine, uint8_t coarse)
{
    // 4 lowest bits from coarse + 8 bits from fine forms 12-bit Tone Period of Tone Generator
    _period = (coarse & 0b0000'1111) << 8 | fine;
}

void SoundChip_AY8910::ToneGenerator::setVolume(uint8_t amplitude)
{
    // Lower 5 bits of registers (R10 - Channel A, R11 - Channel B, R12 - Channel C)
    // define tone generator amplitude (volume)
    _amplitude = amplitude & 0b0001'1111;
}

uint16_t SoundChip_AY8910::ToneGenerator::render(size_t time)
{
    uint16_t result= 0x0000;

    // 0 period is not played
    if (_period < 1)
        return result;

    _counter++;

    if (_counter >= _period)
    {
        _counter = 0;
        _outPulse = !_outPulse;
    }

    return result;
}

/// endregion </ToneGenerator>

/// region <NoiseGenerator>

SoundChip_AY8910::NoiseGenerator::NoiseGenerator()
{
    reset();
}

void SoundChip_AY8910::NoiseGenerator::reset()
{
    _period = 0;
    _randomSeed = 1;
}

void SoundChip_AY8910::NoiseGenerator::setPeriod(uint8_t period)
{
    _period = period;
}

/// Returns LSFR-generated random numbers for noise generator
/// \return Next random number from LSFR
///
/// <b>Information:</b>
/// The Random Number Generator of the 8910 is a 17-bit shift register.
/// The input to the shift register is bit0 XOR bit3
/// Bit0 is the output
/// 17 stage LSFR with 1 tap (3)
///
/// See: https://en.wikipedia.org/wiki/Linear-feedback_shift_register
/// See: https://github.com/mamedev/mame/blob/master/src/devices/sound/ay8910.cpp
uint32_t SoundChip_AY8910::NoiseGenerator::getNextRandom()
{
    _randomSeed ^= (((_randomSeed & 1) ^ ((_randomSeed >> 3) & 1)) << 17);
    _randomSeed >>= 1;

    return _randomSeed;
}
/// endregion </NoiseGenerator>

/// region <EnvelopeGenerator>

/// 4 lowest bits of R13 (Envelope shape) register determine envelope shape
/// at the Envelope Generator Output
/// See more in AY-8910 datasheet (page 5-22): http://map.grauw.nl/resources/sound/generalinstrument_ay-3-8910.pdf
const uint8_t SoundChip_AY8910::EnvelopeGenerator::_envelopeShapes[ENVELOPE_SHAPE_COUNT][3] =
{
    { Envelope_Decay,  Envelope_StayLow,  Envelope_StayLow },       // 00 - 0b0000 - \___
    { Envelope_Decay,  Envelope_StayLow,  Envelope_StayLow },       // 01 - 0b0001 - \___
    { Envelope_Decay,  Envelope_StayLow,  Envelope_StayLow },       // 02 - 0b0010 - \___
    { Envelope_Decay,  Envelope_StayLow,  Envelope_StayLow },       // 03 - 0b0011 - \___
    { Envelope_Attack, Envelope_StayLow,  Envelope_StayLow },       // 04 - 0b0100 - /___
    { Envelope_Attack, Envelope_StayLow,  Envelope_StayLow },       // 05 - 0b0101 - /___
    { Envelope_Attack, Envelope_StayLow,  Envelope_StayLow },       // 06 - 0b0110 - /___
    { Envelope_Attack, Envelope_StayLow,  Envelope_StayLow },       // 07 - 0b0111 - /___
    { Envelope_Decay,  Envelope_Decay,    Envelope_Decay },         // 08 - 0b1000 - \\\\
    { Envelope_Fade,   Envelope_StayLow,  Envelope_StayLow },       // 09 - 0b1001 - \___
    { Envelope_Decay,  Envelope_Attack,   Envelope_Decay },         // 0A - 0b1010 - \/\/
    { Envelope_Decay,  Envelope_StayHigh, Envelope_StayHigh },      // 0B - 0b1011 - \---
    { Envelope_Attack, Envelope_Attack,   Envelope_Attack },        // 0C - 0b1100 - ////
    { Envelope_Attack, Envelope_StayHigh, Envelope_StayHigh },      // 0D - 0b1101 - /---
    { Envelope_Attack, Envelope_Decay,    Envelope_Attack },        // 0E - 0b1110 - /\/\
    { Envelope_Attack,Envelope_StayLow,   Envelope_StayLow }        // 0F - 0b1111 - /___
};

int16_t SoundChip_AY8910::EnvelopeGenerator::_envelopeWaves[ENVELOPE_COUNTER_MAX][ENVELOPE_SHAPE_BLOCKS];
bool SoundChip_AY8910::EnvelopeGenerator::_initialized = false;

SoundChip_AY8910::EnvelopeGenerator::EnvelopeGenerator()
{
    // Initialize shape waveform table if not done yet (static single-time)
    if (!_initialized)
    {
        generateEnvelopeShapes();

        _initialized = true;
    }

    reset();
}

SoundChip_AY8910::EnvelopeGenerator::~EnvelopeGenerator()
{

}

void SoundChip_AY8910::EnvelopeGenerator::reset()
{
    _period = 0;
    _shape = 0;
}

void SoundChip_AY8910::EnvelopeGenerator::setPeriod(uint8_t fine, uint8_t coarse)
{
    _period = (coarse << 8) | fine;
}

/// Sets current envelope shape [0:15] from register R15
/// \param shape 4 lowest bits determine envelope shape
void SoundChip_AY8910::EnvelopeGenerator::setShape(uint8_t shape)
{
    _shape = shape & 0x0000'1111;
}

/// Pre-create envelope shaped waveform samples
/// 16 shapes
/// 32 samples (5-bit counter) x 3 phase blocks
void SoundChip_AY8910::EnvelopeGenerator::generateEnvelopeShapes()
{
    uint8_t volume = 0;
    int8_t delta = 0;

    // Generate 16 envelope shapes
    for (int envelope = 0; envelope < ENVELOPE_SHAPE_COUNT; envelope++)
    {
        // Each shape is constructed from 3 blocks:
        // 1. Intro - played only once
        // 2-3 - repeated sequence
        for (int block = 0; block < ENVELOPE_SHAPE_BLOCKS; block++)
        {
            // Fetch envelope form from shapes dictionary
            EnvelopeBlockTypeEnum blockType = static_cast<EnvelopeBlockTypeEnum>(_envelopeShapes[envelope][block]);

            // Set initial volume and it's change rule
            // Min volume = 0; Max volume = 31
            // +1 - volume increases, 0 - volume remains the same, -1 - volume decreases
            switch (blockType)
            {
                case Envelope_Decay:
                    volume = ENVELOPE_COUNTER_MAX - 1;
                    delta = -1;
                    break;
                case Envelope_Attack:
                    volume = 0;
                    delta = 1;
                    break;
                case Envelope_StayLow:
                    volume = 0;
                    delta = 0;
                    break;
                case Envelope_StayHigh:
                    volume = ENVELOPE_COUNTER_MAX - 1;
                    delta = 0;
                    break;
            }

            // Generate 5-bits volume envelope for each block type
            for (int i = 0; i < ENVELOPE_COUNTER_MAX; i++)
            {
                // Join 3 channel volumes, 5-bit each (for Channels A, B, C) into single 16-bit sample
                _envelopeWaves[envelope][block * ENVELOPE_COUNTER_MAX + i] = (volume << (ENVELOPE_COUNTER_BITS * 2)) | (volume << ENVELOPE_COUNTER_BITS) | volume;

                volume += delta;
            }
        }
    }
}

/// endregion </EnvelopeGenerator>

/// endregion </Nested classes>

/// region <Constructors / Destructors>

SoundChip_AY8910::SoundChip_AY8910() : _decoder(_registers)
{

}

SoundChip_AY8910::~SoundChip_AY8910()
{

}
/// endregion </Constructors / Destructors>

/// region <Methods>

void SoundChip_AY8910::reset()
{
    // Reset decoder and the whole registers array
    _decoder.reset();

    // Reset generators
    _toneGenerators[AY_CHANNEL_A].reset();
    _toneGenerators[AY_CHANNEL_B].reset();
    _toneGenerators[AY_CHANNEL_C].reset();
    _noiseGenerator.reset();
    _envelopeGenerator.reset();
}

uint8_t SoundChip_AY8910::readRegister(uint8_t regAddr)
{
    uint8_t result = 0xFF;

    // Return value for valid register address, otherwise 0xFF
    if (regAddr <= 0x0F)
    {
        result = _registers.reg[regAddr];
    }

    return result;
}


void SoundChip_AY8910::writeRegister(uint8_t regAddr, uint8_t value, size_t time)
{
    // Invalid register address provided - ignore it
    if (regAddr > 0x0F)
        return;

    // XOR value with previous state => all non-zeroed bits indicate the change
    uint8_t changedBits = _registers.reg[regAddr] ^ value;

    // Apply new register value
    _registers.reg[regAddr] = value;

    switch (regAddr)
    {
        // Change period (frequency) for Channel A Tone Generator
        case AY_A_FINE:
        case AY_A_COARSE:
            _toneGenerators[AY_CHANNEL_A].setPeriod(_registers.reg[AY_A_FINE], _registers.reg[AY_A_COARSE]);
            break;
        // Change period (frequency) for Channel B Tone Generator
        case AY_B_FINE:
        case AY_B_COARSE:
            _toneGenerators[AY_CHANNEL_B].setPeriod(_registers.reg[AY_B_FINE], _registers.reg[AY_B_COARSE]);
            break;
        // Change period (frequency) for Channel C Tone Generator
        case AY_C_FINE:
        case AY_C_COARSE:
            _toneGenerators[AY_CHANNEL_C].setPeriod(_registers.reg[AY_C_FINE], _registers.reg[AY_C_COARSE]);
            break;
        // Change period (frequency) for Noise Generator
        case AY_NOISE_PERIOD:
            _noiseGenerator.setPeriod(_registers.reg[AY_NOISE_PERIOD]);
            break;
        case AY_MIXER_CONTROL:
            throw std::logic_error("AY_MIXER_CONTROL register not implemented yet");
            break;
        // Change volume for Channel A
        case AY_A_VOLUME:
            _toneGenerators[AY_CHANNEL_A].setVolume(_registers.reg[AY_A_VOLUME]);
            break;
        // Change volume for Channel B
        case AY_B_VOLUME:
            _toneGenerators[AY_CHANNEL_B].setVolume(_registers.reg[AY_B_VOLUME]);
            break;
        // Change volume for Channel C
        case AY_C_VOLUME:
            _toneGenerators[AY_CHANNEL_C].setVolume(_registers.reg[AY_C_VOLUME]);
            break;
        // Change period (frequency) for Envelope Generator
        case AY_ENVELOPE_PERIOD_FINE:
        case AY_ENVELOPE_PERIOD_COARSE:
            _envelopeGenerator.setPeriod(_registers.reg[AY_ENVELOPE_PERIOD_FINE], _registers.reg[AY_ENVELOPE_PERIOD_COARSE]);
            break;
        // Set one of 16 envelope shapes
        case AY_ENVELOPE_SHAPE:
            _envelopeGenerator.setShape(_registers.reg[AY_ENVELOPE_SHAPE]);
            break;
    }

    // TODO: Here we can log all register writes to get YM/MYM files
}

/// Generate PSG output as PCM data into the buffer
void SoundChip_AY8910::render()
{

}

/// endregion </Methods>