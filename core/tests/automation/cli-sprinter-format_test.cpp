/// @file cli-sprinter-format_test.cpp
/// @brief CLI `state sprinter` (cli-sprinter-format.h): the arguments, the subcommands and the
/// text, on a real Sprinter machine (the reports are DeviceState's, tested in
/// sprinterdevicestate_test.cpp). No CLI socket.

#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "../../automation/cli/src/commands/cli-sprinter-format.h"
#include "../emulator/machines/sprinter/sprinterfixture.h"

TEST(CliSprinterFormat_Test, ParseOptionsTakesKeysAndOnePositional)
{
    std::string map, dos, pn5, rw, positional, error;
    ASSERT_TRUE(CliSprinter::ParseOptions({"sprinter", "port", "21BC", "rw=w", "MAP=1", "dos=0"}, 2, map, dos, pn5, rw,
                                          positional, error))
        << error;
    EXPECT_EQ(positional, "21BC");
    EXPECT_EQ(rw, "w");
    EXPECT_EQ(map, "1");
    EXPECT_EQ(dos, "0");
    EXPECT_TRUE(pn5.empty());

    positional.clear();
    EXPECT_FALSE(CliSprinter::ParseOptions({"sprinter", "ports", "colour=1"}, 2, map, dos, pn5, rw, positional, error));
    EXPECT_NE(error.find("colour"), std::string::npos);
    positional.clear();
    EXPECT_FALSE(CliSprinter::ParseOptions({"sprinter", "port", "21BC", "7FFD"}, 2, map, dos, pn5, rw, positional, error));
}

class CliSprinterMachine_Test : public SprinterFixture
{
protected:
    std::string Run(const std::vector<std::string>& args) { return CliSprinter::StateText(_context, args); }
};

TEST_F(CliSprinterMachine_Test, SubcommandsRenderTheReports)
{
    EXPECT_NE(Run({"sprinter"}).find("module: Standard"), std::string::npos);

    // One port: index, code, name (a synthetic table entry)
    OpenDcp();
    SetCode(0x21BC, false, 0x2B);
    const std::string lookup = Run({"sprinter", "port", "21BC", "rw=w"});
    EXPECT_NE(lookup.find("code: 0x2B"), std::string::npos) << lookup;
    EXPECT_NE(lookup.find("name: IdePrimary"), std::string::npos) << lookup;

    // The table, one line per row
    const std::string table = Run({"sprinter", "ports", "rw=w"});
    EXPECT_NE(table.find("#2B  w    001x xxxx 101x x100  #20A4        1  IdePrimary"), std::string::npos) << table;

    // The screen text (an empty mode table: 32 empty lines)
    EXPECT_EQ(Run({"sprinter", "text"}), std::string(32, '\n'));

    // Errors are text, never an exception
    EXPECT_NE(Run({"sprinter", "ports", "map=7"}).find("Error: map must be 0-3"), std::string::npos);
    EXPECT_NE(Run({"sprinter", "port"}).find("Error: state sprinter port <hex>"), std::string::npos);
    EXPECT_NE(Run({"sprinter", "bogus"}).find("Error: unknown subcommand"), std::string::npos);
}

TEST(CliSprinterOther_Test, OtherMachinesSayNotASprinter)
{
    EmulatorContext context(LoggerLevel::LogError);
    EXPECT_NE(CliSprinter::StateText(&context, {"sprinter"}).find("Not a Sprinter machine"), std::string::npos);
    EXPECT_NE(CliSprinter::StateText(&context, {"sprinter", "ports"}).find("Error: Not a Sprinter machine"), std::string::npos);
}
