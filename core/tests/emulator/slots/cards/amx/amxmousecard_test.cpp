// AmxMouseCard (core/src/emulator/slots/cards/amx/amxmousecard.h): the AMX mouse in the edge slot of a 48K - its PIO
// on A7 = 0, the buttons on #DF, one IM 2 interrupt per mickey through the CPU's daisy chain (Z80::Im2Chain), RETI
// ending the service, the TTD blob (controller manager C11).

#include <gtest/gtest.h>

#include <functional>
#include <initializer_list>
#include <string>
#include <utility>
#include <vector>

#include "_helpers/emulatortesthelper.h"
#include "emulator/cpu/core.h"
#include "emulator/cpu/z80.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/io/controllers/controllermanager.h"
#include "emulator/io/mouse/mousemanager.h"
#include "emulator/memory/memory.h"
#include "emulator/ports/portdecoder.h"
#include "emulator/slots/card.h"
#include "emulator/slots/cards/amx/amxmousecard.h"
#include "emulator/slots/slotcontrol.h"
#include "emulator/slots/slotmanager.h"

using namespace controllers;

namespace
{

class AmxMouseCard_Test : public ::testing::Test
{
protected:
    Emulator* _emulator = nullptr;
    EmulatorContext* _context = nullptr;

    void TearDown() override
    {
        if (_emulator)
            EmulatorTestHelper::CleanupEmulator(_emulator);
    }

    void Create(const std::vector<std::pair<std::string, std::string>>& slots, const char* model = "48K")
    {
        std::function<void(CONFIG&)> slotSet;
        std::string error;
        ASSERT_TRUE(SlotControl::CreateOverride(slots, slotSet, error)) << error;
        _emulator = EmulatorTestHelper::CreateStandardEmulator(model, LoggerLevel::LogError, RamPowerOn::Zero, slotSet);
        ASSERT_NE(_emulator, nullptr);
        _context = _emulator->GetContext();
    }

    AmxMouseCard* Card() const
    {
        return dynamic_cast<AmxMouseCard*>(_context->pSlotManager->FindCard("edge.1"));
    }
    Z80* Cpu() const
    {
        return _context->pCore->GetZ80();
    }

    uint8_t In(uint16_t port, bool& drove)
    {
        return _context->pPortDecoder->ReadCycle(port, 0x8000, drove);
    }
    void Out(uint16_t port, uint8_t value)
    {
        _context->pPortDecoder->WriteCycle(port, value, 0x8000);
    }

    void Load(uint16_t address, std::initializer_list<uint8_t> bytes)
    {
        for (uint8_t b : bytes)
            _context->pMemory->DirectWriteToZ80Memory(address++, b);
    }
    uint8_t Peek(uint16_t address) const
    {
        return _context->pMemory->DirectReadFromZ80Memory(address);
    }

    /// An AMX Art style program (AMX Art 1.1's #815A): IM 2 with I = #FE, both PIO ports in mode 1 with vectors #F0
    /// (X) and #F2 (Y) and the interrupt control word #87; the X handler counts right / left mickeys at #9000 /
    /// #9001, the Y handler up / down at #9002 / #9003 (bit 0 of the data port, then EI : RETI); the frame interrupt
    /// (the idle bus's #FF) counts frames at #9010 and ends with EI : RET, as AMX Art's does
    void LoadAmxProgram()
    {
        Load(0x8000, {0xF3,                    // DI
                      0x31, 0x00, 0xC0,        // LD SP,#C000
                      0x3E, 0xFE, 0xED, 0x47,  // LD A,#FE : LD I,A
                      0xED, 0x5E,              // IM 2
                      0x3E, 0x4F, 0xD3, 0x5F, 0x3E, 0xF0, 0xD3, 0x5F, 0x3E, 0x87, 0xD3, 0x5F,
                      0x3E, 0x4F, 0xD3, 0x7F, 0x3E, 0xF2, 0xD3, 0x7F, 0x3E, 0x87, 0xD3, 0x7F,
                      0xFB,                    // EI
                      0x18, 0xFE});            // JR $
        auto handler = [&](uint16_t at, uint8_t port, uint16_t counters) {
            Load(at, {0xF5, 0xE5,                                                         // PUSH AF : PUSH HL
                      0xDB, port,                                                         // IN A,(port)
                      0x21, static_cast<uint8_t>(counters), static_cast<uint8_t>(counters >> 8),  // LD HL,counters
                      0xE6, 0x01, 0x28, 0x01, 0x23,                                       // AND 1 : JR Z,+1 : INC HL
                      0x34,                                                               // INC (HL)
                      0xE1, 0xF1, 0xFB, 0xED, 0x4D});                                     // POP HL : POP AF : EI : RETI
        };
        handler(0x8100, 0x1F, 0x9000);
        handler(0x8140, 0x3F, 0x9002);
        Load(0x8180, {0xF5, 0x3A, 0x10, 0x90, 0x3C, 0x32, 0x10, 0x90, 0xF1, 0xFB, 0xC9});
        Load(0xFEF0, {0x00, 0x81});
        Load(0xFEF2, {0x40, 0x81});
        Load(0xFEFF, {0x80, 0x81});
        for (uint16_t a = 0x9000; a <= 0x9010; a++)
            _context->pMemory->DirectWriteToZ80Memory(a, 0);
        Z80* cpu = Cpu();
        cpu->pc = 0x8000;
        cpu->halted = false;
    }
};

}  // namespace

