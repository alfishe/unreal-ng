// The Next's expansion connector as its slot-built cards see it (PortDecoder_Next::SlotCardGate, zxnext.vhd 1886-1893,
// 2230-2242, 3471-3480): NR #80 bit 7 connects the bus, NR #80 bit 5 masks its I/O, a port of the board's own decode reaches a
// card's /IORQ only when NR #8A propagates it, and a read of #FE ANDs the connector's data into the ULA's.

#include <gtest/gtest.h>

#include <functional>
#include <string>
#include <utility>
#include <vector>

#include "_helpers/emulatortesthelper.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/memory/next/nextmemory.h"
#include "emulator/ports/portdecoder.h"
#include "emulator/slots/cards/fuller/fullerboxcard.h"
#include "emulator/slots/slotcontrol.h"
#include "emulator/slots/slotmanager.h"
#include "emulator/sound/chips/soundchip_ay8910.h"

namespace
{

/// A card on the connector that answers what it claims and counts what it sees
class ProbeCard : public PortDevice
{
public:
    uint8_t readValue = 0xF0;
    unsigned reads = 0;
    unsigned writes = 0;
    uint16_t lastPort = 0;
    uint8_t lastValue = 0;

    uint8_t portDeviceInMethod(uint16_t port) override
    {
        reads++;
        lastPort = port;
        return readValue;
    }
    void portDeviceOutMethod(uint16_t port, uint8_t value) override
    {
        writes++;
        lastPort = port;
        lastValue = value;
    }
};

class NextExpansionBus_Test : public ::testing::Test
{
protected:
    Emulator* _emulator = nullptr;
    EmulatorContext* _context = nullptr;
    PortDecoder* _ports = nullptr;
    ProbeCard _probe;
    bool _probeAttached = false;

    void TearDown() override
    {
        if (_probeAttached)
            _ports->DetachSlotCard(&_probe);
        if (_emulator)
            EmulatorTestHelper::CleanupEmulator(_emulator);
    }

    void Create(const std::vector<std::pair<std::string, std::string>>& slots = {})
    {
        std::function<void(CONFIG&)> slotSet;
        std::string error;
        ASSERT_TRUE(SlotControl::CreateOverride(slots, slotSet, error)) << error;
        _emulator = EmulatorTestHelper::CreateStandardEmulator("NEXT", LoggerLevel::LogError, RamPowerOn::Zero, slotSet);
        ASSERT_NE(_emulator, nullptr);
        _context = _emulator->GetContext();
        _ports = _context->pPortDecoder;
    }

    /// The probe on the connector, claiming exactly these ports
    void AttachProbe(std::initializer_list<uint16_t> ports)
    {
        std::vector<slots::PortClaim> claims;
        for (uint16_t port : ports)
            claims.push_back({.mask = 0xFFFF, .match = port, .dir = slots::Dir::InOut});
        _ports->ConfigureSlotBus(slots::Arbitration::None, slots::ReadRule::CardOverUla, {}, {});
        _ports->AttachSlotCard(&_probe, claims, PortDecoder::kSlotCardSlotBase, slots::CycleDetection::Iorq);
        _probeAttached = true;
    }

    void Out(uint16_t port, uint8_t value) { _ports->WriteCycle(port, value, 0x8000); }
    uint8_t In(uint16_t port, bool& drove) { return _ports->ReadCycle(port, 0x8000, drove); }
    uint8_t In(uint16_t port)
    {
        bool drove = false;
        return In(port, drove);
    }
    void NextReg(uint8_t reg, uint8_t value)
    {
        Out(0x243B, reg);
        Out(0x253B, value);
    }
};

} // namespace

/// A Fuller Box on the Next (~10 ms): dead while the bus is off; with it on, #3F / #5F are the board's (the +3 Multiface's enable port,
/// the DACs of Profi Covox and SounDrive mode 1) until NR #87 bit 1 and NR #88 bits 3 and 1 take them off the bus's port enables, then
/// the card's AY gets them; NR #80 bit 5 masks them again
TEST_F(NextExpansionBus_Test, AFullerBoxSeesOnlyThePortsTheBoardLeavesToTheBus)
{
    Create({{"edge.1", "fuller-box"}});
    auto* card = dynamic_cast<FullerBoxCard*>(_context->pSlotManager->FindCard("edge.1"));
    ASSERT_NE(card, nullptr) << "the Next's edge slot builds the card";

    auto writeVolume = [&](uint8_t value) {
        Out(0x003F, 8);  // channel A volume
        Out(0x005F, value);
    };
    bool drove = true;
    writeVolume(0x05);
    EXPECT_EQ(card->Chip().readRegister(8), 0x00) << "the bus is off at power-on (NR #80 bit 7)";
    EXPECT_EQ(In(0x007F, drove), 0xFF);
    EXPECT_FALSE(drove) << "nothing is read from a connector that is off";

    NextReg(0x80, 0x80);
    writeVolume(0x06);
    EXPECT_EQ(card->Chip().readRegister(8), 0x00) << "#3F / #5F are the board's (the Multiface, the DACs A / D)";
    EXPECT_EQ(In(0x007F, drove), 0xFF);
    EXPECT_TRUE(drove) << "#7F is no port of the board: the stick answers";

    NextReg(0x88, 0xF5);  // the bus's word of NR #84 without bits 3 and 1
    writeVolume(0x09);
    EXPECT_EQ(card->Chip().readRegister(8), 0x00) << "#3F is still the Multiface's";
    NextReg(0x87, 0xFD);  // the bus's word of NR #83 without bit 1
    writeVolume(0x07);
    EXPECT_EQ(card->Chip().readRegister(8), 0x07) << "the DAC off the bus's enables: the card's AY takes the ports";
    EXPECT_EQ(In(0x003F, drove), 0x07);
    EXPECT_TRUE(drove);

    NextReg(0x80, 0xA0);  // bit 5: the bus's I/O masked
    writeVolume(0x08);
    EXPECT_EQ(card->Chip().readRegister(8), 0x07) << "NR #80 bit 5 masks every port";
    EXPECT_EQ(In(0x007F, drove), 0xFF);
    EXPECT_FALSE(drove);
}

