#include "stdafx.h"
#include "pch.h"

#include <array>
#include "common/stringhelper.h"
#include "emulator/emulatorcontext.h"
#include "emulator/io/fdc/vg93.h"

class VG93_Test : public ::testing::Test
{
protected:
    EmulatorContext* _context = nullptr;

protected:
    //void SetUp() override;
    //void TearDown() override;
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

TEST_F(VG93_Test, decodeWD93Command)
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