TEST(AmxMouseCard_Buttons, HeldButtonsPullD7D6D5)
{
    EXPECT_EQ(AmxMouseCard::ButtonLines(0xFF), 0x00);
    EXPECT_EQ(AmxMouseCard::ButtonLines(0xFE), AmxMouseCard::kButtonLeft);
    EXPECT_EQ(AmxMouseCard::ButtonLines(0xFD), AmxMouseCard::kButtonRight);
    EXPECT_EQ(AmxMouseCard::ButtonLines(0xFB), AmxMouseCard::kButtonMiddle);
    EXPECT_EQ(AmxMouseCard::ButtonLines(0xF8), 0xE0);
}

/// A 48K with the card (~5 ms): the slot builds it, it brings the `amx` connector and the `amx` mouse device, and
/// its PIO and buttons answer by the schematic's decode
TEST_F(AmxMouseCard_Test, PortsByTheSchematicsDecode)
{
    Create({{"edge.1", "amx-mouse"}});
    AmxMouseCard* card = Card();
    ASSERT_NE(card, nullptr);

    const int index = _context->pControllerManager->Find("amx");
    ASSERT_GE(index, 0);
    const Connector connector = _context->pControllerManager->At(static_cast<size_t>(index));
    EXPECT_EQ(connector.kind, ConnectorKind::Amx);
    EXPECT_EQ(connector.device, RetroDeviceKind::AmxMouse);
    EXPECT_TRUE(connector.fixed);
    ASSERT_TRUE(_context->pMouseManager->DescribeDevice("amx").has_value());

    // Control A on #5F (and any port with A7 = 0, A6 = 1, A5 = 0), control B on #7F
    Out(0x005F, 0xF0);
    Out(0x125F, 0x4F);
    Out(0x0047, 0x87);
    EXPECT_EQ(card->Pio().GetPort(0).vector, 0xF0);
    EXPECT_TRUE(card->Pio().GetPort(0).ie);
    Out(0x007F, 0xF2);
    EXPECT_EQ(card->Pio().GetPort(1).vector, 0xF2);

    bool drove = false;
    In(0x005F, drove);
    EXPECT_FALSE(drove) << "the control registers are write-only";
    card->Pio().Strobe(0, 0x01);
    EXPECT_EQ(In(0x001F, drove), 0x01);
    EXPECT_TRUE(drove);
    EXPECT_EQ(In(0xFF03, drove), 0x01) << "A0-A4 and A8-A15 are not decoded";

    // The buttons: a held one pulls its line low, released ones drive nothing
    In(0x00DF, drove);
    EXPECT_FALSE(drove);
    _context->pMouseManager->ApplyButtons(0xFE);  // left held
    EXPECT_EQ(In(0x00DF, drove) & 0xE0, 0x60);
    EXPECT_TRUE(drove);
    EXPECT_EQ(In(0xFFC0, drove) & 0xE0, 0x60) << "A7-A5 = 110 selects them";
}

/// The program takes one IM 2 interrupt per mickey with the port's vector, the direction on bit 0, and RETI ends each
/// service; the frame interrupt keeps the idle bus's #FF. Two frames of a 48K (~10 ms)
TEST_F(AmxMouseCard_Test, OneInterruptPerMickeyThroughTheDaisyChain)
{
    Create({{"edge.1", "amx-mouse"}});
    AmxMouseCard* card = Card();
    ASSERT_NE(card, nullptr);
    LoadAmxProgram();
    _emulator->RunFrame(true);
    ASSERT_TRUE(card->Pio().GetPort(0).ie && card->Pio().GetPort(1).ie) << "the program set up the PIO";
    EXPECT_FALSE(_context->HasStepWork(EmulatorContext::kStepWorkDeviceInt)) << "a still mouse costs nothing";

    _context->pMouseManager->ApplyMotion(5, -3);  // 5 right, 3 down
    EXPECT_TRUE(_context->HasStepWork(EmulatorContext::kStepWorkDeviceInt));
    _emulator->RunFrame(true);
    _emulator->RunFrame(true);

    EXPECT_EQ(Peek(0x9000), 5) << "right";
    EXPECT_EQ(Peek(0x9001), 0) << "left";
    EXPECT_EQ(Peek(0x9002), 0) << "up";
    EXPECT_EQ(Peek(0x9003), 3) << "down";
    EXPECT_GE(Peek(0x9010), 2) << "the frame interrupts still come with #FF";
    EXPECT_EQ(card->Strobes(), 8u);
    EXPECT_FALSE(card->Pio().GetPort(0).ius || card->Pio().GetPort(1).ius) << "RETI ended every service";
    EXPECT_FALSE(_context->HasStepWork(EmulatorContext::kStepWorkDeviceInt)) << "drained: the chain disarms";

    _context->pMouseManager->ApplyMotion(-2, 1);
    _emulator->RunFrame(true);
    EXPECT_EQ(Peek(0x9001), 2) << "left";
    EXPECT_EQ(Peek(0x9002), 1) << "up";
}

