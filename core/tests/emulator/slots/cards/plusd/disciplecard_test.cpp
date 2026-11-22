// PlusDCard as the MGT DISCiPLE (core/src/emulator/slots/cards/plusd/plusdcard.h): the same card class as the +D on
// the DISCiPLE's ports (#1B/#5B/#9B/#DB the WD1772, #1F the control latch and the Kempston socket, #7B the RAM / ROM
// swap, #BB the paging), its traps (#0001, #0008, #0066, #028E), the inhibit button and the hold-off after /RESET.
// Design: docs/inprogress/2026-11-15-mgt-plus-d/design.md §12, hardware-reference.md §9.

#include <gtest/gtest.h>

#include <functional>
#include <string>
#include <utility>
#include <vector>

#include "_helpers/emulatortesthelper.h"
#include "emulator/cpu/core.h"
#include "emulator/cpu/z80.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/io/fdc/fdd.h"
#include "emulator/io/fdc/wd1793.h"
#include "emulator/io/joystick/joystick.h"
#include "emulator/media/mediamanager.h"
#include "emulator/memory/memory.h"
#include "emulator/ports/portdecoder.h"
#include "emulator/slots/cards/plusd/plusdcard.h"
#include "emulator/slots/slotcontrol.h"
#include "emulator/slots/slotmanager.h"
#include "emulator/state/devicestate.h"
#include "emulator/state/statenode.h"

namespace
{

class DiscipleCard_Test : public ::testing::Test
{
protected:
    Emulator* _emulator = nullptr;
    EmulatorContext* _context = nullptr;

    void TearDown() override
    {
        if (_emulator)
            EmulatorTestHelper::CleanupEmulator(_emulator);
    }

    void Create(const std::string& model, const std::vector<std::pair<std::string, std::string>>& slots)
    {
        std::function<void(CONFIG&)> slotSet;
        if (!slots.empty())
        {
            std::string error;
            ASSERT_TRUE(SlotControl::CreateOverride(slots, slotSet, error)) << error;
        }
        _emulator = EmulatorTestHelper::CreateStandardEmulator(model, LoggerLevel::LogError, RamPowerOn::Zero, slotSet);
        ASSERT_NE(_emulator, nullptr);
        _context = _emulator->GetContext();
    }

    /// A 48K with the card in the edge slot, its reset hold-off over
    PlusDCard* CreateReady(bool inhibit = false)
    {
        std::vector<std::pair<std::string, std::string>> slots = {{"edge.1", "disciple"}};
        if (inhibit)
            slots.push_back({"edge.1.inhibit", "on"});
        Create("48K", slots);
        PlusDCard* card = Card();
        if (card != nullptr)
            EndHold(card);
        return card;
    }

    PlusDCard* Card() const
    {
        return PlusDCard::Find(_context);
    }

    Z80* Cpu() const
    {
        return _context->pCore->GetZ80();
    }

    /// Two low fetches 100 T apart: the first starts the hold-off, the second ends it
    void EndHold(PlusDCard* card)
    {
        card->BeforeMachineM1(0x0100);
        Cpu()->t += 100;
        card->BeforeMachineM1(0x0100);
    }

    uint8_t In(uint16_t port, bool& drove)
    {
        return _context->pPortDecoder->ReadCycle(port, 0x8000, drove);
    }

    void Out(uint16_t port, uint8_t value)
    {
        _context->pPortDecoder->WriteCycle(port, value, 0x8000);
    }

    void Run(const std::vector<uint8_t>& program, int steps)
    {
        uint16_t address = 0x8000;
        for (uint8_t b : program)
            _context->pMemory->DirectWriteToZ80Memory(address++, b);
        Z80* cpu = Cpu();
        cpu->pc = 0x8000;
        cpu->sp = 0xFF00;
        cpu->halted = false;
        for (int i = 0; i < steps; i++)
            cpu->Z80Step(true);
    }

