// The Sprinter's network card in its ISA slot (network tdd §15 T-NET-7, the slot part of T-NET-14): the default
// population (NE2000 RTL8019AS in slot 2 at #300), the card reached through the ISA bus, the slot rows of the network
// report and the ISA report's resources, ZX-bus cards refused, another population, the network feature, the UM9003's
// hanging reset port, the access journal

#include <gtest/gtest.h>

#include <memory>
#include <string>

#include "base/featuremanager.h"
#include "emulator/cpu/core.h"
#include "emulator/cpu/z80.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/emulatormanager.h"
#include "emulator/io/network/networkmanager.h"
#include "emulator/io/sprinter/isa/isaaccess.h"
#include "emulator/io/sprinter/isa/sprinterisabus.h"
#include "emulator/ports/models/portdecoder_sprinter.h"
#include "emulator/state/devicestate.h"

class SprinterNetwork_Test : public ::testing::Test
{
protected:
    EmulatorManager* _manager = nullptr;
    std::shared_ptr<Emulator> _emulator;
    EmulatorContext* _context = nullptr;
    PortDecoder_Sprinter* _decoder = nullptr;

    void SetUp() override { _manager = EmulatorManager::GetInstance(); }
    void TearDown() override
    {
        _emulator.reset();
        for (const auto& id : _manager->GetEmulatorIds())
            _manager->RemoveEmulator(id);
    }

    void Create(std::function<void(CONFIG&)> configOverride = {})
    {
        _emulator = _manager->CreateEmulatorWithModelAndRAM("sprinter-net", "SPRINTER", 4096, LoggerLevel::LogError,
                                                            nullptr, std::move(configOverride));
        ASSERT_NE(_emulator, nullptr);
        _context = _emulator->GetContext();
        _decoder = dynamic_cast<PortDecoder_Sprinter*>(_context->pPortDecoder);
        ASSERT_NE(_decoder, nullptr);
    }

    NetworkManager* Network() { return _context->pCore->GetNetworkManager(); }

    uint8_t IsaIo(const std::string& action, int slot, uint32_t address, int value = -1)
    {
        StateNode result;
        std::string error;
        EXPECT_TRUE(IsaAccess::Execute(_context, action, slot, address, value, "test", result, error)) << error;
        const StateNode* v = result.find("value");
        return v ? static_cast<uint8_t>(std::strtoul(v->s.c_str() + 1, nullptr, 16)) : 0;
    }
};

TEST_F(SprinterNetwork_Test, DefaultPopulation_Ne2000InSlot2)
{
    Create();
    ASSERT_EQ(Network()->SlotCards().size(), 1u);
    EXPECT_EQ(Network()->SlotCards()[0].slotId, "isa2");
    ASSERT_NE(_decoder->GetIsaBus().Card(1), nullptr);
    EXPECT_STREQ(_decoder->GetIsaBus().Card(1)->Kind(), "ne2000");
    ASSERT_NE(_decoder->GetIsaBus().Card(0), nullptr);
    EXPECT_STREQ(_decoder->GetIsaBus().Card(0)->Kind(), "zxbus") << "slot 1: the ZX-bus adapter (ISA I2)";

    EXPECT_EQ(IsaIo("io_read", 2, 0x30A), 0x50) << "RTL8019AS ID 'P' through the ISA bus";
    EXPECT_EQ(IsaIo("io_read", 2, 0x30B), 0x70);
    EXPECT_EQ(IsaIo("io_read", 1, 0x30A), 0xFF) << "slot 1: the GS decodes #33 / #B3 / #BB only";
    EXPECT_EQ(IsaIo("io_read", 2, 0x20A), 0xFF) << "nothing at #200";

    const Ne2000Board* card = Network()->EthernetCard("isa2.eth");
    ASSERT_NE(card, nullptr);
    uint8_t mac[6];
    card->StationMac(mac);
    EXPECT_EQ(mac[0], 0x02);
    EXPECT_EQ(mac[1], 0x53);
    EXPECT_EQ(mac[2], 0x50);
    EXPECT_EQ(mac[5], 0x02) << "automatic MAC: 02:53:50:00:<instance>:<slot>";
}

