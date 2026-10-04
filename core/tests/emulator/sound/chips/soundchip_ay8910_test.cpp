#include "soundchip_ay8910_test.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <string>
#include <vector>

#include "common/stringhelper.h"
#include "emulator/emulatorcontext.h"
#include "emulator/sound/chips/soundchip_ay8910.h"

/// region <SetUp / TearDown>

void SoundChip_AY8910_Test::SetUp()
{
    // Instantiate emulator with all peripherals, but no configuration loaded
    _context = new EmulatorContext(LoggerLevel::LogError);
    _soundChip = new SoundChip_AY8910CUT(_context);
}

void SoundChip_AY8910_Test::TearDown()
{
    if (_soundChip != nullptr)
    {
        delete _soundChip;
        _soundChip = nullptr;
    }
}

/// endregion </Setup / TearDown>


TEST_F(SoundChip_AY8910_Test, writeRegister)
{
    SoundChip_AY8910& soundChip = *_soundChip;

    std::vector<std::tuple<uint16_t, uint8_t>> inputData =
    {

    };

    //_soundChip->writeRegister();
}

TEST_F(SoundChip_AY8910_Test, ToneGenerator)
{
    ToneGeneratorCUT toneGenerator;
}

/// region <Reset state and I/O ports>

// The /RESET (power-on, machine reset) state is the datasheets' one: every register is 0
// (GI AY-3-8910: "applying logic 0 to the Reset pin will reset all registers to 0"; Yamaha
// YM2149: "the contents of all registers in the array are reset to 0"; YM2203 /IC: "All the
// content of register array become 0"). R7 = 0 makes both I/O ports inputs, whose pins read
// #FF through the on-chip pull-ups ("when in the input mode, all pins will read normally
// high"). The emulator used to reset R7 to #FF (both ports outputs with latch 0, tone and noise
// off), which a reader of R14 / R15 saw as 0. Sources: docs/inprogress/2026-10-04-ay-reset/TODO.md

namespace
{
/// A chip with every register and the selection moved away from the reset state
void ScrambleRegisters(SoundChip_AY8910& chip)
{
    for (uint8_t reg = 0; reg < 16; reg++)
        chip.writeRegister(reg, static_cast<uint8_t>(0xA5 ^ (reg * 0x11)));
    chip.writeRegister(AY_MIXER_CONTROL, 0xFF);
    chip.setRegister(AY_PORTB);
}

/// The datasheet reset state, checked through every view the emulator has of it
void ExpectDatasheetResetState(SoundChip_AY8910& chip)
{
    const uint8_t* regs = chip.getRegisters();
    for (uint8_t reg = 0; reg < 16; reg++)
    {
        EXPECT_EQ(regs[reg], 0) << "register file R" << int(reg);
        EXPECT_EQ(chip.readRegister(reg), 0) << "readRegister R" << int(reg);
    }
    EXPECT_EQ(chip.getCurrentRegister(), 0) << "selected register";

    // The generator start state is unchanged by the register fix (e4c3bbbaf, b852df9f3): tone and
    // noise stay gated off until the program writes R7, fixed amplitude 0. Pinned in
    // GeneratorStartAfterResetIsUnchanged; whether the gates should follow R7 = 0 is an open
    // owner decision (docs/inprogress/2026-10-04-ay-reset/TODO.md)
    for (uint8_t ch = 0; ch < 3; ch++)
    {
        const auto& tone = chip.getToneGenerators()[ch];
        EXPECT_FALSE(tone.toneEnabled()) << "channel " << int(ch) << " tone gate";
        EXPECT_FALSE(tone.noiseEnabled()) << "channel " << int(ch) << " noise gate";
        EXPECT_EQ(tone.volume(), 0) << "channel " << int(ch) << " amplitude";
        EXPECT_FALSE(tone.envelopeEnabled()) << "channel " << int(ch) << " envelope mode";
    }

    // Both ports are inputs: a bus read gives the pulled-up pins, not the zero latch
    chip.setRegister(AY_PORTA);
    EXPECT_EQ(chip.portDeviceInMethod(0xFFFD), 0xFF) << "IN #FFFD of R14 (port A input)";
    EXPECT_EQ(chip.readCurrentRegister(), 0xFF) << "R14 bus read";
    chip.setRegister(AY_PORTB);
    EXPECT_EQ(chip.portDeviceInMethod(0xFFFD), 0xFF) << "IN #FFFD of R15 (port B input)";
    EXPECT_EQ(chip.readRegisterOnBus(AY_PORTB), 0xFF) << "R15 bus read";
    chip.setRegister(0);
}
}  // namespace

