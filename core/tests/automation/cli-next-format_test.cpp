/// @file cli-next-format_test.cpp
/// @brief CLI `state next` (cli-next-format.h): the subcommands and the journal's text, on a real NEXT machine (the reports are
/// DeviceState's, tested in nextregjournal_test.cpp and nextskeleton_test.cpp). No CLI socket.

#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "../../automation/cli/src/commands/cli-next-format.h"
#include "../_helpers/emulatortesthelper.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/ports/models/portdecoder_next.h"

TEST(CliNextFormat_Test, TheJournalSwitchesAndPrintsWhoWroteWhat)
{
    Emulator* emulator = EmulatorTestHelper::CreateStandardEmulator("NEXT", LoggerLevel::LogError, RamPowerOn::Zero);
    ASSERT_NE(emulator, nullptr);
    EmulatorContext* context = emulator->GetContext();
    PortDecoder_Next* ports = dynamic_cast<PortDecoder_Next*>(context->pPortDecoder);
    ASSERT_NE(ports, nullptr);
    auto run = [&](const std::vector<std::string>& args) { return CliNext::StateText(context, args, "\n"); };

    EXPECT_NE(run({"next", "journal"}).find("(off: `state next journal on` first)"), std::string::npos);
    EXPECT_NE(run({"next", "journal", "on"}).find("NextREG journal on"), std::string::npos);
    ports->Board().WriteNextReg(0x07, 0x03);
    ports->Board().WriteNextReg(0x15, 0x01);
    const std::string all = run({"next", "journal"});
    EXPECT_NE(all.find("2 event(s)"), std::string::npos) << all;
    const std::string speed = run({"next", "journal", "regs=07", "sources=nextreg"});
    EXPECT_NE(speed.find("NR0x07 0x00 -> 0x03  CPU Speed (CPU speed 28 MHz)"), std::string::npos) << speed;
    EXPECT_EQ(speed.find("NR0x15"), std::string::npos);
    EXPECT_NE(run({"next", "journal", "regs=zz"}).find("Error:"), std::string::npos);
    EXPECT_NE(run({"next", "journal", "colour=1"}).find("unknown option"), std::string::npos);
    EXPECT_NE(run({"next", "journal", "clear"}).find("0 event(s) held"), std::string::npos);
    EXPECT_NE(run({"next", "regs"}).find("registers"), std::string::npos);
    EXPECT_NE(run({"next", "bogus"}).find("unknown subcommand"), std::string::npos);
    EmulatorTestHelper::CleanupEmulator(emulator);
}