TEST_F(SprinterNetwork_Test, Reports_SlotRowsResourcesAndHowTheZ80ReachesThem)
{
    Create();
    Network()->OnFrame();
    const StateNode net = DeviceState::Network(_context);
    const StateNode* slots = net.find("slots");
    ASSERT_NE(slots, nullptr);
    ASSERT_EQ(slots->items.size(), 2u);
    const StateNode& row = slots->items[1];
    EXPECT_EQ(row.find("id")->s, "isa2");
    EXPECT_EQ(row.find("card")->s, "ne2000");
    EXPECT_EQ(row.find("chip")->s, "RTL8019AS");
    EXPECT_EQ(row.find("base")->s, "#300");
    ASSERT_NE(row.find("registers"), nullptr);
    EXPECT_EQ(slots->items[0].find("card")->s, "none");
    EXPECT_FALSE(net.find("machine")->find("zx_bus")->b) << "no ZX-bus for network cards (the adapter passes no memory cycles, Q7)";

    const StateNode isa = DeviceState::Isa(_context);
    const StateNode& slot2 = isa.find("slots")->items[1];
    EXPECT_EQ(slot2.find("resources")->find("io")->s, "#300-#31F");
    EXPECT_EQ(slot2.find("resources")->find("irq")->i, 3);
    EXPECT_NE(slot2.find("z80_access")->find("io")->s.find("page #D6"), std::string::npos);
    EXPECT_NE(slot2.find("z80_access")->find("io")->s.find("#C300-#C31F"), std::string::npos);
    EXPECT_EQ(slot2.find("z80_access")->find("io_now")->s, "yes");
    EXPECT_TRUE(isa.find("conflicts")->items.empty());
    EXPECT_NE(isa.find("summary")->s.find("slot 2: ne2000 I/O #300-#31F IRQ 3"), std::string::npos) << isa.find("summary")->s;

    // The Sprinter report carries the same summary
    const StateNode sprinter = DeviceState::Sprinter(_context);
    EXPECT_EQ(sprinter.find("isa")->find("summary")->s, isa.find("summary")->s);
}

TEST_F(SprinterNetwork_Test, Journal_NamesTheRegisters)
{
    Create();
    _decoder->GetIsaBus().ClearJournal();
    IsaIo("io_write", 2, 0x300, 0x21);
    IsaIo("io_read", 2, 0x307);
    IsaIo("io_read", 2, 0x310);
    const StateNode journal = DeviceState::IsaJournal(_context, 0);
    const StateNode* entries = journal.find("entries");
    ASSERT_NE(entries, nullptr);
    ASSERT_EQ(entries->items.size(), 3u);
    EXPECT_EQ(entries->items[0].find("what")->s, "CR");
    EXPECT_EQ(entries->items[0].find("access")->s, "write");
    EXPECT_EQ(entries->items[1].find("what")->s, "ISR");
    EXPECT_EQ(entries->items[2].find("what")->s, "data port");
    EXPECT_EQ(entries->items[1].find("cpu_address")->s, "#C307");
}

TEST_F(SprinterNetwork_Test, ZxBusCardsAreRefusedWithTheReason)
{
    Create([](CONFIG& config) { config.network.card = 1; });
    EXPECT_EQ(_context->pZxNetUsb, nullptr);
    const StateNode net = DeviceState::Network(_context);
    const StateNode* notes = net.find("not_fitted");
    ASSERT_NE(notes, nullptr);
    bool found = false;
    for (const StateNode& n : notes->items)
        found |= n.s.find("no ZX-Bus") != std::string::npos;
    EXPECT_TRUE(found);
}

