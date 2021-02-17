#include "soundchip_ay8910_test.h"

#include "common/stringhelper.h"
#include "emulator/emulatorcontext.h"
#include "emulator/sound/chips/soundchip_ay8910.h"

/// region <SetUp / TearDown>

void SoundChip_AY8910_Test::SetUp()
{
    // Instantiate emulator with all peripherals, but no configuration loaded
    _context = new EmulatorContext();
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