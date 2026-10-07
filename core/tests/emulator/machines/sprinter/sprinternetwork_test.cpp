// The Sprinter's network card in its ISA slot (network tdd §15 T-NET-7, the slot part of T-NET-14): the default
// population (NE2000 RTL8019AS in slot 2 at #300), the card reached through the ISA bus, the slot rows of the network
// report and the ISA report's resources, ZX-bus cards refused, another population, the network feature, the UM9003's
// hanging reset port, the access journal; the 3C509B in slot 2 (network phase SN5)

#include <gtest/gtest.h>

#include <memory>
#include <string>

#include "base/featuremanager.h"
#include "emulator/cpu/core.h"
#include "emulator/cpu/z80.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/emulatormanager.h"
#include "emulator/io/network/ethernet/etherlink3.h"
#include "emulator/io/network/networkmanager.h"
#include "emulator/io/sprinter/isa/isaaccess.h"
#include "emulator/io/sprinter/isa/sprinterisabus.h"
#include "emulator/ports/models/portdecoder_sprinter.h"
#include "emulator/state/devicestate.h"
#include "debugger/ttd/timetravelcontroller.h"
#include "emulator/io/network/vnet/ethernetgateway.h"

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

    const IEthernetCard* card = Network()->EthernetCard("isa2.eth");
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

