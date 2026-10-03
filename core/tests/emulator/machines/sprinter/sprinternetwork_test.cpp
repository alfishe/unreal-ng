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
    EXPECT_EQ(_decoder->GetIsaBus().Card(0), nullptr) << "slot 1 waits for the ZX-bus adapter (ISA I2)";

    EXPECT_EQ(IsaIo("io_read", 2, 0x30A), 0x50) << "RTL8019AS ID 'P' through the ISA bus";
    EXPECT_EQ(IsaIo("io_read", 2, 0x30B), 0x70);
    EXPECT_EQ(IsaIo("io_read", 1, 0x30A), 0xFF) << "slot 1 is empty";
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
    EXPECT_FALSE(net.find("machine")->find("zx_bus")->b) << "no ZX-bus on the Sprinter (until the adapter)";

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
    ASSERT_EQ(Network()->SlotCards().size(), 1u);
    EXPECT_EQ(Network()->SlotCards()[0].slotId, "isa1");
    EXPECT_EQ(IsaIo("io_read", 1, 0x34A), 0x50) << "the NE2000 at #340 in slot 1";
    const StateNode isa = DeviceState::Isa(_context);
    const StateNode* why = isa.find("slots")->items[1].find("not_fitted");
    ASSERT_NE(why, nullptr) << "the SprinterESP is not built yet";
    EXPECT_NE(why->s.find("SN3"), std::string::npos) << why->s;
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