    uint8_t CpuReads(uint16_t address)
    {
        Run({0x3A, static_cast<uint8_t>(address), static_cast<uint8_t>(address >> 8),  // LD A,(nn)
             0x32, 0x00, 0x90,                                                       // LD (#9000),A
             0x18, 0xFE},                                                            // JR $
            2);
        return _context->pMemory->DirectReadFromZ80Memory(0x9000);
    }
};

}  // namespace

/// The card on a 48K (~10 ms): the DISCiPLE model, the GDOS 3 ROM, its two drive slots, a WD1772
TEST_F(DiscipleCard_Test, PluggedOn48K)
{
    Create("48K", {{"edge.1", "disciple"}});
    PlusDCard* card = Card();
    ASSERT_NE(card, nullptr);
    EXPECT_TRUE(card->IsDisciple());
    EXPECT_STREQ(card->CardId(), "disciple");
    ASSERT_NE(card->Fdc(), nullptr);
    EXPECT_TRUE(card->Fdc()->Is1772());
    EXPECT_TRUE(_context->pMediaManager->HasSlot("fdd.disciple1"));
    EXPECT_TRUE(_context->pMediaManager->HasSlot("fdd.disciple2"));
    EXPECT_FALSE(_context->pMediaManager->HasSlot("fdd.plusd1"));
    EXPECT_EQ(card->Drive(0)->getSlotId(), "fdd.disciple1");
    // GDOS 3: DI; XOR A; OUT (#1F),A; JP #02F2
    EXPECT_EQ(card->Rom()[0], 0xF3);
    EXPECT_EQ(card->Rom()[2], 0xD3);
    EXPECT_EQ(card->Rom()[3], 0x1F);
}

/// The presets carry the card (~10 ms)
TEST_F(DiscipleCard_Test, PresetsCarryTheCard)
{
    Create("48K-DISCIPLE", {});
    ASSERT_NE(Card(), nullptr);
    EXPECT_TRUE(Card()->IsDisciple());
}

/// The FDC registers on #1B / #5B / #9B / #DB, A8-A15 not decoded (~5 ms)
TEST_F(DiscipleCard_Test, PortsReachTheWd1772)
{
    PlusDCard* card = CreateReady();
    ASSERT_NE(card, nullptr);
    bool drove = false;
    Out(0x005B, 0x23);
    EXPECT_EQ(In(0x125B, drove), 0x23) << "track register";
    EXPECT_TRUE(drove);
    Out(0xFF9B, 0x05);
    EXPECT_EQ(In(0x009B, drove), 0x05) << "sector register";
    Out(0x00DB, 0x5A);
    EXPECT_EQ(card->Fdc()->getDataRegister(), 0x5A) << "data register";
    EXPECT_EQ(card->Fdc()->getTrackRegister(), 0x23);
    In(0x00BB, drove);
    EXPECT_FALSE(drove) << "#BB pages in and leaves the bus alone";
}

/// The control latch: bit 0 drive 1 / 2, bit 1 the side, bit 2 /DDEN (~5 ms)
TEST_F(DiscipleCard_Test, ControlLatchSelectsDriveSideDensity)
{
    PlusDCard* card = CreateReady();
    ASSERT_NE(card, nullptr);
    WD1793* fdc = card->Fdc();
    Out(0x001F, PlusDCard::kDiscipleControlDrive1);
    EXPECT_EQ(fdc->getDrive(), card->Drive(0));
    EXPECT_FALSE(fdc->getSideUp());
    EXPECT_TRUE(fdc->isDoubleDensityMode());
    Out(0x001F, PlusDCard::kDiscipleControlSide | PlusDCard::kDiscipleControlSingleDensity);
    EXPECT_EQ(fdc->getDrive(), card->Drive(1)) << "bit 0 clear: drive 2";
    EXPECT_TRUE(fdc->getSideUp());
    EXPECT_FALSE(fdc->isDoubleDensityMode()) << "/DDEN high: FM";
}

