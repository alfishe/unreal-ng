// The Next's DivMMC journal (docs/inprogress/2026-10-07-zx-next/automation-gaps-2026-10-11.md, P2)
// Source: D (design). The automap rules are divmmc.vhd's / zxnext.vhd's (research-divmmc-original-rtl.md); the esxDOS call names are
// the tbblue firmware's src/asm/dot_commands/esxapi.def

#include "stdafx.h"
#include "pch.h"

#include <gtest/gtest.h>

#include "../../../automation/cli/src/commands/cli-next-format.h"
#include "_helpers/emulatortesthelper.h"
#include "emulator/cpu/core.h"
#include "emulator/cpu/z80.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/io/z80n/nextdivmmcjournal.h"
#include "emulator/memory/memory.h"
#include "emulator/ports/models/portdecoder_next.h"
#include "emulator/state/devicestate.h"

TEST(NextDivMmcJournal_Test, RingQueryAndNames)
{
    NextDivMmcJournal journal(3);
    for (uint8_t i = 0; i < 5; i++)
    {
        NextDivMmcEvent e;
        e.kind = i % 2 ? NextDivMmcKind::MapOut : NextDivMmcKind::MapIn;
        e.frame = i;
        journal.Record(e);
    }
    EXPECT_EQ(journal.Size(), 3u);
    EXPECT_EQ(journal.Evicted(), 2u);
    NextDivMmcJournalQuery q;
    std::string error;
    ASSERT_TRUE(NextDivMmcJournalQueryFromStrings("map_out", "", "", "", "", q, error)) << error;
    EXPECT_EQ(journal.Query(q).size(), 1u) << "frames 2, 3, 4 are held; only frame 3 is a map_out";
    ASSERT_TRUE(NextDivMmcJournalQueryFromStrings("", "", "3", "4", "", q, error)) << error;
    EXPECT_EQ(journal.Query(q).size(), 2u);
    EXPECT_FALSE(NextDivMmcJournalQueryFromStrings("jump", "", "", "", "", q, error));
    EXPECT_FALSE(NextDivMmcJournalQueryFromStrings("", "-1", "", "", "", q, error));

    EXPECT_STREQ(NextEsxCallName(0x9A), "F_OPEN");
    EXPECT_STREQ(NextEsxCallName(0x9D), "F_READ");
    EXPECT_STREQ(NextEsxCallName(0xB1), "F_GETFREE");
    EXPECT_EQ(NextEsxCallName(0x96), nullptr) << "no table assigns $96";
    EXPECT_STREQ(NextDivMmcEntryName(0x0008), "rst");
    EXPECT_STREQ(NextDivMmcEntryName(0x0066), "nmi");
    EXPECT_STREQ(NextDivMmcEntryName(0x3D2F), "3dxx");
    EXPECT_STREQ(NextDivMmcEntryName(0x056A), "rom3_routine");
}

class NextDivMmcJournalMachine_Test : public ::testing::Test
{
protected:
    Emulator* _emulator = nullptr;
    EmulatorContext* _context = nullptr;
    Z80* _z80 = nullptr;
    PortDecoder_Next* _ports = nullptr;

    void SetUp() override
    {
        _emulator = EmulatorTestHelper::CreateStandardEmulator("NEXT", LoggerLevel::LogError, RamPowerOn::Zero);
        ASSERT_NE(_emulator, nullptr);
        _context = _emulator->GetContext();
        _z80 = _context->pCore->GetZ80();
        _ports = dynamic_cast<PortDecoder_Next*>(_context->pPortDecoder);
        ASSERT_NE(_ports, nullptr);
        NextBoard& board = _ports->Board();
        board.WriteNextReg(0x83, 0xFF);  // the DivMMC port and memory enabled
        board.WriteNextReg(0x0A, 0x10);  // NR #0A bit 4: the automap
        board.WriteNextReg(0xB8, 0x82);  // #0008 only
        board.WriteNextReg(0xB9, 0xFF);  // whatever the ROM
        board.WriteNextReg(0xBA, 0x00);  // delayed
        board.WriteNextReg(0xBB, 0x40);  // the #1FF8 exit
    }
    void TearDown() override
    {
        if (_emulator)
            EmulatorTestHelper::CleanupEmulator(_emulator);
    }
};

