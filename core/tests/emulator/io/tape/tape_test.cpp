#include "tape_test.h"

#include "common/dumphelper.h"
#include "common/stringhelper.h"

/// region <SetUp / TearDown>

void Tape_Test::SetUp()
{
    _context = new EmulatorContext(LoggerLevel::LogError);
    _tape = new TapeCUT(_context);
}

void Tape_Test::TearDown()
{
    if (_tape != nullptr)
    {
        delete _tape;
        _tape = nullptr;
    }
}

/// endregion </Setup / TearDown>

TEST_F(Tape_Test, generateBitstream)
{
    TapeCUT tape(_context);

    std::vector<uint8_t> data = { 0x00, 0x01, 0x02, 0xFF };
    std::vector<uint32_t> referenceResult =
    {
        // Pilot
        2168, 2168, 2168, 2168, 2168, 2168, 2168, 2168, 2168, 2168,

        // Synchronization
        667, 735,

        // [0] - 0x00
        855, 855, 855, 855, 855, 855, 855, 855,
        855, 855, 855, 855, 855, 855, 855, 855,

        // [1] - 0x01
        855, 855, 855, 855, 855, 855, 855, 855,
        855, 855, 855, 855, 855, 855, 1710, 1710,

        // [2] - 0x02
        855, 855, 855, 855, 855, 855, 855, 855,
        855, 855, 855, 855, 1710, 1710, 855, 855,

        // [3] - 0xFF
        1710, 1710, 1710, 1710, 1710, 1710, 1710, 1710,
        1710, 1710, 1710, 1710, 1710, 1710, 1710, 1710,

        // Pause
        3500000
    };
    constexpr size_t referenceDuration = 3500000 + 58992;

    TapeBlock tapeBlock;
    tapeBlock.type = TapeBlockFlagEnum::TAP_BLOCK_FLAG_HEADER;
    tapeBlock.data = data;

    size_t result = tape.generateBitstream(tapeBlock, 2168, 667, 735, 855, 1710, 10, 1000);

    EXPECT_EQ(result, referenceDuration);
    EXPECT_EQ(tapeBlock.totalBitstreamLength, referenceDuration);
    EXPECT_EQ(tapeBlock.edgePulseTimings, referenceResult);

    // region <Debug print>

    /*
    std::stringstream ss;

    ss << "Vector len: " << tapeBlock.edgePulseTimings.size() << std::endl;
    std::for_each(tapeBlock.edgePulseTimings.begin(), tapeBlock.edgePulseTimings.end(), [&ss](uint32_t value)
    {
        ss << value << ", ";
    });

    ss << std::endl;
    std::cout << ss.str();
    */

    // endregion </Debug print>
}

TEST_F(Tape_Test, getPilotSample)
{
    constexpr uint16_t SIGNAL_HALF_PERIOD = 855;
    constexpr uint16_t SIGNAL_PERIOD = SIGNAL_HALF_PERIOD * 2;

    //size_t maxValue = std::numeric_limits<size_t>::max();
    constexpr size_t maxValue = SIGNAL_HALF_PERIOD * 1000;


    for (size_t tState = 0; tState < maxValue; tState++)
    {
        uint8_t value = _tape->getPilotSample(tState);

        bool referenceValue = (tState % SIGNAL_PERIOD) < SIGNAL_HALF_PERIOD;

        if (value != referenceValue)
        {
            FAIL() << StringHelper::Format("Failed at tState: %d. Expected %d, found %d", tState, referenceValue, value);
        }

        /*
        std::string message = StringHelper::Format("tState: %07d, value: 0x%02X", tState, value);
        std::cout << message << std::endl;
        */
    }
}