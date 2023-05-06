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