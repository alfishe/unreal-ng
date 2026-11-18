// The MGT +D in snapshots (docs/inprogress/2026-11-15-mgt-plus-d/TODO.md P3.4): SZX PLSD carries the card (paging,
// latch, WD1772 task registers, 8K RAM), .z80 v3 hardware 3 / 6 the paging alone (byte 59, byte 83 = 16); every other
// snapshot runs its program with the +D paged out, though the loader's reset pages it in.

#include <gtest/gtest.h>

#include <fstream>
#include <functional>
#include <iterator>
#include <string>
#include <vector>

#include "_helpers/emulatortesthelper.h"
#include "_helpers/testpathhelper.h"
#include "emulator/cpu/core.h"
#include "emulator/cpu/z80.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/io/fdc/wd1793.h"
#include "emulator/ports/portdecoder.h"
#include "emulator/slots/cards/plusd/plusdcard.h"
#include "emulator/slots/slotcontrol.h"

namespace
{

class PlusDSnapshot_Test : public ::testing::Test
{
protected:
    Emulator* _emulator = nullptr;
    EmulatorContext* _context = nullptr;

    void TearDown() override
    {
        if (_emulator)
            EmulatorTestHelper::CleanupEmulator(_emulator);
    }

    void Create(bool withCard)
    {
        std::function<void(CONFIG&)> slotSet;
        std::string error;
        if (withCard)
            ASSERT_TRUE(SlotControl::CreateOverride({{"edge.1", "plusd"}}, slotSet, error)) << error;
        _emulator = EmulatorTestHelper::CreateStandardEmulator("48K", LoggerLevel::LogError, RamPowerOn::Zero, slotSet);
        ASSERT_NE(_emulator, nullptr);
        _context = _emulator->GetContext();
        // A program in RAM: the CPU state a snapshot keeps
        Z80& z80 = *_context->pCore->GetZ80();
        z80.pc = 0x8000;
        z80.sp = 0xBF00;
    }

    PlusDCard* Card() const
    {
        return PlusDCard::Find(_context);
    }

    void Out(uint16_t port, uint8_t value)
    {
        _context->pPortDecoder->WriteCycle(port, value, 0x8000);
    }

    static std::vector<uint8_t> ReadFile(const std::string& path)
    {
        std::ifstream file(path, std::ios::binary);
        return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
    }
};

}  // namespace

/// SZX (~30 ms): PLSD written and read back: paging, latch (drive 1, side 1), the WD1772's registers, the RAM
TEST_F(PlusDSnapshot_Test, SzxCarriesTheCard)
{
    Create(true);
    PlusDCard* card = Card();
    ASSERT_NE(card, nullptr);
    Out(0x00EF, 0x81);
    Out(0x00EB, 0x27);
    Out(0x00F3, 0x09);
    for (int i = 0; i < 0x2000; i++)
        card->Ram()[i] = static_cast<uint8_t>(i * 13 + 5);
    card->SetPaged(true);
    const std::string path = TestPathHelper::GetUniqueTestScratchPath("plusd.szx");
    ASSERT_TRUE(_emulator->SaveSnapshot(path));

    const std::vector<uint8_t> file = ReadFile(path);
    const std::string text(file.begin(), file.end());
    EXPECT_NE(text.find("PLSD"), std::string::npos) << "the file has a PLSD block";

    Out(0x00EF, 0x00);
    Out(0x00EB, 0x00);
    Out(0x00F3, 0x01);
    for (int i = 0; i < 0x2000; i++)
        card->Ram()[i] = 0;
    card->SetPaged(false);
    ASSERT_TRUE(_emulator->LoadSnapshot(path));

    card = Card();
    ASSERT_NE(card, nullptr);
    EXPECT_TRUE(card->IsPaged());
    EXPECT_EQ(card->Control(), 0x81);
    EXPECT_EQ(card->Fdc()->getDrive(), card->Drive(0));
    EXPECT_TRUE(card->Fdc()->getSideUp());
    EXPECT_EQ(card->Fdc()->getTrackRegister(), 0x27);
    EXPECT_EQ(card->Fdc()->getSectorRegister(), 0x09);
    int wrong = 0;
    for (int i = 0; i < 0x2000; i++)
        wrong += card->Ram()[i] != static_cast<uint8_t>(i * 13 + 5) ? 1 : 0;
    EXPECT_EQ(wrong, 0) << "RAM bytes that differ";
}

/// A PLSD on a machine without the card (~30 ms): the snapshot loads, the block is reported as ignored
TEST_F(PlusDSnapshot_Test, SzxWithoutTheCardLoads)
{
    Create(true);
    const std::string path = TestPathHelper::GetUniqueTestScratchPath("plusd-nocard.szx");
    ASSERT_TRUE(_emulator->SaveSnapshot(path));
    EmulatorTestHelper::CleanupEmulator(_emulator);
    _emulator = nullptr;

    Create(false);
    ASSERT_EQ(Card(), nullptr);
    EXPECT_TRUE(_emulator->LoadSnapshot(path));
    EXPECT_EQ(_context->pCore->GetZ80()->pc, 0x8000);
}

/// .z80 (~30 ms): hardware 3, byte 59 the paging, byte 83 = 16; read back paged in and paged out
TEST_F(PlusDSnapshot_Test, Z80KeepsThePaging)
{
    Create(true);
    PlusDCard* card = Card();
    ASSERT_NE(card, nullptr);
    for (bool paged : {true, false})
    {
        card->SetPaged(paged);
        const std::string path = TestPathHelper::GetUniqueTestScratchPath(paged ? "plusd-in.z80" : "plusd-out.z80");
        ASSERT_TRUE(_emulator->SaveSnapshot(path));
        const std::vector<uint8_t> file = ReadFile(path);
        ASSERT_GT(file.size(), 86u);
        EXPECT_EQ(file[34], 3) << "hardware 3: 48K + M.G.T.";
        EXPECT_EQ(file[59] != 0, paged) << "byte 59: paged";
        EXPECT_EQ(file[83], 16) << "byte 83: +D";

        card->SetPaged(!paged);
        ASSERT_TRUE(_emulator->LoadSnapshot(path));
        card = Card();
        ASSERT_NE(card, nullptr);
        EXPECT_EQ(card->IsPaged(), paged);
        EXPECT_EQ(_context->pCore->GetZ80()->pc, 0x8000);
    }
}

/// .sna (~20 ms): no +D in the format; the loader's reset pages the card in, the program runs with it paged out
TEST_F(PlusDSnapshot_Test, SnaRunsPagedOut)
{
    Create(true);
    PlusDCard* card = Card();
    ASSERT_NE(card, nullptr);
    const std::string path = TestPathHelper::GetUniqueTestScratchPath("plusd.sna");
    card->SetPaged(false);
    ASSERT_TRUE(_emulator->SaveSnapshot(path));
    card->SetPaged(true);
    ASSERT_TRUE(_emulator->LoadSnapshot(path));
    card = Card();
    ASSERT_NE(card, nullptr);
    EXPECT_FALSE(card->IsPaged());
}