/// NR #8A bit 0 (~5 ms): a write to #FE reaches the card only when propagated; a read of #FE ANDs the card's data into the ULA's,
/// and NR #81 bit 6 leaves the card's alone for A7:A4 = 0
TEST_F(NextExpansionBus_Test, Nr8aBit0PropagatesPortFeAndAndsTheCardsData)
{
    Create();
    AttachProbe({0x00FE, 0x000E});
    NextReg(0x80, 0x80);
    _probe.readValue = 0xF5;
    constexpr uint8_t kKeys = 0x1F;  // the keyboard columns (no key pressed); EAR (bit 6) follows the tape input

    Out(0x00FE, 0x01);
    EXPECT_EQ(_probe.writes, 0u) << "#FE is the board's: masked from the connector";
    bool drove = true;
    EXPECT_EQ(In(0x00FE, drove) & kKeys, kKeys);
    EXPECT_EQ(_probe.reads, 0u);

    NextReg(0x8A, 0x01);
    Out(0x00FE, 0x02);
    EXPECT_EQ(_probe.writes, 1u);
    EXPECT_EQ(_probe.lastValue, 0x02);
    EXPECT_EQ(In(0x00FE, drove) & kKeys, 0x15) << "port_fe_dat: the ULA's AND the connector's";
    EXPECT_TRUE(drove);

    NextReg(0x81, 0x40);
    EXPECT_EQ(In(0x00FE) & kKeys, 0x15) << "A7:A4 = F: no override";
    _probe.readValue = 0x5A;
    EXPECT_EQ(In(0x000E), 0x5A) << "NR #81 bit 6 with A7:A4 = 0: the connector's data alone";

    NextReg(0x80, 0x00);
    const unsigned writes = _probe.writes;
    Out(0x00FE, 0x03);
    EXPECT_EQ(_probe.writes, writes) << "propagation needs the bus on";
}

/// NR #8A bits 1-4 (~5 ms): the paging ports and #FF reach the card while the board still decodes them; a propagated port still
/// reaches it with NR #80 bit 5 set
TEST_F(NextExpansionBus_Test, Nr8aPropagatesThePagingPortsAndTheBoardStillDecodesThem)
{
    Create();
    AttachProbe({0x7FFD, 0xDFFD, 0x1FFD, 0x00FF});
    NextReg(0x80, 0x80);
    for (uint16_t port : {0x7FFD, 0xDFFD, 0x1FFD, 0x00FF})
        Out(port, 0x00);
    EXPECT_EQ(_probe.writes, 0u) << "all four are the board's";

    NextReg(0x8A, 0x1E);
    unsigned expected = 0;
    for (uint16_t port : {0x7FFD, 0xDFFD, 0x1FFD, 0x00FF})
    {
        Out(port, 0x00);
        EXPECT_EQ(_probe.writes, ++expected) << std::hex << port;
        EXPECT_EQ(_probe.lastPort, port);
    }
    Out(0x7FFD, 0x03);
    EXPECT_EQ(_probe.lastValue, 0x03);
    auto* memory = dynamic_cast<NextMemory*>(_context->pMemory);
    ASSERT_NE(memory, nullptr);
    EXPECT_EQ(memory->GetMmu(6), 6) << "the board pages bank 3 in all the same";
    Out(0x243B, 0x8A);
    EXPECT_EQ(In(0x253B), 0x1E) << "NR #8A reads back its six bits";

    NextReg(0x80, 0xA0);
    Out(0x7FFD, 0x04);
    EXPECT_EQ(_probe.lastValue, 0x04) << "NR #80 bit 5 masks the unpropagated ports only";
    NextReg(0x8A, 0x1C);
    Out(0x7FFD, 0x05);
    EXPECT_EQ(_probe.lastValue, 0x04) << "bit 1 off: #7FFD masked again";
}