TEST_F(SprinterNetwork_Test, OtherPopulations)
{
    Create([](CONFIG& config) { config.sprinter.isa.slot[1].kind = 0; });
    EXPECT_TRUE(Network()->SlotCards().empty());
    EXPECT_EQ(_decoder->GetIsaBus().Card(1), nullptr);
    TearDown();

    Create([](CONFIG& config) {
        config.sprinter.isa.slot[0] = config.sprinter.isa.slot[1];
        config.sprinter.isa.slot[0].base = 0x340;
        config.sprinter.isa.slot[1].kind = static_cast<uint8_t>(sprinterisa::CardKind::SprinterEsp);
    });
    ASSERT_EQ(Network()->SlotCards().size(), 2u);
    EXPECT_EQ(IsaIo("io_read", 1, 0x34A), 0x50) << "the NE2000 at #340 in slot 1";
    ASSERT_NE(Network()->SerialCard("isa2"), nullptr) << "the SprinterESP in slot 2 (network phase SN3)";
    IsaIo("io_write", 2, 0x3EF, 0x55);
    EXPECT_EQ(IsaIo("io_read", 2, 0x3EF), 0x55) << "its 16550's scratch register";
    const StateNode isa = DeviceState::Isa(_context);
    EXPECT_EQ(isa.find("slots")->items[1].find("not_fitted"), nullptr);
    EXPECT_EQ(isa.find("slots")->items[1].find("card")->s, "sprinteresp");
}

TEST_F(SprinterNetwork_Test, TheCardIsHardware_NetworkFeatureOffKeepsIt)
{
    Create();
    FeatureManager* features = _emulator->GetFeatureManager();
    ASSERT_TRUE(features->setFeature(Features::kNetwork, false));
    EXPECT_EQ(Network()->SlotCards().size(), 1u);
    EXPECT_EQ(IsaIo("io_read", 2, 0x30A), 0x50);
    features->setFeature(Features::kNetwork, true);
}

TEST_F(SprinterNetwork_Test, Um9003ResetPortHangsTheCpu)
{
    Create([](CONFIG& config) { config.sprinter.isa.slot[1].chip = static_cast<uint8_t>(sprinterisa::Ne2000Chip::Um9003); });
    EXPECT_EQ(IsaIo("io_read", 2, 0x30A), 0x20) << "the UMC ID";
    Z80* z80 = _context->pCore->GetZ80();
    z80->iff1 = z80->iff2 = 1;
    IsaIo("io_read", 2, 0x31F);
    EXPECT_EQ(z80->halted, 1) << "the CPU waits for an ISA cycle that never ends";
    EXPECT_EQ(z80->iff1, 0);
    const StateNode isa = DeviceState::Isa(_context);
    EXPECT_NE(isa.find("slots")->items[1].find("stalled"), nullptr);
    // RESET DRV ends it
    IsaIo("reset", 0, 0);
    EXPECT_FALSE(Network()->EthernetCard("isa2.eth")->Stalled());
}

/// region <ISA interrupt lines (ISA phase I4)>

// The default population: the RTL8019AS in slot 2 drives its IRQ pin (CONFIG1.IRQEN set at power-up) low, slot 1 is
// empty (pulled up): PIO port B reads PB1 = 0, PB0 = 1. The report says where each line goes and why it does not
// interrupt (no PB bit is monitored: the BIOS programs port B for the printer bits only)
TEST_F(SprinterNetwork_Test, IrqLines_DefaultPopulationOnPioPortB)
{
    Create();
    EXPECT_EQ(_decoder->GetZ84().pio.GetPort(1).inputs, 0xFD);
    const StateNode isa = DeviceState::Isa(_context);
    const StateNode* slot2 = isa.find("slots")->items[1].find("irq_line");
    ASSERT_NE(slot2, nullptr);
    EXPECT_EQ(slot2->find("pio_bit")->s, "PB1");
    EXPECT_EQ(slot2->find("line")->s, "low");
    EXPECT_NE(slot2->find("driven")->s.find("ne2000"), std::string::npos);
    EXPECT_NE(slot2->find("cause")->s.find("IRQEN set"), std::string::npos) << slot2->find("cause")->s;
    EXPECT_EQ(slot2->find("reaches_cpu")->s.rfind("no:", 0), 0u) << slot2->find("reaches_cpu")->s;
    EXPECT_EQ(isa.find("slots")->items[0].find("irq_line")->find("line")->s, "high") << "the ZX-bus adapter drives no IRQ: the pull-up";

    // IRQEN cleared through page 3 (9346CR config mode): the pin floats, the pull-up wins
    IsaIo("io_write", 2, 0x300, 0xE1);   // page 3
    IsaIo("io_write", 2, 0x301, 0xC0);   // 9346CR: config register write enable
    IsaIo("io_write", 2, 0x304, 0x00);   // CONFIG1: IRQEN = 0
    EXPECT_EQ(_decoder->GetZ84().pio.GetPort(1).inputs, 0xFF);
    EXPECT_GE(_decoder->GetIsaBus().GetCounters(1).irqRises, 1u);
}