TEST_F(NextDivMmcJournalMachine_Test, AnEntryAtRst8TellsTheEsxDosCallAndAnExitAtTheEndOfTheArea)
{
    NextDivMmc& divmmc = _ports->DivMmc();
    divmmc.OnMachineM1(0x0008);
    EXPECT_EQ(divmmc.Journal().Size(), 0u) << "off: nothing recorded, the automap still works";
    EXPECT_TRUE(divmmc.Automapped());
    divmmc.OnMachineM1(0x1FF8);
    EXPECT_FALSE(divmmc.Automapped());

    DeviceState::NextDivMmcJournalControl(_context, 1, true);
    // RST $08 / DEFB $9D at #C000: the RST has pushed #C001 (the byte after the RST) at SP
    Memory* memory = _context->pMemory;
    _z80->sp = 0x8000;
    memory->DirectWriteToZ80Memory(0x8000, 0x00);
    memory->DirectWriteToZ80Memory(0x8001, 0xC0);
    memory->DirectWriteToZ80Memory(0xC000, 0x9D);
    _z80->hl = 0x1234;
    _z80->bc = 0x0010;
    divmmc.OnMachineM1(0x0008);
    divmmc.OnMachineM1(0x1FF8);

    const StateNode report = DeviceState::NextDivMmcReport(_context, NextDivMmcJournalQuery{});
    const StateNode* events = report.find("events");
    ASSERT_NE(events, nullptr);
    ASSERT_EQ(events->items.size(), 3u) << "map_in, esx_call, map_out";
    EXPECT_EQ(events->items[0].find("kind")->s, "map_in");
    EXPECT_EQ(events->items[0].find("address")->s, "0x0008");
    EXPECT_EQ(events->items[0].find("entry")->s, "rst");
    EXPECT_EQ(events->items[0].find("map")->s, "delayed");
    const StateNode& call = events->items[1];
    EXPECT_EQ(call.find("kind")->s, "esx_call");
    EXPECT_EQ(call.find("name")->s, "F_READ");
    EXPECT_EQ(call.find("call")->s, "0x9D");
    EXPECT_EQ(call.find("return_address")->s, "0xC000");
    EXPECT_EQ(call.find("hl")->s, "0x1234");
    EXPECT_EQ(call.find("bc")->s, "0x0010");
    EXPECT_EQ(events->items[2].find("kind")->s, "map_out");
    EXPECT_EQ(events->items[2].find("address")->s, "0x1FF8");
    EXPECT_FALSE(events->items[2].find("mapped")->b);

    // NR #B8-#BB decoded; the state
    const StateNode* entries = report.find("entries");
    ASSERT_NE(entries, nullptr);
    EXPECT_TRUE(entries->items[1].find("enabled")->b) << "#0008";
    EXPECT_EQ(entries->items[1].find("when")->s, "always");
    EXPECT_FALSE(entries->items[0].find("enabled")->b) << "#0000";
    EXPECT_TRUE(report.find("state")->find("automap_armed")->b);
    EXPECT_FALSE(report.find("state")->find("automap_latch")->b);
}

TEST_F(NextDivMmcJournalMachine_Test, RetnDisableResetAndThePortAreTold)
{
    NextDivMmc& divmmc = _ports->DivMmc();
    DeviceState::NextDivMmcJournalControl(_context, 1, true);
    divmmc.OnMachineM1(0x0008);
    divmmc.OnRetn();
    divmmc.OnMachineM1(0x0008);
    _ports->Board().WriteNextReg(0x0A, 0x00);  // clears the automap: the latch drops at once
    divmmc.WritePort(0x80);                   // CONMEM; a port write is told whether or not the automap is armed
    divmmc.Reset();

    NextDivMmcJournalQuery q;
    std::string error;
    ASSERT_TRUE(NextDivMmcJournalQueryFromStrings("retn,disabled,port,reset", "", "", "", "", q, error)) << error;
    const auto events = divmmc.Journal().Query(q);
    ASSERT_EQ(events.size(), 4u);
    EXPECT_EQ(events[0].kind, NextDivMmcKind::Retn);
    EXPECT_EQ(events[1].kind, NextDivMmcKind::Disabled);
    EXPECT_EQ(events[2].kind, NextDivMmcKind::Port);
    EXPECT_EQ(events[2].control, 0x80);
    EXPECT_TRUE(events[2].mapped);
    EXPECT_EQ(events[3].kind, NextDivMmcKind::Reset);
}

TEST_F(NextDivMmcJournalMachine_Test, TheCliPrintsTheEntriesAndTheCalls)
{
    auto run = [&](const std::vector<std::string>& args) { return CliNext::StateText(_context, args, "\n"); };
    EXPECT_NE(run({"next", "divmmc"}).find("(off: `state next divmmc on` first)"), std::string::npos);
    EXPECT_NE(run({"next", "divmmc", "on"}).find("DivMMC journal on"), std::string::npos);
    Memory* memory = _context->pMemory;
    _z80->sp = 0x8000;
    memory->DirectWriteToZ80Memory(0x8000, 0x00);
    memory->DirectWriteToZ80Memory(0x8001, 0xC0);
    memory->DirectWriteToZ80Memory(0xC000, 0x9A);
    _ports->DivMmc().OnMachineM1(0x0008);
    const std::string text = run({"next", "divmmc"});
    EXPECT_NE(text.find("entry 0x0008 always, delayed"), std::string::npos) << text;
    EXPECT_NE(text.find("map_in 0x0008 (rst, delayed)"), std::string::npos) << text;
    EXPECT_NE(text.find("esx_call F_OPEN return 0xC000"), std::string::npos) << text;
    EXPECT_NE(run({"next", "divmmc", "kinds=jump"}).find("Error:"), std::string::npos);
    EXPECT_NE(run({"next", "divmmc", "colour=1"}).find("unknown option"), std::string::npos);
    EXPECT_NE(run({"next", "divmmc", "clear"}).find("0 event(s) held"), std::string::npos);
}
