#include <gtest/gtest.h>
#include "stdafx.h"
#include "pch.h"

#include "_helpers/testtiminghelper.h"
#include "common/stringhelper.h"
#include "emulator/emulatorcontext.h"
#include <emulator/io/fdc/wd1793.h>
#include <emulator/cpu/z80.h>

/// region <Test types>

class WD1793_Test : public ::testing::Test
{
protected:
    EmulatorContext* _context = nullptr;
    CoreCUT* _core = nullptr;
    Z80* _z80 = nullptr;

    TestTimingHelper* _timingHelper = nullptr;

protected:
    void SetUp() override
    {
        _context = new EmulatorContext(LoggerLevel::LogError);

        // Mock Core and Z80 to make timings work
        _core = new CoreCUT(_context);
        _z80 = new Z80(_context);
        _core->_z80 = _z80;
        _context->pCore = _core;

        // Timing helper
        _timingHelper = new TestTimingHelper(_context);
        _timingHelper->resetClock();    // Reset all t-state counters within system (Z80, emulator state)
    }

    void TearDown() override
    {
        if (_timingHelper)
        {
            delete _timingHelper;
        }

        if (_context)
        {
            if (_context->pCore)
            {
                if (_core->_z80)
                {
                    _core->_z80 = nullptr;

                    delete _z80;
                }

                _context->pCore = nullptr;

                delete _core;
            }

            delete _context;
        }
    }
};

struct RangeCommand
{
    uint8_t rangeStart;
    uint8_t rangeEnd;
    VG93::WD_COMMANDS command;
};

class RangeLookup
{
private:
    static constexpr std::array<RangeCommand, 11> _referenceValues
            {
                    RangeCommand { 0x00, 0x0F, VG93CUT::WD_CMD_RESTORE },
                    RangeCommand { 0x10, 0x1F, VG93CUT::WD_CMD_SEEK },
                    RangeCommand { 0x20, 0x3F, VG93CUT::WD_CMD_STEP },
                    RangeCommand { 0x40, 0x5F, VG93CUT::WD_CMD_STEP_IN },
                    RangeCommand { 0x60, 0x7F, VG93CUT::WD_CMD_STEP_OUT },
                    RangeCommand { 0x80, 0x9F, VG93CUT::WD_CMD_READ_SECTOR },
                    RangeCommand { 0xA0, 0xBF, VG93CUT::WD_CMD_WRITE_SECTOR },
                    RangeCommand { 0xC0, 0xDF, VG93CUT::WD_CMD_READ_ADDRESS },
                    RangeCommand { 0xE0, 0xEF, VG93CUT::WD_CMD_READ_TRACK },
                    RangeCommand { 0xF0, 0xFF, VG93CUT::WD_CMD_WRITE_TRACK },
                    RangeCommand { 0xD0, 0xDF, VG93CUT::WD_CMD_FORCE_INTERRUPT },
            };

public:
    bool isValueInRange(uint8_t value)
    {
        bool result = false;

        for (const auto& rangeCommand : _referenceValues)
        {
            if (value >= rangeCommand.rangeStart && value <= rangeCommand.rangeEnd)
            {
                result = true;
            }
        }

        return result;
    }

    VG93CUT::WD_COMMANDS getCommandForValue(uint8_t value)
    {
        VG93CUT::WD_COMMANDS result = VG93CUT::WD_CMD_RESTORE;

        for (const auto& rangeCommand : _referenceValues)
        {
            if (value >= rangeCommand.rangeStart && value <= rangeCommand.rangeEnd)
            {
                result = rangeCommand.command;
            }
        }

        return result;
    }
};

/// endregion </Test types>

/// region <WD1793 commands>

/// Basic WD1793 commands decoding test
TEST_F(WD1793_Test, decodeWD93Command)
{
    RangeLookup referenceValues;

    for (int i = 0; i <= 255; i++)
    {
        VG93::WD_COMMANDS result = VG93CUT::decodeWD93Command(i);
        VG93::WD_COMMANDS reference = referenceValues.getCommandForValue(i);

        EXPECT_EQ(result, reference) << StringHelper::Format("0x%02X -> %s", i, VG93CUT::getWD_COMMANDName(result));

        //std::string message = StringHelper::Format("0x%02X -> %s", i, VG93CUT::getWD_COMMANDName(result));
        //std::cout << message << std::endl;
    }
}

TEST_F(WD1793_Test, isTypeNCommand)
{
    for (int i = 0; i <= 255; i++)
    {
        bool isType1Command = WD1793CUT::isType1Command(i);
        bool isType2Command = WD1793CUT::isType2Command(i);
        bool isType3Command = WD1793CUT::isType3Command(i);
        bool isType4Command = WD1793CUT::isType4Command(i);
        int trueCount = (isType1Command ? 1 : 0) + (isType2Command ? 1 : 0) + (isType3Command ? 1 : 0) + (isType4Command ? 1 : 0);

        std::string message = StringHelper::Format("%03d: t1: %d; t2: %d; t3: %d; t4: %d", i, isType1Command, isType2Command, isType3Command, isType4Command);
        EXPECT_EQ(trueCount, 1) << "Only one command type can be active at a time. " << message;

        //std::cout << message << std::endl;
    }
}

/// endregion </WD1793 commands>

/// region <FSM>

TEST_F(WD1793_Test, FSM_Restore)
{
    static constexpr size_t const Z80_FREQUENCY = 3.5 * 1'000'000;
    static constexpr size_t const TSTATES_IN_MS = Z80_FREQUENCY / 1000;
    static constexpr size_t const RESTORE_TEST_DURATION_SEC = 10;
    static constexpr size_t const RESTORE_TEST_DURATION_TSTATES = Z80_FREQUENCY * RESTORE_TEST_DURATION_SEC;
    static constexpr size_t const RESTORE_TEST_QUANT_TSTATES = 100; // Time increments during simulation

    WD1793CUT fdc(_context);

    // Mock parameters
    const uint8_t restoreCommand = 0b0000'1100; // RESTORE with load head, verify and fastest stepping rate 00 (3ms @ 2MHz, 6ms @ 1MHz)
    WD1793CUT::WD_COMMANDS decodedCommand = WD1793CUT::decodeWD93Command(restoreCommand);
    fdc._commandRegister = restoreCommand;
    fdc._lastDecodedCmd = decodedCommand;

    // Send command to FDC
    fdc.cmdRestore(restoreCommand);

    // Perform simulation loop
    for (size_t clk = 0; clk < RESTORE_TEST_DURATION_TSTATES; clk += RESTORE_TEST_QUANT_TSTATES)
    {
        fdc.process();
    }
}

/// endregion </FSM>