namespace
{
/// IM 2 at I = #90: vector #00 (PIO port B) -> #8100 reads the 16550's RBR into #A000 and counts in #A001; every
/// other vector (the PLD's frame INT answers #FF) -> #8282 counts in #A002. Main: map ISA slot 1 I/O into window 3
/// (#1FFD = #11, page #D4), EI, HALT loop
void LoadIrqProgram(Memory* memory)
{
    const std::vector<uint8_t> main = {0xF3, 0x3E, 0x90, 0xED, 0x47, 0xED, 0x5E, 0x01, 0xFD, 0x1F, 0x3E, 0x11,
                                       0xED, 0x79, 0x3E, 0xD4, 0xD3, 0xE2, 0xFB, 0x76, 0x18, 0xFD};
    const std::vector<uint8_t> pioHandler = {0xF5, 0x3A, 0xE8, 0xC3, 0x32, 0x00, 0xA0, 0x3A, 0x01, 0xA0, 0x3C,
                                             0x32, 0x01, 0xA0, 0xF1, 0xFB, 0xED, 0x4D};
    const std::vector<uint8_t> otherHandler = {0xF5, 0x3A, 0x02, 0xA0, 0x3C, 0x32, 0x02, 0xA0, 0xF1, 0xFB, 0xED, 0x4D};
    for (size_t i = 0; i < main.size(); ++i)
        memory->DirectWriteToZ80Memory(static_cast<uint16_t>(0x8000 + i), main[i]);
    for (size_t i = 0; i < pioHandler.size(); ++i)
        memory->DirectWriteToZ80Memory(static_cast<uint16_t>(0x8100 + i), pioHandler[i]);
    for (size_t i = 0; i < otherHandler.size(); ++i)
        memory->DirectWriteToZ80Memory(static_cast<uint16_t>(0x8282 + i), otherHandler[i]);
    for (uint16_t a = 0x9000; a <= 0x9101; ++a)
        memory->DirectWriteToZ80Memory(a, 0x82);
    memory->DirectWriteToZ80Memory(0x9000, 0x00);
    memory->DirectWriteToZ80Memory(0x9001, 0x81);
    for (uint16_t a = 0xA000; a < 0xA003; ++a)
        memory->DirectWriteToZ80Memory(a, 0);
}
}  // namespace