/// At most kStrobeDivider strobes per axis and frame: a big move is spread over frames, nothing is lost
TEST_F(AmxMouseCard_Test, ABigMoveIsPacedNotLost)
{
    Create({{"edge.1", "amx-mouse"}});
    AmxMouseCard* card = Card();
    ASSERT_NE(card, nullptr);
    LoadAmxProgram();
    _emulator->RunFrame(true);
    _context->pMouseManager->ApplyMotion(100, 0);
    _emulator->RunFrame(true);
    EXPECT_LE(Peek(0x9000), AmxMouseCard::kStrobeDivider + 1);
    EXPECT_GE(Peek(0x9000), AmxMouseCard::kStrobeDivider - 1);
    for (int i = 0; i < 4 && card->Pending(0) != 0; i++)
        _emulator->RunFrame(true);
    EXPECT_EQ(Peek(0x9000), 100);
    EXPECT_EQ(card->Pending(0), 0);
}

/// Interrupts off: one strobe per axis waits as a pending interrupt, the rest stays in the counters
TEST_F(AmxMouseCard_Test, WithInterruptsOffTheMotionWaits)
{
    Create({{"edge.1", "amx-mouse"}});
    AmxMouseCard* card = Card();
    ASSERT_NE(card, nullptr);
    _context->pMouseManager->ApplyMotion(3, 0);
    Cpu()->Im2Chain().IsIntAsserted(Cpu()->t);
    EXPECT_TRUE(card->Pio().GetPort(0).ip);
    EXPECT_EQ(card->Pending(0), 2);
    EXPECT_FALSE(_context->HasStepWork(EmulatorContext::kStepWorkDeviceInt)) << "nothing can raise /INT now";
    EXPECT_FALSE(card->IsMouseInUse());
}

TEST_F(AmxMouseCard_Test, TtdBlobRoundTrip)
{
    Create({{"edge.1", "amx-mouse"}});
    AmxMouseCard* card = Card();
    ASSERT_NE(card, nullptr);
    std::vector<CardTtdDevice> devices;
    card->CollectTtdDevices(devices);
    ASSERT_EQ(devices.size(), 1u);
    EXPECT_EQ(devices[0].instance, "edge.1.amx-mouse");
    ttd::TTDSerializable* blob = devices[0].device;
    EXPECT_EQ(blob->TTDType(), ttd::TTDDeviceType::AmxMouse);
    ASSERT_EQ(blob->TTDStateSize(), AmxMouseCard::kStateSize);

    Out(0x005F, 0xF0);
    Out(0x005F, 0x87);
    _context->pMouseManager->ApplyMotion(-7, 4);
    _context->pMouseManager->ApplyButtons(0xFB);
    std::vector<uint8_t> saved(blob->TTDStateSize());
    blob->TTDSaveState(saved.data());
    const uint64_t hash = blob->TTDHashState();

    _context->pMouseManager->ApplyMotion(20, 20);
    card->BusReset();
    EXPECT_NE(blob->TTDHashState(), hash);
    blob->TTDLoadState(saved.data());
    EXPECT_EQ(blob->TTDHashState(), hash);
    EXPECT_EQ(card->Pending(0), -7);
    EXPECT_EQ(card->Pending(1), 4);
    EXPECT_EQ(card->Pio().GetPort(0).vector, 0xF0);
    EXPECT_TRUE(_context->HasStepWork(EmulatorContext::kStepWorkDeviceInt)) << "the restore re-arms the chain";
}

/// No card: the chain is empty, the plain step
TEST_F(AmxMouseCard_Test, WithoutTheCardTheChainIsEmpty)
{
    Create({});
    EXPECT_TRUE(Cpu()->Im2Chain().Empty());
    EXPECT_LT(_context->pControllerManager->Find("amx"), 0);
}
