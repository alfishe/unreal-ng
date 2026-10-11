// The Next's SD journal (docs/inprogress/2026-10-07-zx-next/automation-gaps-2026-10-11.md, P1)
// Source: D (design). The card protocol is SdCardSpi's; the SPI byte timing is spi_master.vhd's (16 CPU clocks per byte)

#include "stdafx.h"
#include "pch.h"

#include <gtest/gtest.h>

#include "../../../automation/cli/src/commands/cli-next-format.h"
#include "_helpers/emulatortesthelper.h"
#include "emulator/cpu/core.h"
#include "emulator/cpu/z80.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/io/sdcard/sdcardspi.h"
#include "emulator/io/storage/memorydisk.h"
#include "emulator/io/z80n/nextsdjournal.h"
#include "emulator/ports/models/portdecoder_next.h"
#include "emulator/state/devicestate.h"

namespace
{
NextSdEvent Event(uint8_t card, NextSdKind kind, uint8_t index, uint32_t frame)
{
    NextSdEvent e;
    e.card = card;
    e.kind = kind;
    e.index = index;
    e.frame = frame;
    return e;
}
}  // namespace

TEST(NextSdJournal_Test, RingKeepsTheNewestAndCountsTheEvicted)
{
    NextSdJournal journal(3);
    for (uint8_t i = 1; i <= 5; i++)
        journal.Record(Event(0, NextSdKind::Command, i, 10));
    EXPECT_EQ(journal.Size(), 3u);
    EXPECT_EQ(journal.Evicted(), 2u);
    const auto all = journal.Query(NextSdJournalQuery{});
    ASSERT_EQ(all.size(), 3u);
    EXPECT_EQ(all.front().seq, 3u) << "oldest first";
    EXPECT_EQ(all.back().index, 5);
    journal.Clear();
    EXPECT_EQ(journal.Size(), 0u);
    EXPECT_EQ(journal.LastSeq(), 5u) << "seq never goes back";
}

TEST(NextSdJournal_Test, QueryFiltersKindCardSeqFramesAndLimit)
{
    NextSdJournal journal;
    journal.Record(Event(0, NextSdKind::Command, 0, 1));
    journal.Record(Event(1, NextSdKind::Command, 17, 2));
    journal.Record(Event(1, NextSdKind::ReadBlock, 0, 3));
    journal.Record(Event(0, NextSdKind::WriteBlock, 0, 4));
    NextSdJournalQuery q;
    std::string error;
    ASSERT_TRUE(NextSdJournalQueryFromStrings("command", "", "", "", "", "", q, error)) << error;
    EXPECT_EQ(journal.Query(q).size(), 2u);
    ASSERT_TRUE(NextSdJournalQueryFromStrings("read,write", "", "", "", "", "", q, error)) << error;
    EXPECT_EQ(journal.Query(q).size(), 2u);
    ASSERT_TRUE(NextSdJournalQueryFromStrings("", "1", "", "", "", "", q, error)) << error;
    EXPECT_EQ(journal.Query(q).size(), 2u);
    ASSERT_TRUE(NextSdJournalQueryFromStrings("", "", "2", "", "", "", q, error)) << error;
    EXPECT_EQ(journal.Query(q).size(), 2u) << "since = seq 2 leaves events 3 and 4";
    ASSERT_TRUE(NextSdJournalQueryFromStrings("", "", "", "2", "3", "", q, error)) << error;
    EXPECT_EQ(journal.Query(q).size(), 2u);
    ASSERT_TRUE(NextSdJournalQueryFromStrings("", "", "", "", "", "1", q, error)) << error;
    ASSERT_EQ(journal.Query(q).size(), 1u);
    EXPECT_EQ(journal.Query(q)[0].kind, NextSdKind::WriteBlock) << "limit keeps the newest";
    EXPECT_FALSE(NextSdJournalQueryFromStrings("jump", "", "", "", "", "", q, error));
    EXPECT_FALSE(NextSdJournalQueryFromStrings("", "2", "", "", "", "", q, error));
    EXPECT_FALSE(NextSdJournalQueryFromStrings("", "", "x", "", "", "", q, error));
}

namespace
{
uint8_t Xfer(SdCardSpi& card, uint8_t mosi) { return card.exchange(mosi); }

uint8_t Command(SdCardSpi& card, uint8_t index, uint32_t arg, uint8_t crc)
{
    Xfer(card, 0x40 | index);
    Xfer(card, arg >> 24);
    Xfer(card, arg >> 16);
    Xfer(card, arg >> 8);
    Xfer(card, arg);
    Xfer(card, crc);
    for (int i = 0; i < 16; i++)
        if (uint8_t r = Xfer(card, 0xFF); r != 0xFF)
            return r;
    return 0xFF;
}
}  // namespace

