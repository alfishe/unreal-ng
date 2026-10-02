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

// video / palette / ring (automation audit G3, G4, G10): the map, 16 pens a line, the ring rows
TEST_F(CliSprinterMachine_Test, VideoPaletteAndRingRenderAsText)
{
    SprinterVideoRam& vram = _decoder->GetVideoRam();
    Pld().rgMod = 0x00;
    vram.Write(SprinterVideoRam::ModeAddress(0, 0, 0), 0x10);  // text 80
    vram.Write(SprinterVideoRam::ModeAddress(1, 0, 0), 0xF0);  // border
    const std::string video = Run({"sprinter", "video"});
    EXPECT_NE(video.find("Sprinter mode table: page 0 (displayed)"), std::string::npos) << video;
    EXPECT_NE(video.find("\ntB"), std::string::npos) << video;
    EXPECT_NE(Run({"sprinter", "video", "squares=1"}).find("kind: text_80"), std::string::npos);
    EXPECT_NE(Run({"sprinter", "video", "page=3"}).find("Error: page must be 0 or 1"), std::string::npos);

    const std::string palette = Run({"sprinter", "palette", "4"});
    EXPECT_NE(palette.find("Palette 4 (text paper, column 0x3F0, used)"), std::string::npos) << palette;
    EXPECT_NE(palette.find("  F0: "), std::string::npos) << palette;
    EXPECT_NE(Run({"sprinter", "palette", "9"}).find("Error: k must be"), std::string::npos);

    const std::string ring = Run({"sprinter", "ring"});
    EXPECT_NE(ring.find("Covox-Blaster ring: covox, play 0x00"), std::string::npos) << ring;
    EXPECT_NE(ring.find("F0:"), std::string::npos) << ring;
}

// bios (automation audit G11): the report and a selection without a reset
TEST_F(CliSprinterMachine_Test, BiosReportsAndSelects)
{
    EXPECT_NE(Run({"sprinter", "bios"}).find("sp2k-3.06-hf2.rom"), std::string::npos);
    const std::string selected = Run({"sprinter", "bios", "-", "accel_int_suspend=1", "reset=0"});
    EXPECT_NE(selected.find("loads at the next reset"), std::string::npos) << selected;
    EXPECT_EQ(_context->config.sprinter.accel_int_suspend, 1);
    EXPECT_NE(Run({"sprinter", "bios", "9.9", "reset=0"}).find("Error: unknown BIOS"), std::string::npos);
}

TEST(CliSprinterOther_Test, OtherMachinesSayNotASprinter)
{
    EmulatorContext context(LoggerLevel::LogError);
    EXPECT_NE(CliSprinter::StateText(&context, {"sprinter"}).find("Not a Sprinter machine"), std::string::npos);
    EXPECT_NE(CliSprinter::StateText(&context, {"sprinter", "ports"}).find("Error: Not a Sprinter machine"), std::string::npos);
}
