// The NextREG write journal (docs/inprogress/2026-10-07-zx-next/design-nextreg-journal.md)
// Source: D (design; the register file semantics are zxnext.vhd's)

#include "stdafx.h"
#include "pch.h"

#include <gtest/gtest.h>

#include "_helpers/emulatortesthelper.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/io/z80n/nextregjournal.h"
#include "emulator/state/devicestate.h"
#include "emulator/ports/models/portdecoder_next.h"

TEST(NextRegJournal_Test, RingKeepsTheNewestAndCountsTheEvicted)
{
    NextRegJournal journal(4);
    journal.SetEnabled(true);
    for (uint8_t i = 1; i <= 6; i++)
        journal.Record(10, i, 0x8000 + i, NextRegSource::Port, i, i * 2, i - 1);
    EXPECT_EQ(journal.Size(), 4u);
    EXPECT_EQ(journal.Evicted(), 2u);
    EXPECT_EQ(journal.LastSeq(), 6u);
    const auto all = journal.Query(NextRegJournalQuery{});
    ASSERT_EQ(all.size(), 4u);
    EXPECT_EQ(all.front().seq, 3u);  // oldest first
    EXPECT_EQ(all.back().seq, 6u);
    EXPECT_EQ(all.back().previous, 5);
    journal.Clear();
    EXPECT_EQ(journal.Size(), 0u);
    EXPECT_EQ(journal.LastSeq(), 6u) << "seq never goes back: a reader can keep its cursor over a clear";
}

TEST(NextRegJournal_Test, QueryFiltersRegistersSourcesSeqFramesAndLimit)
{
    NextRegJournal journal;
    journal.SetEnabled(true);
    journal.Record(1, 0, 0x100, NextRegSource::NextReg, 0x07, 3, 0);
    journal.Record(2, 0, 0x102, NextRegSource::Copper, 0x41, 9, 0);
    journal.Record(3, 0, 0x104, NextRegSource::Port, 0x07, 0, 3);
    journal.Record(4, 0, 0x106, NextRegSource::NextReg, 0x02, 1, 0);
    NextRegJournalQuery q;
    std::string error;
    ASSERT_TRUE(NextRegJournalQueryFromStrings("07", "", "", "", "", "", q, error)) << error;
    EXPECT_EQ(journal.Query(q).size(), 2u);
    ASSERT_TRUE(NextRegJournalQueryFromStrings("", "copper,port", "", "", "", "", q, error)) << error;
    EXPECT_EQ(journal.Query(q).size(), 2u);
    ASSERT_TRUE(NextRegJournalQueryFromStrings("", "", "2", "", "", "", q, error)) << error;
    EXPECT_EQ(journal.Query(q).size(), 2u) << "since = seq 2 leaves events 3 and 4";
    ASSERT_TRUE(NextRegJournalQueryFromStrings("", "", "", "2", "3", "", q, error)) << error;
    EXPECT_EQ(journal.Query(q).size(), 2u);
    ASSERT_TRUE(NextRegJournalQueryFromStrings("", "", "", "", "", "1", q, error)) << error;
    const auto newest = journal.Query(q);
    ASSERT_EQ(newest.size(), 1u);
    EXPECT_EQ(newest[0].reg, 0x02) << "limit keeps the newest";
    EXPECT_FALSE(NextRegJournalQueryFromStrings("zz", "", "", "", "", "", q, error));
    EXPECT_FALSE(NextRegJournalQueryFromStrings("", "joystick", "", "", "", "", q, error));
}

TEST(NextRegJournal_Test, TheBoardRecordsEachDoorWithItsOwnSource)
{
    Emulator* emulator = EmulatorTestHelper::CreateStandardEmulator("NEXT", LoggerLevel::LogError, RamPowerOn::Zero);
    ASSERT_NE(emulator, nullptr);
    PortDecoder_Next* ports = dynamic_cast<PortDecoder_Next*>(emulator->GetContext()->pPortDecoder);
    ASSERT_NE(ports, nullptr);
    NextBoard& board = ports->Board();
    board.Write(0x15, 1);  // journal off: nothing recorded
    EXPECT_EQ(board.Journal().Size(), 0u);

    board.Journal().SetEnabled(true);
    board.SelectRegister(0x15);
    board.WriteSelected(0x03);          // OUT (#253B)
    board.WriteNextReg(0x16, 0x20);     // the NEXTREG instruction
    board.Write(0x17, 0x30);            // the board itself
    const auto events = board.Journal().Query(NextRegJournalQuery{});
    ASSERT_EQ(events.size(), 3u);
    EXPECT_EQ(events[0].source, NextRegSource::Port);
    EXPECT_EQ(events[0].reg, 0x15);
    EXPECT_EQ(events[0].previous, 0x01) << "the value before: the write with the journal off was stored";
    EXPECT_EQ(events[1].source, NextRegSource::NextReg);
    EXPECT_EQ(events[2].source, NextRegSource::Internal);
    EmulatorTestHelper::CleanupEmulator(emulator);
}

TEST(NextRegJournal_Test, TheReportDecodesTheRegistersThatMatterAndTheControlSwitchesIt)
{
    Emulator* emulator = EmulatorTestHelper::CreateStandardEmulator("NEXT", LoggerLevel::LogError, RamPowerOn::Zero);
    ASSERT_NE(emulator, nullptr);
    EmulatorContext* context = emulator->GetContext();
    PortDecoder_Next* ports = dynamic_cast<PortDecoder_Next*>(context->pPortDecoder);
    ASSERT_NE(ports, nullptr);

    StateNode state = DeviceState::NextRegJournalControl(context, 1, false, 0);
    ASSERT_NE(state.find("enabled"), nullptr);
    EXPECT_TRUE(state.find("enabled")->b);
    ports->Board().WriteNextReg(0x07, 0x03);
    ports->Board().WriteNextReg(0x02, 0x01);
    NextRegJournalQuery query;
    std::string error;
    ASSERT_TRUE(NextRegJournalQueryFromStrings("07", "", "", "", "", "", query, error)) << error;
    StateNode report = DeviceState::NextRegJournalReport(context, query);
    const StateNode* events = report.find("events");
    ASSERT_NE(events, nullptr);
    ASSERT_EQ(events->size(), 1u);
    const StateNode& event = events->items[0];
    EXPECT_EQ(event.find("decoded")->s, "CPU speed 28 MHz");
    EXPECT_EQ(event.find("source")->s, "nextreg");
    EXPECT_EQ(event.find("name")->s, "CPU Speed");

    state = DeviceState::NextRegJournalControl(context, 0, true, 0);
    EXPECT_FALSE(state.find("enabled")->b);
    EXPECT_EQ(state.find("size")->i, 0);
    EmulatorTestHelper::CleanupEmulator(emulator);
}