/// #1F reads the Kempston socket on bits 0-4, the printer busy (no printer) (~5 ms)
TEST_F(DiscipleCard_Test, ControlPortReadsTheJoystick)
{
    PlusDCard* card = CreateReady();
    ASSERT_NE(card, nullptr);
    ASSERT_NE(_context->pJoystick, nullptr);
    bool drove = false;
    EXPECT_EQ(In(0x001F, drove), 0x00);
    EXPECT_TRUE(drove);
    _context->pJoystick->Press(0x11);  // right + fire
    EXPECT_EQ(In(0x001F, drove), 0x11);
    bool peekDrives = false;
    EXPECT_EQ(card->Peek(0x001F, peekDrives), 0x11);
}

/// #BB: a read pages in, a write pages out; paged in the ROM is at #0000 and the RAM at #2000; #7B swaps them (~5 ms)
TEST_F(DiscipleCard_Test, PagingAndMemorySwap)
{
    PlusDCard* card = CreateReady();
    ASSERT_NE(card, nullptr);
    ASSERT_FALSE(card->IsPaged());
    bool drove = false;
    In(0x00BB, drove);
    ASSERT_TRUE(card->IsPaged());
    EXPECT_EQ(CpuReads(0x0000), card->Rom()[0]);

    Run({0x3E, 0xA5, 0x32, 0x45, 0x23, 0x18, 0xFE}, 2);  // LD A,#A5; LD (#2345),A
    EXPECT_EQ(card->Ram()[0x0345], 0xA5);

    Out(0x007B, 0x00);
    EXPECT_TRUE(card->IsMemorySwapped());
    EXPECT_EQ(CpuReads(0x0345), 0xA5) << "the RAM at #0000";
    EXPECT_EQ(CpuReads(0x2000), card->Rom()[0]) << "the ROM at #2000";
    Run({0x3E, 0x5A, 0x32, 0x10, 0x00, 0x18, 0xFE}, 2);  // LD A,#5A; LD (#0010),A
    EXPECT_EQ(card->Ram()[0x0010], 0x5A);

    In(0x007B, drove);
    EXPECT_FALSE(card->IsMemorySwapped());
    EXPECT_EQ(CpuReads(0x0000), card->Rom()[0]);

    Out(0x00BB, 0x00);
    EXPECT_FALSE(card->IsPaged());
    EXPECT_EQ(CpuReads(0x0001), 0xAF) << "the 48K ROM";
}

/// The traps #0001 / #0008 / #0066 / #028E page in on the fetch; the +D's #003A does not (~5 ms)
TEST_F(DiscipleCard_Test, M1TrapsPageIn)
{
    PlusDCard* card = CreateReady();
    ASSERT_NE(card, nullptr);
    for (uint16_t trap : {0x0001, 0x0008, 0x0066, 0x028E})
    {
        Out(0x00BB, 0x00);
        ASSERT_FALSE(card->IsPaged());
        card->BeforeMachineM1(static_cast<uint16_t>(trap + 1));
        EXPECT_FALSE(card->IsPaged()) << "fetch at " << trap + 1;
        card->BeforeMachineM1(trap);
        EXPECT_TRUE(card->IsPaged()) << "fetch at " << trap;
    }
    Out(0x00BB, 0x00);
    card->BeforeMachineM1(0x003A);
    EXPECT_FALSE(card->IsPaged()) << "#003A is a +D trap only";
}

/// The inhibit button: no trap and no #BB page-in, unless the latch's bit 4 lets them; a latch write without bit 4
/// pages out (~5 ms)
TEST_F(DiscipleCard_Test, InhibitBlocksPaging)
{
    PlusDCard* card = CreateReady(true);
    ASSERT_NE(card, nullptr);
    EXPECT_TRUE(card->IsInhibited());
    card->BeforeMachineM1(0x0066);
    EXPECT_FALSE(card->IsPaged()) << "the NMI trap is inhibited";
    bool drove = false;
    In(0x00BB, drove);
    EXPECT_FALSE(card->IsPaged());

    Out(0x001F, PlusDCard::kDiscipleControlInhibitOverride);
    card->BeforeMachineM1(0x0066);
    EXPECT_TRUE(card->IsPaged()) << "bit 4 overrides the button";
    Out(0x001F, 0x00);
    EXPECT_FALSE(card->IsPaged()) << "a latch write without bit 4 pages out";

    card->SetInhibit(false);
    card->BeforeMachineM1(0x0066);
    EXPECT_TRUE(card->IsPaged()) << "released";
}