TEST_F(SoundChip_AY8910_Test, PowerOnStateIsTheDatasheetResetState)
{
    // Construction is power-on: the chip runs reset()
    SoundChip_AY8910CUT chip(_context);
    ExpectDatasheetResetState(chip);
}

TEST_F(SoundChip_AY8910_Test, ResetClearsAllRegisters)
{
    SoundChip_AY8910& chip = *_soundChip;
    ScrambleRegisters(chip);
    ASSERT_EQ(chip.getRegisters()[AY_MIXER_CONTROL], 0xFF);

    chip.reset();
    ExpectDatasheetResetState(chip);
}

TEST_F(SoundChip_AY8910_Test, ResetStateIsSilent)
{
    // Every amplitude is 0 after reset: no output at all
    SoundChip_AY8910& chip = *_soundChip;
    ScrambleRegisters(chip);
    chip.reset();
    for (int n = 0; n < 2000; n++)
    {
        chip.updateState(true);
        ASSERT_EQ(chip.mixedLeft(), 0.0) << "tick " << n;
        ASSERT_EQ(chip.mixedRight(), 0.0) << "tick " << n;
    }
}

/// The generators' start after a reset is pinned bit for bit: the register fix (R7 #FF -> 0) must not
/// move it. Tone counters at 0 with output low, the noise LFSR seeded with 1 (not #FFFFFFFF, see
/// NoiseGenerator::reset), noise period 1 and output low; the first 256 generator ticks of the noise
/// output and of tone A (period 1: toggles every tick) are the same as before the fix. Expected
/// values recorded with the pre-fix reset (R7 = #FF) and re-checked after it
TEST_F(SoundChip_AY8910_Test, GeneratorStartAfterResetIsUnchanged)
{
    SoundChip_AY8910& chip = *_soundChip;
    ScrambleRegisters(chip);
    for (int n = 0; n < 1000; n++)
        chip.updateState(true);  // move every generator away from its start
    chip.reset();

    // TTD blob layout (soundchip_ay8910.cpp): tone n at 17 + 7n (period u16, counter u16, volume,
    // flags, out), noise at 38 (period, counter u16, out, LFSR u32), envelope at 46
    std::vector<uint8_t> blob(chip.TTDStateSize());
    chip.TTDSaveState(blob.data());
    for (size_t ch = 0; ch < 3; ch++)
    {
        const uint8_t* tone = blob.data() + 17 + 7 * ch;
        const uint8_t expectedTone[7] = {0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
        EXPECT_EQ(0, std::memcmp(tone, expectedTone, 7)) << "tone " << ch << " start state";
    }
    const uint8_t expectedNoise[8] = {0x01, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00};
    EXPECT_EQ(0, std::memcmp(blob.data() + 38, expectedNoise, 8)) << "noise start state (period 1, LFSR seed 1)";

    // Envelope: shape 8 (continuous sawtooth, 279eb3d4e - kept for software that never writes R13,
    // although the register file reads R13 = 0), period 1, counter 0, segment 0, output 31
    const uint8_t expectedEnvelope[11] = {0x08, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x1F};
    EXPECT_EQ(0, std::memcmp(blob.data() + 46, expectedEnvelope, 11)) << "envelope start state";
    EXPECT_EQ(chip.readRegister(AY_ENVELOPE_SHAPE), 0x00) << "R13 reads 0 while the generator runs shape 8";

    static const char* const kNoise =
        "0000000000000000000000000000000001100000000000000000000000000110"
        "0001100000000000000000000110000000000110000000000000011000011000"
        "0110000110000000011000000000000000000000011001100001100000000000"
        "0000011001111000000001100000000001100110000111100110000110000110";
    std::string noise;
    std::string toneA;
    for (int n = 0; n < 256; n++)
    {
        chip.updateState(true);
        chip.TTDSaveState(blob.data());
        noise += blob[41] ? '1' : '0';  // noise out
        toneA += blob[23] ? '1' : '0';  // tone A out
        // Envelope output: a falling sawtooth 31..0, one step per tick (8 periods of 32 steps)
        EXPECT_EQ(static_cast<int8_t>(blob[56]), 31 - (n % 32)) << "envelope output at tick " << n;
    }
    EXPECT_EQ(noise, kNoise) << "noise LFSR output after reset";
    std::string expectedTone;
    for (int n = 0; n < 256; n++)
        expectedTone += (n % 2 == 0) ? '1' : '0';
    EXPECT_EQ(toneA, expectedTone) << "tone A output after reset";

    // LFSR after 128 shifts
    chip.TTDSaveState(blob.data());
    uint32_t lfsr = 0;
    std::memcpy(&lfsr, blob.data() + 42, 4);
    EXPECT_EQ(lfsr, 0x8126u);
}

TEST_F(SoundChip_AY8910_Test, OutputPortReadsItsLatchInputPortReadsPins)
{
    SoundChip_AY8910& chip = *_soundChip;
    chip.reset();

    // Latches written while both ports are inputs: the register file keeps them, the bus shows the pins
    chip.writeRegister(AY_PORTA, 0x5A);
    chip.writeRegister(AY_PORTB, 0x3C);
    EXPECT_EQ(chip.readRegister(AY_PORTA), 0x5A) << "register file keeps the latch";
    EXPECT_EQ(chip.readRegister(AY_PORTB), 0x3C) << "register file keeps the latch";
    EXPECT_EQ(chip.readRegisterOnBus(AY_PORTA), 0xFF);
    EXPECT_EQ(chip.readRegisterOnBus(AY_PORTB), 0xFF);

    // R7 bit 6: port A output - the latch appears, port B stays an input
    chip.writeRegister(AY_MIXER_CONTROL, 0b0100'0000);
    EXPECT_EQ(chip.readRegisterOnBus(AY_PORTA), 0x5A);
    EXPECT_EQ(chip.readRegisterOnBus(AY_PORTB), 0xFF);

    // R7 bit 7: port B output too
    chip.writeRegister(AY_MIXER_CONTROL, 0b1100'0000);
    chip.setRegister(AY_PORTA);
    EXPECT_EQ(chip.portDeviceInMethod(0xFFFD), 0x5A);
    chip.setRegister(AY_PORTB);
    EXPECT_EQ(chip.portDeviceInMethod(0xFFFD), 0x3C);

    // Only port B output; a later latch write shows at once on the output port
    chip.writeRegister(AY_MIXER_CONTROL, 0b1000'0000);
    chip.writeRegister(AY_PORTB, 0x81);
    EXPECT_EQ(chip.readRegisterOnBus(AY_PORTA), 0xFF);
    EXPECT_EQ(chip.readRegisterOnBus(AY_PORTB), 0x81);

    // Back to inputs: pins again, the latch survives in the register file
    chip.writeRegister(AY_MIXER_CONTROL, 0x00);
    EXPECT_EQ(chip.readRegisterOnBus(AY_PORTA), 0xFF);
    EXPECT_EQ(chip.readRegisterOnBus(AY_PORTB), 0xFF);
    EXPECT_EQ(chip.readRegister(AY_PORTA), 0x5A);
    EXPECT_EQ(chip.readRegister(AY_PORTB), 0x81);

    // Every other register reads back unchanged through the bus, whatever the port directions
    for (uint8_t reg = 0; reg < AY_PORTA; reg++)
    {
        const uint8_t value = static_cast<uint8_t>(0x10 + reg);
        chip.writeRegister(reg, value);
        EXPECT_EQ(chip.readRegisterOnBus(reg), value) << "R" << int(reg);
    }
    EXPECT_EQ(chip.readRegisterOnBus(0x10), 0xFF) << "no register 16";
}

/// endregion </Reset state and I/O ports>

/// region <Generators period/frequency tests>

TEST_F(SoundChip_AY8910_Test, GetToneGeneratorDivisor)
{
    uint32_t minDivisor = 0xFFFF'FFFF;
    uint32_t maxDivisor = 0x0000'0000;

    // Only 12 bits used for tone generator. What's exceed should be masked
    for (int i = 0; i < (1 << 16); i++)
    {
        /// Mask everything above 12-bit for reference value
        uint32_t refDivisor = i & ((1 << 12) - 1);
        if (refDivisor == 0)
            refDivisor = 1;
        refDivisor *= 16;

        uint8_t coarse = i >> 8;        // Sending all 8 bits instead of 4 and expect that method under test will mask correctly
        uint8_t fine = i & 0b1111'1111;

        uint32_t divisor = _soundChip->getToneGeneratorDivisor(fine, coarse);

        EXPECT_EQ(divisor, refDivisor);

        if (divisor < minDivisor)
            minDivisor = divisor;
        if (divisor > maxDivisor)
            maxDivisor = divisor;
    }

    double baseFrequency = static_cast<double>(_soundChip->AY_BASE_FREQUENCY) / 16.0;
    std::cout << StringHelper::Format("minDivisor: 0x%08X  maxDivisor: 0x%08X", minDivisor, maxDivisor) << std::endl;
    std::cout << StringHelper::Format("minFreq:    %.2lfHz      maxFreq:    %.02lfHz", baseFrequency / maxDivisor, baseFrequency / (minDivisor + 1));
}

TEST_F(SoundChip_AY8910_Test, GetToneGeneratorFrequency)
{
    double minFrequency = 100'000'000.0;
    double maxFrequency = 0.0;
    double baseFrequency = static_cast<double>(_soundChip->AY_BASE_FREQUENCY);

    // Only 12 bits used for tone generator. What's exceed should be masked
    for (int i = 0; i < (1 << 16); i++)
    {
        // Mask everything above 12-bit for reference value
        uint32_t refDivisor = i & ((1 << 12) - 1);
        if (refDivisor == 0)
            refDivisor = 1;
        double refFrequency = baseFrequency / (16 * refDivisor);

        uint8_t coarse = i >> 8;        // Sending all 8 bits instead of 4 and expect that method under test will mask correctly
        uint8_t fine = i & 0b1111'1111;

        double frequency = _soundChip->getToneGeneratorFrequency(1.75 * 1'000'000, fine, coarse);

        EXPECT_EQ(frequency, refFrequency);

        if (frequency < minFrequency)
            minFrequency = frequency;
        if (frequency > maxFrequency)
            maxFrequency = frequency;
    }

    std::cout << StringHelper::Format("minFreq:    %.2lfHz      maxFreq:    %.02lf    @%s", minFrequency, maxFrequency, _soundChip->printFrequency(_soundChip->AY_BASE_FREQUENCY).c_str());

    if (_soundChip->AY_BASE_FREQUENCY == 1.75 * 1'000'000)
    {
        ASSERT_NEAR(minFrequency, 26.71, 0.01);
        ASSERT_NEAR(maxFrequency, 109375.0, 0.01);
    }
    else if (_soundChip->AY_BASE_FREQUENCY == 1.7734 * 1'000'000)
    {
        ASSERT_NEAR(minFrequency, 27.07, 0.01);
        ASSERT_NEAR(maxFrequency, 110837.5, 0.01);
    }
}

TEST_F(SoundChip_AY8910_Test, GetNoiseGeneratorFrequency)
{
    double minFrequency = 100'000'000.0;
    double maxFrequency = 0.0;
    double baseFrequency = static_cast<double>(_soundChip->AY_BASE_FREQUENCY);

    // Only 5 bits used for noise generator. What's exceed should be masked
    for (int i = 0; i < (1 << 8); i++)
    {
        // Mask everything above 5-bit for reference value
        uint32_t refDivisor = i & ((1 << 5) - 1);
        if (refDivisor == 0)
            refDivisor = 1;
        double refFrequency = baseFrequency / (16 * refDivisor);

        double frequency = _soundChip->getNoiseGeneratorFrequency(1.75 * 1'000'000, i);

        ASSERT_NEAR(frequency, refFrequency, 0.01);

        if (frequency < minFrequency)
            minFrequency = frequency;
        if (frequency > maxFrequency)
            maxFrequency = frequency;
    }

    std::cout << StringHelper::Format("minFreq:    %.2lfHz      maxFreq:    %.02lf    @%s", minFrequency, maxFrequency, _soundChip->printFrequency(_soundChip->AY_BASE_FREQUENCY).c_str());

    if (_soundChip->AY_BASE_FREQUENCY == 1.75 * 1'000'000)
    {
        ASSERT_NEAR(minFrequency, 3528.23, 0.01);
        ASSERT_NEAR(maxFrequency, 109375.0, 0.01);
    }
    else if (_soundChip->AY_BASE_FREQUENCY == 1.7734 * 1'000'000)
    {
        ASSERT_NEAR(minFrequency, 27.067, 0.01);
        ASSERT_NEAR(maxFrequency, 110837.5, 0.01);
    }
}

TEST_F(SoundChip_AY8910_Test, printToneDivisorsFromFrequency)
{
    std::string message = _soundChip->printToneDivisorsFromFrequency(440.0, 1750000);
    std::cout << message;
}

/// endregion </Generators period/frequency tests>

/// region <Timings tests>
TEST_F(SoundChip_AY8910_Test, GetAYClockStateCounter)
{

}
/// endregion </Timings tests>
/// region <Output DC removal>

// The output DC remover is a one-pole high-pass (FilterDCBlocker, the output
// coupling capacitor), not the former 1024-sample moving average. Pinned at
// chip level: a burst leaves no delayed step (BW Demo music start: an R13
// write restarted the envelope for ~0.3 ms and the moving average answered
// with a step 4.7 ms later), and the bass fundamental survives.

TEST_F(SoundChip_AY8910_Test, OutputDcRemovalLeavesNoDelayedStepAfterBurst)
{
    SoundChip_AY8910& chip = *_soundChip;
    chip.writeRegister(AY_MIXER_CONTROL, 0x3F);  // tone and noise off: channel A is a plain DC level
    chip.writeRegister(AY_A_VOLUME, 0x0F);

    const size_t burst = size_t(0.0003 * SoundChip_AY8910::GENERATOR_RATE);
    double height = 0.0;
    for (size_t n = 0; n < burst; n++)
    {
        chip.updateState(true);
        height = std::max(height, chip.mixedLeft());
    }
    ASSERT_GT(height, 0.1) << "the burst did not reach the output";

    chip.writeRegister(AY_A_VOLUME, 0x00);
    chip.updateState(true);  // the falling edge itself

    // 20 ms of tail: four times the old 1024-sample (4.68 ms) window. The
    // moving average gave the burst back as a ramp as long as the burst
    // (0.3 ms, a click), so the change is measured over 0.5 ms spans, not
    // between neighbouring samples
    const size_t span = size_t(0.0005 * SoundChip_AY8910::GENERATOR_RATE);
    std::vector<double> tail;
    for (size_t n = 0; n < size_t(0.02 * SoundChip_AY8910::GENERATOR_RATE); n++)
    {
        chip.updateState(true);
        tail.push_back(chip.mixedLeft());
    }
    double maxChange = 0.0;
    for (size_t n = span; n < tail.size(); n++)
        maxChange = std::max(maxChange, std::fabs(tail[n] - tail[n - span]));
    EXPECT_LT(maxChange, height * 2e-3) << "a step after the burst ended";
}

TEST_F(SoundChip_AY8910_Test, OutputDcRemovalKeepsBassFundamental)
{
    // Square tone on channel A at volume 15; fundamental amplitude by
    // correlation over whole periods after 0.5 s of settling. The moving
    // average cut 50 Hz by ~21 dB; the 5 Hz high-pass by ~0.04 dB
    auto fundamental = [this](uint16_t tonePeriod) -> double
    {
        SoundChip_AY8910CUT chip(_context);
        chip.writeRegister(AY_A_FINE, uint8_t(tonePeriod & 0xFF));
        chip.writeRegister(AY_A_COARSE, uint8_t(tonePeriod >> 8));
        chip.writeRegister(AY_MIXER_CONTROL, 0x3E);  // tone A only
        chip.writeRegister(AY_A_VOLUME, 0x0F);

        const double ticksPerPeriod = 2.0 * tonePeriod;  // 16 AY clocks per step, 8 per tick
        const double w = 2.0 * 3.14159265358979323846 / ticksPerPeriod;
        const size_t settle = size_t(0.5 * SoundChip_AY8910::GENERATOR_RATE);
        const size_t measure = size_t(ticksPerPeriod) * 10;
        double sc = 0, cc = 0;
        for (size_t n = 0; n < settle + measure; n++)
        {
            chip.updateState(true);
            if (n >= settle)
            {
                sc += chip.mixedLeft() * std::sin(w * double(n));
                cc += chip.mixedLeft() * std::cos(w * double(n));
            }
        }
        return 2.0 * std::sqrt(sc * sc + cc * cc) / double(measure);
    };

    const double at1k = fundamental(109);    // 1003 Hz
    const double at50 = fundamental(2188);   // 49.99 Hz
    ASSERT_GT(at1k, 0.05) << "the tone did not reach the output";
    EXPECT_GT(20.0 * std::log10(at50 / at1k), -0.1) << "bass fundamental attenuated";
}

/// endregion </Output DC removal>
