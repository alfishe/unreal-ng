#include "soundchip_ay8910_test.h"

#include "common/stringhelper.h"
#include "emulator/emulatorcontext.h"
#include "emulator/sound/chips/soundchip_ay8910.h"

/// region <SetUp / TearDown>

void SoundChip_AY8910_Test::SetUp()
{
    // Instantiate emulator with all peripherals, but no configuration loaded
    _context = new EmulatorContext(LoggerLevel::LogError);
    _soundChip = new SoundChip_AY8910CUT();
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

TEST_F(SoundChip_AY8910_Test, ToneGenerator)
{
    ToneGeneratorCUT toneGenerator;
}

TEST_F(SoundChip_AY8910_Test, NoiseGenerator)
{
    NoiseGeneratorCUT noiseGenerator;

    uint16_t sample;
    for (int i = 0; i < 1000; i++)
    {
        sample = noiseGenerator.getNextRandom();

        std::string message = StringHelper::Format("[%d] %04X", i, sample);
        cout << message << std::endl;
    }

}

TEST_F(SoundChip_AY8910_Test, EnvelopeGenerator)
{
    EnvelopeGeneratorCUT envelopeGenerator;

    // Check if instance initialized successfully
    EXPECT_EQ(envelopeGenerator._initialized, true);

    std::stringstream ss;
    for (int i = 0; i < 32; i++)
    {
        std::string header = StringHelper::Format("");
        for (int j = 0; j < 3; j++)
        {
            cout << envelopeGenerator._envelopeWaves[i][j] << std::endl;
        }
    }
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
    cout << StringHelper::Format("minDivisor: 0x%08X  maxDivisor: 0x%08X", minDivisor, maxDivisor) << std::endl;
    cout << StringHelper::Format("minFreq:    %.2lfHz      maxFreq:    %.02lfHz", baseFrequency / maxDivisor, baseFrequency / (minDivisor + 1));
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

        double frequency = _soundChip->getToneGeneratorFrequency(fine, coarse);

        EXPECT_EQ(frequency, refFrequency);

        if (frequency < minFrequency)
            minFrequency = frequency;
        if (frequency > maxFrequency)
            maxFrequency = frequency;
    }

    cout << StringHelper::Format("minFreq:    %.2lfHz      maxFreq:    %.02lf    @%s", minFrequency, maxFrequency, _soundChip->printFrequency(_soundChip->AY_BASE_FREQUENCY).c_str());

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

        double frequency = _soundChip->getNoiseGeneratorFrequency(i);

        ASSERT_NEAR(frequency, refFrequency, 0.01);

        if (frequency < minFrequency)
            minFrequency = frequency;
        if (frequency > maxFrequency)
            maxFrequency = frequency;
    }

    cout << StringHelper::Format("minFreq:    %.2lfHz      maxFreq:    %.02lf    @%s", minFrequency, maxFrequency, _soundChip->printFrequency(_soundChip->AY_BASE_FREQUENCY).c_str());

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

/// endregion </Generators period/frequency tests>

/// region <Timings tests>
TEST_F(SoundChip_AY8910_Test, GetAYClockStateCounter)
{

}
/// endregion </Timings tests>