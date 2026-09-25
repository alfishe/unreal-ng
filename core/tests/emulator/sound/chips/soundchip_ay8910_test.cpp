#include "soundchip_ay8910_test.h"

#include <algorithm>
#include <cmath>
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