TEST(SdCardSpiTrace_Test, CommandsCarryTheirArgumentAndR1AndBlocksAreTold)
{
    SdCardSpi card;
    ASSERT_TRUE(card.insert(std::make_unique<MemoryDisk>(64), SdCardSpi::WriteMode::Session, SdCardSpi::Type::SDHC));
    std::vector<SdTraceEvent> heard;
    card.setTraceListener([&heard](const SdTraceEvent& e) { heard.push_back(e); });
    card.select(true);

    Command(card, 16, 512, 0xFF);  // before CMD0: the card ignores it, the trace still hears the bytes it took as a command
    ASSERT_EQ(heard.size(), 1u);
    EXPECT_EQ(heard[0].r1, 0xFF) << "no response";
    EXPECT_EQ(Command(card, 0, 0, 0x95), 0x01);
    ASSERT_EQ(heard.size(), 2u);
    EXPECT_EQ(heard[1].kind, SdTraceEvent::Kind::Command);
    EXPECT_EQ(heard[1].index, 0);
    EXPECT_EQ(heard[1].r1, 0x01);
    EXPECT_TRUE(heard[1].crcValid);
    EXPECT_EQ(Command(card, 8, 0x1AA, 0x87), 0x01);
    EXPECT_EQ(heard[2].argument, 0x1AAu);
    // ACMD41 until ready
    for (int i = 0; i < 20; i++)
    {
        Command(card, 55, 0, 0xFF);
        if (Command(card, 41, 0x40000000, 0xFF) == 0x00)
            break;
    }
    const SdTraceEvent& acmd = heard.back();
    EXPECT_EQ(acmd.index, 41);
    EXPECT_TRUE(acmd.app) << "the command after CMD55 is an application command";
    EXPECT_EQ(acmd.r1, 0x00);

    heard.clear();
    EXPECT_EQ(Command(card, 17, 5, 0xFF), 0x00);
    ASSERT_GE(heard.size(), 2u);
    EXPECT_EQ(heard[0].kind, SdTraceEvent::Kind::ReadBlock) << "the block is queued while the command is decoded";
    EXPECT_EQ(heard[0].block, 5u);
    EXPECT_EQ(heard[1].kind, SdTraceEvent::Kind::Command);
    EXPECT_EQ(heard[1].index, 17);
    EXPECT_EQ(heard[1].argument, 5u);
}

class NextSdJournalMachine_Test : public ::testing::Test
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
    }
    void TearDown() override
    {
        if (_emulator)
            EmulatorTestHelper::CleanupEmulator(_emulator);
    }
    void Cmd0()
    {
        auto spaced = [&]() { _z80->t += 20; };  // a byte time (16 clocks) between two accesses
        _ports->DecodePortOut(0xE7, 0xFE, 0);
        const uint8_t cmd0[6] = {0x40, 0, 0, 0, 0, 0x95};
        for (uint8_t b : cmd0)
        {
            spaced();
            _ports->DecodePortOut(0xEB, b, 0);
        }
        for (int i = 0; i < 16; i++)
        {
            spaced();
            if (_ports->DecodePortIn(0xEB, 0) == 0x01)
                break;
        }
        _ports->DecodePortOut(0xE7, 0xFF, 0);
    }
};

TEST_F(NextSdJournalMachine_Test, TheJournalIsOffUntilSwitchedOnThenTellsTheCommand)
{
    ASSERT_TRUE(_ports->InsertSdCard(0, std::make_unique<MemoryDisk>(4096), SdCardSpi::WriteMode::Session));
    Cmd0();
    EXPECT_EQ(_ports->SdJournal().Size(), 0u) << "off: nothing recorded";

    const StateNode on = DeviceState::NextSdJournalControl(_context, 1, true);
    EXPECT_TRUE(on.find("enabled")->b);
    _z80->pc = 0x1234;
    _z80->m1_pc = 0x1234;
    Cmd0();
    const StateNode report = DeviceState::NextSdReport(_context, NextSdJournalQuery{});
    const StateNode* cards = report.find("cards");
    ASSERT_NE(cards, nullptr);
    ASSERT_EQ(cards->items.size(), 2u);
    EXPECT_TRUE(cards->items[0].find("present")->b);
    EXPECT_EQ(cards->items[0].find("type")->s, "SDHC") << "a Next's card is SDHC whatever its size";
    EXPECT_EQ(cards->items[0].find("state")->s, "idle");
    EXPECT_FALSE(cards->items[1].find("present")->b);
    const StateNode* events = report.find("events");
    ASSERT_NE(events, nullptr);
    ASSERT_EQ(events->items.size(), 1u);
    const StateNode& e = events->items[0];
    EXPECT_EQ(e.find("command")->s, "CMD0");
    EXPECT_EQ(e.find("r1")->s, "0x01");
    EXPECT_EQ(e.find("card")->i, 0);
    EXPECT_EQ(e.find("pc")->s, "0x1234");
    EXPECT_EQ(report.find("spi")->find("selected")->s, "none") << "the card was deselected again";
}

TEST_F(NextSdJournalMachine_Test, TheCliPrintsTheCardsAndTheJournal)
{
    auto run = [&](const std::vector<std::string>& args) { return CliNext::StateText(_context, args, "\n"); };
    ASSERT_TRUE(_ports->InsertSdCard(0, std::make_unique<MemoryDisk>(4096), SdCardSpi::WriteMode::Session));
    EXPECT_NE(run({"next", "sd"}).find("(off: `state next sd on` first)"), std::string::npos);
    EXPECT_NE(run({"next", "sd", "on"}).find("SD journal on"), std::string::npos);
    Cmd0();
    const std::string text = run({"next", "sd"});
    EXPECT_NE(text.find("card 0: SDHC, idle"), std::string::npos) << text;
    EXPECT_NE(text.find("card 1: none"), std::string::npos) << text;
    EXPECT_NE(text.find("CMD0 0x00000000 -> R1 0x01"), std::string::npos) << text;
    EXPECT_NE(run({"next", "sd", "kinds=write"}).find("0 event(s)"), std::string::npos);
    EXPECT_NE(run({"next", "sd", "card=7"}).find("Error:"), std::string::npos);
    EXPECT_NE(run({"next", "sd", "colour=1"}).find("unknown option"), std::string::npos);
    EXPECT_NE(run({"next", "sd", "clear"}).find("0 event(s) held"), std::string::npos);
}