// T-ISA I4: the SprinterESP's 16550 in slot 1 (INTR to IRQ3, not gated by OUT2) interrupts the CPU through PIO port B
// bit 0 in bit-control mode, BC-Term's setup (#00 vector, #CF, #01, #B7, #FE, #83). A character sent in loopback lands
// one character time after the THR write - the line rises then, mid-frame, not at the frame end; the PIO requests, the
// CPU takes IM 2 vector #00 ahead of the frame INT, the handler reads RBR (the line falls) and RETI ends the service.
// (The frame INT beside it: the BC-Term test on the real BIOS and DSS)
TEST_F(SprinterNetwork_Test, IrqLines_UartReceiveInterruptsTheCpuThroughPioPortB)
{
    Create([](CONFIG& config) {
        config.sprinter.isa.slot[0].kind = static_cast<uint8_t>(sprinterisa::CardKind::SprinterEsp);
        config.sprinter.fast_start = 1;   // the configured PLD at once (no ROM loader writing into its sink)
    });
    _emulator->Reset();
    _emulator->RunNFrames(30, true);   // the BIOS has set the machine up
    ASSERT_EQ(_decoder->GetPldState().configState, SprinterConfigState::Configured);

    // The 16550: 115 200 baud (divisor 8 at 14.7456 MHz), 8N1, FIFO with trigger 1, the receive interrupt, loopback
    for (auto [reg, value] : std::vector<std::pair<uint32_t, int>>{{0x3EB, 0x80}, {0x3E8, 8}, {0x3E9, 0}, {0x3EB, 3},
                                                                   {0x3EA, 0x07}, {0x3E9, 0x01}, {0x3EC, 0x10}})
        IsaIo("io_write", 1, reg, value);
    EXPECT_EQ(_decoder->GetZ84().pio.GetPort(1).inputs & 0x01, 0) << "INTR low: the 16550 drives IRQ3";

    LoadIrqProgram(_context->pMemory);
    for (uint8_t v : {0x00, 0xCF, 0x01, 0xB7, 0xFE, 0x83})
        _decoder->DecodePortOut(0x001F, v, 0);
    EXPECT_EQ(_decoder->IsaLineDeadline(), UINT64_MAX) << "armed, but nothing on the line";
    Z80* z80 = _context->pCore->GetZ80();
    z80->pc = 0x8000;
    z80->sp = 0x8FF0;
    z80->iff1 = z80->iff2 = 0;
    z80->halted = 0;
    _emulator->RunNFrames(3, true);
    ASSERT_EQ(z80->im, 2);
    EXPECT_EQ(_context->pMemory->DirectReadFromZ80Memory(0xA001), 0) << "no character yet";

    SprinterIsaBus& bus = _decoder->GetIsaBus();
    bus.ClearJournal();
    const SprinterIsaBus::Counters before = bus.GetCounters(0);
    IsaIo("io_write", 1, 0x3E8, 0x41);   // THR: 'A' goes round the loop
    EXPECT_NE(_decoder->IsaLineDeadline(), UINT64_MAX) << "a character on the line while the PIO waits";
    _emulator->RunNFrames(2, true);

    EXPECT_EQ(_context->pMemory->DirectReadFromZ80Memory(0xA000), 0x41) << "the handler read RBR";
    EXPECT_EQ(_context->pMemory->DirectReadFromZ80Memory(0xA001), 1) << "one PIO interrupt";
    const SprinterIsaBus::Counters& c = bus.GetCounters(0);
    EXPECT_EQ(c.irqRises - before.irqRises, 1u);
    EXPECT_EQ(c.irqFalls - before.irqFalls, 1u);
    EXPECT_EQ(c.pioRequests - before.pioRequests, 1u);
    EXPECT_EQ(c.acknowledged - before.acknowledged, 1u);
    EXPECT_EQ(c.serviceEnds - before.serviceEnds, 1u);
    EXPECT_EQ(_decoder->GetZ84().pio.GetPort(1).ius, 0);

    // The journal: THR write, line high one character time later, request, acknowledge, RBR read, line low, RETI
    const uint32_t frameLength = _context->config.frame;
    int64_t writeAt = -1, riseAt = -1;
    std::vector<std::string> irqEvents;
    for (const SprinterIsaBus::JournalEntry& e : bus.Journal())
    {
        const int64_t at = static_cast<int64_t>(e.frame) * frameLength + e.t;
        if (!e.irq && e.write && e.what == "THR")
            writeAt = at;
        if (e.irq)
        {
            irqEvents.push_back(e.what);
            if (riseAt < 0 && e.what.rfind("IRQ line high", 0) == 0)
                riseAt = at;
        }
    }
    ASSERT_GE(writeAt, 0);
    ASSERT_GE(riseAt, 0);
    // 10 bits at 115 200 baud = 86.8 us = 304 base T-states; the line reaches PB0 after the instruction that crossed it
    EXPECT_GE(riseAt - writeAt, 303);
    EXPECT_LE(riseAt - writeAt, 330);
    std::string all;
    for (const std::string& e : irqEvents)
        all += e + "\n";
    ASSERT_GE(irqEvents.size(), 5u) << all << "PC #" << std::hex << z80->pc << " A001 " << int(_context->pMemory->DirectReadFromZ80Memory(0xA001));
    EXPECT_EQ(irqEvents[0].rfind("IRQ line high -> PB0 (IIR #C4", 0), 0u) << irqEvents[0];
    EXPECT_EQ(irqEvents[1].rfind("PIO port B requests an interrupt (vector #00; slot 1)", 0), 0u) << irqEvents[1];
    EXPECT_EQ(irqEvents[2].rfind("INT acknowledged: PIO port B, IM 2 vector #00 -> table #9000", 0), 0u) << irqEvents[2];
    EXPECT_EQ(irqEvents[3].rfind("IRQ line low -> PB0", 0), 0u) << irqEvents[3];
    EXPECT_EQ(irqEvents[4].rfind("RETI", 0), 0u) << irqEvents[4];
    EXPECT_EQ(_decoder->IsaLineDeadline(), UINT64_MAX);

    const StateNode isa = DeviceState::Isa(_context);
    EXPECT_NE(isa.find("irq_summary")->s.find("slot 1 IRQ low (driven by sprinteresp) -> PB0: interrupts the CPU; 1 "
                                              "requests, 1 acknowledged"),
              std::string::npos)
        << isa.find("irq_summary")->s;
    const StateNode journal = DeviceState::IsaJournal(_context, 0);
    bool irqEntry = false;
    for (const StateNode& e : journal.find("entries")->items)
        irqEntry |= e.find("event") && e.find("event")->s == "irq";
    EXPECT_TRUE(irqEntry);
}