/// After /RESET the card is out, the memory not swapped, and for a few T-states no trap or #BB / #7B access counts:
/// the machine's own reset at #0000 / #0001 runs (~10 ms)
TEST_F(DiscipleCard_Test, ResetHoldsPagingOff)
{
    PlusDCard* card = CreateReady();
    ASSERT_NE(card, nullptr);
    bool drove = false;
    In(0x00BB, drove);
    Out(0x007B, 0x00);
    Out(0x001F, PlusDCard::kDiscipleControlDrive1);
    ASSERT_TRUE(card->IsPaged());

    card->BusReset();
    EXPECT_FALSE(card->IsPaged());
    EXPECT_FALSE(card->IsMemorySwapped());
    EXPECT_TRUE(card->IsResetHeld());
    EXPECT_EQ(card->Control(), PlusDCard::kDiscipleControlDrive1) << "the latch is not on /RESET";

    card->BeforeMachineM1(0x0000);  // the reset vector starts the hold-off
    Cpu()->t += 4;
    card->BeforeMachineM1(0x0001);
    EXPECT_FALSE(card->IsPaged()) << "the trap at #0001 is held off";
    In(0x00BB, drove);
    EXPECT_FALSE(card->IsPaged()) << "#BB is held off";

    Cpu()->t += PlusDCard::kResetHoldTStates;
    card->BeforeMachineM1(0x0001);
    EXPECT_FALSE(card->IsResetHeld());
    EXPECT_TRUE(card->IsPaged()) << "after the hold-off the trap pages in";
}

/// The 48K boots to BASIC with the card fitted: the machine's reset runs, GDOS gets in at its traps and goes out
TEST_F(DiscipleCard_Test, Boots48KToBasic)
{
    Create("48K", {{"edge.1", "disciple"}});
    PlusDCard* card = Card();
    ASSERT_NE(card, nullptr);
    _emulator->Reset();
    ASSERT_TRUE(EmulatorTestHelper::RunUntilBASICReady(_emulator, 300)) << "pc=" << std::hex << Cpu()->pc;
    EmulatorTestHelper::RunFramesFast(_emulator, 4);
    EXPECT_FALSE(card->IsResetHeld());
    EXPECT_FALSE(card->IsPaged()) << "pc=" << std::hex << Cpu()->pc;
}

/// The 128K boots to its menu with the card fitted
TEST_F(DiscipleCard_Test, Boots128KToMenu)
{
    Create("128K-DISCIPLE", {});
    PlusDCard* card = Card();
    ASSERT_NE(card, nullptr);
    _emulator->Reset();
    ASSERT_TRUE(EmulatorTestHelper::RunUntilBASICReady(_emulator, 300)) << "pc=" << std::hex << Cpu()->pc;
    EXPECT_FALSE(card->IsPaged());
}

/// state fdc names the model and its own fields (~10 ms)
TEST_F(DiscipleCard_Test, StateFdcNamesTheModel)
{
    Create("48K-DISCIPLE", {});  // no Beta 128 on the board: the card's controller is the machine's
    PlusDCard* card = Card();
    ASSERT_NE(card, nullptr);
    EndHold(card);
    const StateNode report = DeviceState::Fdc(_context);
    ASSERT_NE(report.find("controller"), nullptr);
    EXPECT_EQ(report.find("controller")->s, "WD1772 (MGT DISCiPLE)");
    ASSERT_NE(report.find("card"), nullptr);
    EXPECT_EQ(report.find("card")->s, "disciple");
    ASSERT_NE(report.find("memory_swapped"), nullptr);
    ASSERT_NE(report.find("reset_hold"), nullptr);
    EXPECT_FALSE(report.find("memory_swapped")->b);
    EXPECT_FALSE(report.find("reset_hold")->b);
    const StateNode* drives = report.find("drives");
    ASSERT_NE(drives, nullptr);
    ASSERT_EQ(drives->items.size(), 2u);
    EXPECT_EQ(drives->items[0].find("media_slot")->s, "fdd.disciple1");
}