// The legacy [NETWORK] Card=ZXNETUSB becomes a slot behind the ISA ZX-bus adapter; the adapter passes the General
// Sound's ports only (ZX-bus slots SL-8), so the slot plan leaves the card out and its report says why
TEST_F(SprinterNetwork_Test, ZxBusCardsAreRefusedWithTheReason)
{
    Create([](CONFIG& config) { config.network.card = 1; });
    EXPECT_EQ(_context->pZxNetUsb, nullptr);
    const StateNode slots = DeviceState::Slots(_context);
    const StateNode* entries = slots.find("slots");
    ASSERT_NE(entries, nullptr);
    std::string reason;
    for (const StateNode& slot : entries->items)
        if (slot.find("card") && slot.find("card")->s == "zxnetusb" && slot.find("reason"))
            reason = slot.find("reason")->s;
    EXPECT_NE(reason.find("passes the General Sound's ports only"), std::string::npos) << reason;
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

// Network phase SN5: the 3C509B in slot 2 ([ISA] Slot2=EL3C509B) - found through its ID port, activated at the
// EEPROM's base, its window 0 seen through the ISA bus; the reports show the ID port beside the I/O range, the IRQ line
// stays with the pull-up until a driver sets ENA and leaves window 0; the journal names the registers by window
TEST_F(SprinterNetwork_Test, El3c509b_InSlot2_IsolationActivationAndReports)
{
    Create([](CONFIG& config) {
        config.sprinter.isa.slot[1].kind = static_cast<uint8_t>(sprinterisa::CardKind::El3c509b);
        config.sprinter.isa.slot[1].chip = static_cast<uint8_t>(sprinterisa::El3Chip::Tpo);
    });
    ASSERT_EQ(Network()->SlotCards().size(), 1u);
    ASSERT_NE(_decoder->GetIsaBus().Card(1), nullptr);
    EXPECT_STREQ(_decoder->GetIsaBus().Card(1)->Kind(), "el3c509b");
    auto* card = dynamic_cast<EtherLink3*>(Network()->EthernetCard("isa2.eth"));
    ASSERT_NE(card, nullptr);
    ASSERT_NE(_context->pEthernetGateway, nullptr) << "a frame-level card: the gateway is its wire";
    auto later = [&](uint64_t t) { _context->emulatorState.t_states += t; };
    later(2000);   // past the EEPROM's autoload (310 us)

    EXPECT_EQ(IsaIo("io_read", 2, 0x30E), 0xFF) << "not active yet: nothing at #300";
    IsaIo("io_write", 2, 0x110, 0x00);
    IsaIo("io_write", 2, 0x110, 0x00);
    for (int i = 0; i < 255; ++i)
        IsaIo("io_write", 2, 0x110, EtherLink3::IdSequenceByte(i));
    IsaIo("io_write", 2, 0x110, 0xD0);
    IsaIo("io_write", 2, 0x110, 0x87);
    later(700);   // 162 us
    uint16_t word = 0;
    for (int i = 0; i < 16; ++i)
        word = static_cast<uint16_t>((word << 1) | (IsaIo("io_read", 2, 0x110) & 1));
    EXPECT_EQ(word, 0x6D50) << "the manufacturer ID, bit by bit through the ID port";
    EXPECT_EQ(IsaIo("io_read", 1, 0x110), 0xFF) << "slot 1 is empty: its ID port is nobody's";
    IsaIo("io_write", 2, 0x110, 0xFF);
    EXPECT_EQ(IsaIo("io_read", 2, 0x300), 0x50);
    EXPECT_EQ(IsaIo("io_read", 2, 0x301), 0x6D);
    EXPECT_EQ(IsaIo("io_read", 2, 0x302), 0x50);
    EXPECT_EQ(IsaIo("io_read", 2, 0x303), 0x95) << "3C509B-TPO";
    EXPECT_EQ(IsaIo("io_read", 2, 0x700), 0xFF) << "A15-A0 decoded: no 1 KB mirror";

    Network()->OnFrame();
    const StateNode net = DeviceState::Network(_context);
    const StateNode& row = net.find("slots")->items[1];
    EXPECT_EQ(row.find("card")->s, "el3c509b");
    EXPECT_EQ(row.find("chip")->s, "3C509B-TPO");
    EXPECT_EQ(row.find("base")->s, "#300");
    EXPECT_EQ(row.find("id_port")->s, "#110");
    EXPECT_TRUE(row.find("activated")->b);
    EXPECT_EQ(row.find("link")->s, "ethernet-gateway");

    const StateNode isa = DeviceState::Isa(_context);
    const StateNode& slot2 = isa.find("slots")->items[1];
    EXPECT_EQ(slot2.find("resources")->find("io")->s, "#300-#30F");
    const StateNode* id = slot2.find("resources")->find("id_port");
    ASSERT_NE(id, nullptr);
    EXPECT_EQ(id->find("io")->s, "#100-#1F0 step #10");
    EXPECT_NE(id->find("z80")->s.find("#C100-#C1F0"), std::string::npos) << id->find("z80")->s;
    EXPECT_NE(slot2.find("z80_access")->find("io")->s.find("#C300-#C30F"), std::string::npos);
    EXPECT_NE(isa.find("summary")->s.find("slot 2: el3c509b I/O #300-#30F id_port #100-#1F0 IRQ 3"), std::string::npos)
        << isa.find("summary")->s;

    // The IRQ pin: ENA clear at power-up - the pull-up holds PB1 high; ENA set in window 1 - driven low (no cause)
    EXPECT_EQ(_decoder->GetZ84().pio.GetPort(1).inputs & 0x02, 0x02);
    IsaIo("io_write", 2, 0x304, 0x01);
    IsaIo("io_write", 2, 0x305, 0x00);
    IsaIo("io_write", 2, 0x30E, 0x01);
    IsaIo("io_write", 2, 0x30F, 0x08);   // window 1
    EXPECT_EQ(_decoder->GetZ84().pio.GetPort(1).inputs & 0x02, 0x00) << "the 3C509B drives IRQ 3 low";

    _decoder->GetIsaBus().ClearJournal();
    IsaIo("io_read", 2, 0x308);
    IsaIo("io_write", 2, 0x300, 0x2A);
    IsaIo("io_write", 2, 0x110, 0x00);
    const StateNode journal = DeviceState::IsaJournal(_context, 0);
    const StateNode* entries = journal.find("entries");
    ASSERT_EQ(entries->items.size(), 3u);
    EXPECT_EQ(entries->items[0].find("what")->s, "RX status");
    EXPECT_EQ(entries->items[1].find("what")->s, "TX PIO data");
    EXPECT_EQ(entries->items[2].find("what")->s, "ID port");
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

// --- Bridge mode (network SN6) --------------------------------------------------------------------------------------

namespace
{
/// The host adapter without a host: what the cards send is kept, what the "LAN" sends is queued by the test
class FakeHostFrames : public IHostFrames
{
public:
    std::vector<HostAdapter> Adapters(std::string&) override
    {
        HostAdapter a;
        a.name = "fake0";
        a.up = a.running = true;
        return {a};
    }
    bool Open(const std::string& adapter, std::string& error) override
    {
        if (adapter != "fake0")
        {
            error = "no host adapter '" + adapter + "'";
            return false;
        }
        open = true;
        return true;
    }
    void Close() override { open = false; }
    bool IsOpen() const override { return open; }
    std::string Adapter() const override { return open ? "fake0" : ""; }
    void SetStations(const std::vector<Mac>& s) override { stations = s; }
    bool Translates() const override { return false; }
    Mac HostMac() const override { return {}; }
    void SetGuestIps(const std::vector<uint32_t>&) override {}
    void Send(const uint8_t* frame, size_t length) override { sent.emplace_back(frame, frame + length); }
    void Drain(std::vector<std::vector<uint8_t>>& out) override
    {
        for (auto& f : incoming)
            out.push_back(std::move(f));
        incoming.clear();
    }
    Counters GetCounters() const override { return {}; }
    std::string LastError() const override { return {}; }
    std::string Library() const override { return "fake"; }

    bool open = false;
    std::vector<Mac> stations;
    std::vector<std::vector<uint8_t>> sent;
    std::vector<std::vector<uint8_t>> incoming;
};
}  // namespace

/// ethernet_mode=bridge on the Sprinter's NE2000: the gateway turns into a switch on the host adapter, the adapter
/// learns the card's MAC, a frame from the LAN reaches the card as a journaled NetFrame input (so a TTD replay needs no
/// host), the report shows the bridge; back to nat the router answers again and the adapter closes
TEST_F(SprinterNetwork_Test, Bridge_FramesFromTheLanAreJournaledInputs)
{
    Create();
    auto fake = std::make_unique<FakeHostFrames>();
    FakeHostFrames* host = fake.get();
    Network()->SetHostFrames(std::move(fake));
    NetworkManager::Change change;
    std::string error;
    ASSERT_TRUE(NetworkManager::ParseChange({{"ethernet_mode", "bridge"}, {"bridge_adapter", "fake0"}}, change, error)) << error;
    ASSERT_TRUE(Network()->RequestChange(change, error)) << error;
    Network()->OnFrame();
    ASSERT_NE(Network()->Gateway(), nullptr);
    EXPECT_EQ(Network()->Gateway()->GetMode(), EthernetGateway::Mode::Bridge);
    EXPECT_TRUE(host->open);
    ASSERT_EQ(host->stations.size(), 1u) << "the adapter keeps the card's frames";
    EXPECT_EQ(host->stations[0][5], 0x02);

    ttd::TimeTravelController* ttm = _context->pTimeTravelController;
    ASSERT_NE(ttm, nullptr);
    ASSERT_TRUE(ttm->StartRecording());
    std::vector<uint8_t> frame(60, 0x5A);
    std::copy(host->stations[0].begin(), host->stations[0].end(), frame.begin());
    const uint8_t lan[6] = {0x00, 0x11, 0x22, 0x33, 0x44, 0x55};
    std::copy(lan, lan + 6, frame.begin() + 6);
    frame[12] = 0x08;
    host->incoming.push_back(frame);
    Network()->OnFrame();
    EXPECT_EQ(Network()->Gateway()->GetLanCounters().in, 1u);
    // A short frame from the adapter (Wi-Fi drops the padding): padded to the 60-byte minimum, the card takes it
    host->incoming.push_back(std::vector<uint8_t>(frame.begin(), frame.begin() + 42));
    Network()->OnFrame();
    EXPECT_EQ(Network()->Gateway()->GetLanCounters().in, 2u);
    EXPECT_EQ(Network()->Gateway()->Capture().back().bytes.size(), 60u);
    EXPECT_EQ(Network()->Gateway()->GetCounters().framesToCards, 2u) << "the NE2000 took both";
    bool journaled = false;
    for (const ttd::TTDInputEvent& ev : ttm->GetInputJournal().Events())
    {
        if (ev.kind != ttd::TTDInputKind::NetFrame)
            continue;
        const ttd::TTDNetInput* net = ttm->GetInputJournal().NetOf(ev);
        ASSERT_NE(net, nullptr);
        ASSERT_EQ(net->payloadLength, frame.size());
        if (!journaled)
            EXPECT_EQ(std::vector<uint8_t>(ttm->GetInputJournal().PayloadOf(*net),
                                           ttm->GetInputJournal().PayloadOf(*net) + net->payloadLength),
                      frame);
        journaled = true;
    }
    EXPECT_TRUE(journaled) << "a frame from the LAN is an outside input";
    ttm->StopRecording();

    const StateNode report = DeviceState::Network(_context);
    const StateNode* gateway = report.find("ethernet_gateway");
    ASSERT_NE(gateway, nullptr);
    EXPECT_EQ(gateway->find("mode")->s, "bridge");
    ASSERT_NE(gateway->find("bridge"), nullptr);
    EXPECT_EQ(gateway->find("bridge")->find("adapter")->s, "fake0");
    EXPECT_TRUE(gateway->find("bridge")->find("open")->b);
    EXPECT_EQ(report.find("settings")->find("ethernet_mode")->s, "BRIDGE");

    NetworkManager::Change back;
    ASSERT_TRUE(NetworkManager::ParseChange({{"ethernet_mode", "nat"}}, back, error)) << error;
    ASSERT_TRUE(Network()->RequestChange(back, error)) << error;
    Network()->OnFrame();
    EXPECT_EQ(Network()->Gateway()->GetMode(), EthernetGateway::Mode::Nat);
    EXPECT_FALSE(host->open) << "the adapter closes with the bridge";

    NetworkManager::Change bad;
    EXPECT_FALSE(NetworkManager::ParseChange({{"ethernet_mode", "tap"}}, bad, error));
    EXPECT_NE(error.find("nat | bridge"), std::string::npos);
}