// A mask written while the condition already holds requests at once (Z80 PIO: the logic equation turns true when the
// line becomes monitored); masked, disabled or in another mode the line never interrupts and nothing is polled
TEST_F(SprinterNetwork_Test, IrqLines_PioArmingRules)
{
    Create([](CONFIG& config) { config.sprinter.isa.slot[0].kind = static_cast<uint8_t>(sprinterisa::CardKind::SprinterEsp); });
    // Slot 1 driven low by the UART, slot 2 driven low by the NE2000; watch PB0 for an active LOW level: true at once
    for (uint8_t v : {0x00, 0xCF, 0x03, 0x97, 0xFE})   // #97: enabled, OR, active low, mask follows
        _decoder->DecodePortOut(0x001F, v, 0);
    EXPECT_EQ(_decoder->GetZ84().pio.GetPort(1).ip, 1) << "the condition held when PB0 became monitored";
    EXPECT_EQ(_decoder->GetIsaBus().GetCounters(0).pioRequests, 1u);

    // Priority and the acknowledge: the chip's chain drives the vector ahead of the PLD's INT, and the PLD presets its
    // INT flip-flop on the same /M1 + /IORQ cycle (SP2_1K30.TDF:744) - a keyboard INT latched meanwhile ends there
    _decoder->GetIntSource().LatchKeyboardInt();
    _decoder->DecodePortOut(0x001F, 0x00, 0);   // vector #00 (a vector word: bit 0 = 0)
    IInterruptSource* source = _context->pCore->GetZ80()->GetInterruptSource();
    ASSERT_NE(source, nullptr);
    ASSERT_TRUE(source->IsIntAsserted(_context->pCore->GetZ80()->t));
    EXPECT_EQ(source->AcknowledgeInterrupt(_context->pCore->GetZ80()->t), 0x00) << "PIO port B's vector, not the PLD's #FF";
    EXPECT_FALSE(_decoder->GetIntSource().KeyboardIntLatched()) << "the PLD saw the acknowledge";
    EXPECT_EQ(_decoder->GetZ84().pio.GetPort(1).ius, 1);
    EXPECT_EQ(_decoder->GetIsaBus().GetCounters(0).acknowledged, 1u);
    _decoder->GetZ84().OnReti();
    _decoder->OnChipReti();
    EXPECT_EQ(_decoder->GetIsaBus().GetCounters(0).serviceEnds, 1u);

    // Interrupts disabled: no deadline even with a character on the line
    _decoder->DecodePortOut(0x001F, 0x03, 0);
    IsaIo("io_write", 1, 0x3EC, 0x10);
    IsaIo("io_write", 1, 0x3E9, 0x01);
    IsaIo("io_write", 1, 0x3E8, 0x55);
    EXPECT_EQ(_decoder->IsaLineDeadline(), UINT64_MAX) << "nobody waits: the cards are not polled";
    _decoder->DecodePortOut(0x001F, 0x83, 0);
    EXPECT_NE(_decoder->IsaLineDeadline(), UINT64_MAX) << "enabled again: the character's arrival is a deadline";

    // A read of PIO port B data shows the line as it is now (the cards catch up first)
    const uint8_t now = _decoder->DecodePortIn(0x001E, 0);
    EXPECT_EQ(now & 0x01, _decoder->GetIsaBus().IrqLine(0) ? 1 : 0);
}

/// endregion
