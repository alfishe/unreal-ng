#include "tape_test.h"

#include "common/dumphelper.h"
#include "common/stringhelper.h"

/// region <SetUp / TearDown>

void Tape_Test::SetUp()
{
    _emulator = new Emulator(LoggerLevel::LogError);
    if (!_emulator->Init())
    {
        throw std::runtime_error("Failed to initialize emulator for Tape_Test");
    }

    _context = _emulator->GetContext();
    _tape = new TapeCUT(_context);
}

void Tape_Test::TearDown()
{
    if (_tape != nullptr)
    {
        delete _tape;
        _tape = nullptr;
    }

    if (_emulator != nullptr)
    {
        _emulator->Stop();
        _emulator->Release();
        delete _emulator;
        _emulator = nullptr;
    }

    _context = nullptr;  // Owned by _emulator, don't delete
}

/// endregion </Setup / TearDown>

TEST_F(Tape_Test, generateBitstream)
{
    TapeCUT tape(_context);

    std::vector<uint8_t> data = {0x00, 0x01, 0x02, 0xFF};
    std::vector<uint32_t> referenceResult = {
        // Pilot
        2168, 2168, 2168, 2168, 2168, 2168, 2168, 2168, 2168, 2168,

        // Synchronization
        667, 735,

        // [0] - 0x00
        855, 855, 855, 855, 855, 855, 855, 855, 855, 855, 855, 855, 855, 855, 855, 855,

        // [1] - 0x01
        855, 855, 855, 855, 855, 855, 855, 855, 855, 855, 855, 855, 855, 855, 1710, 1710,

        // [2] - 0x02
        855, 855, 855, 855, 855, 855, 855, 855, 855, 855, 855, 855, 1710, 1710, 855, 855,

        // [3] - 0xFF
        1710, 1710, 1710, 1710, 1710, 1710, 1710, 1710, 1710, 1710, 1710, 1710, 1710, 1710, 1710, 1710,

        // Pause
        3500000};
    // Pilot: 10 pulses x 2168 = 21680; sync: 667 + 735 = 1402;
    // data: 2 edges per bit (0x00,0x01,0x02,0xFF) = 71820; pause: 3500000.
    // Total equals the sum of the edgePulseTimings reference below.
    constexpr size_t referenceDuration = 3500000 + 94902;

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

/// A1 (tape-manager design §5.2): period parameters are u32 — a TZX $11
/// turbo half-period above 65535 T-states must survive into edgePulseTimings
/// untruncated (edgePulseTimings is vector<uint32_t>; the old u16 signature
/// silently narrowed it).
TEST_F(Tape_Test, generateBitstreamAcceptsU32Periods)
{
    TapeCUT tape(_context);

    constexpr uint32_t BIG_PILOT_HALF = 70000;
    constexpr uint32_t BIG_ONE_HALF = 66000;

    TapeBlock tapeBlock;
    tapeBlock.type = TapeBlockFlagEnum::TAP_BLOCK_FLAG_DATA;
    tapeBlock.data = { 0xFF };  // one byte of ones: 8 x 2 edges

    size_t result = tape.generateBitstream(tapeBlock, BIG_PILOT_HALF, 667, 735, 855,
                                           BIG_ONE_HALF, /*pilotLength_pulses=*/2, /*pause_ms=*/0);

    // 2 pilot edges at 70000 + sync 667/735 + 16 edges at 66000
    constexpr size_t expected = 2 * 70000 + 667 + 735 + 16 * 66000;
    EXPECT_EQ(result, expected);
    EXPECT_EQ(tapeBlock.totalBitstreamLength, expected);

    // No truncation anywhere: first pilot edge and first data edge carry the
    // full u32 values (a u16 path would emit 70000-65536=4464 / 66000-65536=464)
    ASSERT_GE(tapeBlock.edgePulseTimings.size(), 5u);
    EXPECT_EQ(tapeBlock.edgePulseTimings[0], 70000u);
    EXPECT_EQ(tapeBlock.edgePulseTimings[4], 66000u);
}

TEST_F(Tape_Test, getPilotSample)
{
    // Pilot tone uses 2168 T-states half-period per ZX Spectrum tape specification
    // (not 855 which is for zero-bit data encoding)
    constexpr uint16_t PILOT_HALF_PERIOD = 2168;
    constexpr uint16_t PILOT_PERIOD = PILOT_HALF_PERIOD * 2;

    // Test a reasonable number of pilot tone periods
    constexpr size_t maxValue = PILOT_HALF_PERIOD * 100;

    for (size_t tState = 0; tState < maxValue; tState++)
    {
        uint8_t value = _tape->getPilotSample(tState);

        bool referenceValue = (tState % PILOT_PERIOD) < PILOT_HALF_PERIOD;

        if (value != referenceValue)
        {
            FAIL() << StringHelper::Format("Failed at tState: %d. Expected %d, found %d", tState, referenceValue,
                                           value);
        }

        /*
        std::string message = StringHelper::Format("tState: %07d, value: 0x%02X", tState, value);
        std::cout << message << std::endl;
        */
    }
}