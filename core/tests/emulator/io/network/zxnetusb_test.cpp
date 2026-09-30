// ZXNETUSB card port decoding (network adapters TDD §4.1, the card's CPLD:
// zbus.v, ports.v, wizmap.v). The chip behind it runs without a network.

#include <gtest/gtest.h>

#include <array>
#include <memory>

#include "_helpers/emulatortesthelper.h"
#include "_helpers/fakehostnet.h"
#include "emulator/cpu/core.h"
#include "emulator/cpu/z80.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/io/network/virtualnetwork.h"
#include "emulator/io/network/zxnetusb.h"

class ZxNetUsb_Test : public ::testing::Test
{
protected:
    ZxNetUsb card{nullptr};

    uint8_t In(uint16_t port) { return card.portDeviceInMethod(port); }
    void Out(uint16_t port, uint8_t v) { card.portDeviceOutMethod(port, v); }

    // What NedoOS and wizcfg do before touching a register
    void Select(uint8_t block)
    {
        Out(0x82AB, static_cast<uint8_t>((In(0x82AB) & 0x40) | 0x10));
        Out(0x81AB, block);
    }
};

TEST_F(ZxNetUsb_Test, EverythingReadsFFAfterPowerOn)
{
    // #82AB bit 4 off: the A15 = 0 ports are the (absent) SL811
    EXPECT_EQ(In(0x01AB), 0xFF);
    EXPECT_EQ(In(0x80AB), 0xFF);
    EXPECT_EQ(In(0x83AB), 0x00);
    EXPECT_FALSE(card.ChipRunning());
}

TEST_F(ZxNetUsb_Test, PresenceCheckOfWizcfg)
{
    Out(0x81AB, 0x0A);
    EXPECT_EQ(In(0x81AB) & 0x0F, 0x0A);
}

TEST_F(ZxNetUsb_Test, ChipIsHeldInResetUntilControlBit4)
{
    Select(3);                       // IDR #0FE = block 3, offset #3E
    EXPECT_EQ(In(0x3EAB), 0xFF) << "IDR while in reset";
    Out(0x83AB, 0x10);
    EXPECT_EQ(In(0x3EAB), 0x53) << "IDR high byte after reset release";
    EXPECT_EQ(In(0x3FAB), 0x00);
}

TEST_F(ZxNetUsb_Test, ResetPulseLikeWizcfgClearsTheChip)
{
    Out(0x83AB, 0x10);
    Select(0);
    Out(0x14AB, 0xAA);               // SUBR0
    EXPECT_EQ(In(0x14AB), 0xAA);
    Out(0x83AB, In(0x83AB) & ~0x10); // reset asserted
    Out(0x83AB, In(0x83AB) | 0x10);  // released
    Select(0);
    EXPECT_EQ(In(0x14AB), 0x00);
}

TEST_F(ZxNetUsb_Test, A14AndTheControlMirrorsAreNotDecoded)
{
    Out(0x83AB, 0x10);
    Select(0);
    Out(0x08AB, 0x02);               // SHAR0
    EXPECT_EQ(In(0x48AB), 0x02) << "A14 ignored: #48AB mirrors #08AB";
    Out(0xFDAB, 0x05);               // A15 = 1, A9..A8 = 01: #81AB mirror
    EXPECT_EQ(In(0x81AB), 0x05);
}

TEST_F(ZxNetUsb_Test, SocketBlockSelectedBy81AB)
{
    Out(0x83AB, 0x10);
    Select(9);                       // socket 1: #240
    Out(0x0AAB, 0xC0);               // Sn_PORTR high
    EXPECT_EQ(card.ChipAddress(0x0AAB), 0x24A);
    EXPECT_EQ(card.Chip().GetSocket(1).sourcePort >> 8, 0xC0);
}

TEST_F(ZxNetUsb_Test, InvertA0SwapsTheByteOfAPair)
{
    Out(0x83AB, 0x10);
    Select(0);
    Out(0x82AB, 0x18);               // I/O space + invert A0
    Out(0x08AB, 0x11);               // lands at #009
    Out(0x82AB, 0x10);
    EXPECT_EQ(In(0x09AB), 0x11);
}

TEST_F(ZxNetUsb_Test, RomMapAndPortsTogetherMeanNeither)
{
    Out(0x83AB, 0x10);
    Out(0x82AB, 0x14);
    EXPECT_FALSE(card.ChipInPorts());
    EXPECT_EQ(In(0x3EAB), 0xFF);
}

TEST_F(ZxNetUsb_Test, MachineResetClearsTheCard)
{
    Out(0x83AB, 0x10);
    Out(0x82AB, 0x10);
    Out(0x81AB, 0x08);
    card.Reset();
    EXPECT_EQ(In(0x83AB), 0x00);
    EXPECT_EQ(In(0x81AB), 0x00);
    EXPECT_FALSE(card.ChipRunning());
}

// The card's /INT to the Z80 (the CPLD's zint_n, open drain on ZX-Bus B13):
// W5300 INTn AND #83AB bit 2, AND bit 6; a level the CPU sees until the
// program clears the socket's interrupt bits
class ZxNetUsbInt_Test : public ::testing::Test
{
protected:
    void SetUp() override
    {
        _emulator = EmulatorTestHelper::CreateStandardEmulator("PENTAGON", LoggerLevel::LogError);
        ASSERT_NE(_emulator, nullptr);
        _z80 = _emulator->GetContext()->pCore->GetZ80();
        auto host = std::make_unique<FakeHostNet>();
        _host = host.get();
        _net = std::make_unique<VirtualNetwork>(nullptr, std::move(host), VirtualNetworkConfig{});
        _card = std::make_unique<ZxNetUsb>(_net.get(), _emulator->GetContext()->pCore);
    }

    void TearDown() override
    {
        _card.reset();
        _net.reset();
        EmulatorTestHelper::CleanupEmulator(_emulator);
    }

    void Out(uint16_t port, uint8_t v) { _card->portDeviceOutMethod(port, v); }
    uint8_t In(uint16_t port) { return _card->portDeviceInMethod(port); }

    // W5300 register through the I/O window: #81AB = address bits 9..6,
    // the port's high byte = bits 5..0
    void W(uint16_t address, uint8_t v)
    {
        Out(0x81AB, static_cast<uint8_t>(address >> 6));
        Out(static_cast<uint16_t>(((address & 0x3F) << 8) | 0xAB), v);
    }

    // Socket 0 in TCP, connected by the host: Sn_IR CON set
    void ConnectSocket0()
    {
        W(0x201, W5300::kModeTcp);                    // S0_MR
        W(0x203, W5300::kCmdOpen);                    // S0_CR
        for (uint16_t a = 0x214; a < 0x218; ++a)      // S0_DIPR 93.184.216.34
            W(a, static_cast<uint8_t>(std::array<uint8_t, 4>{93, 184, 216, 34}[a - 0x214]));
        W(0x212, 0);
        W(0x213, 80);                                 // S0_DPORTR
        W(0x203, W5300::kCmdConnect);
        const FakeHostNet::Command* connect = _host->Last("connect");
        ASSERT_NE(connect, nullptr);
        _host->Push(NetEventType::Connected, connect->socket);
        _net->Pump();
    }

    bool Line() const { return (_z80->GetDeviceIntLines() & Z80::kDeviceIntZxNetUsb) != 0; }

    Emulator* _emulator = nullptr;
    Z80* _z80 = nullptr;
    FakeHostNet* _host = nullptr;
    std::unique_ptr<VirtualNetwork> _net;
    std::unique_ptr<ZxNetUsb> _card;
};

TEST_F(ZxNetUsbInt_Test, ANetworkEventPullsTheLineWhenBothEnablesAreSet)
{
    Out(0x82AB, 0x10);                     // W5300 in the I/O space
    Out(0x83AB, 0x10 | 0x04 | 0x40);       // running, W5300 INT enabled, INT to the Z80 enabled
    W(0x005, 0x01);                        // IMR: socket 0
    EXPECT_FALSE(Line());

    ConnectSocket0();
    EXPECT_TRUE(Line()) << "Sn_IR CON while the chip's INT is unmasked";
    EXPECT_EQ(In(0x83AB) & 0x81, 0x81) << "#83AB: W5300 INT (bit 0) and INT to the Z80 (bit 7)";

    W(0x207, W5300::kIrCon);               // S0_IR: write 1 to clear
    EXPECT_FALSE(Line()) << "released when the program clears the socket's bit";
}

TEST_F(ZxNetUsbInt_Test, TheControlBitsGateTheLine)
{
    Out(0x82AB, 0x10);
    Out(0x83AB, 0x10 | 0x04);              // Z80 INT disabled
    W(0x005, 0x01);
    ConnectSocket0();
    EXPECT_FALSE(Line()) << "#83AB bit 6 off: the card keeps its INT to itself";
    EXPECT_EQ(In(0x83AB) & 0x81, 0x01);

    Out(0x83AB, 0x10 | 0x04 | 0x40);
    EXPECT_TRUE(Line()) << "enabled while the chip holds INT: asserted at once";
    Out(0x83AB, 0x10 | 0x40);
    EXPECT_FALSE(Line()) << "#83AB bit 2 off";
}

TEST_F(ZxNetUsbInt_Test, ResetAndUnplugReleaseTheLine)
{
    Out(0x82AB, 0x10);
    Out(0x83AB, 0x10 | 0x04 | 0x40);
    W(0x005, 0x01);
    ConnectSocket0();
    ASSERT_TRUE(Line());
    _card->Reset();
    EXPECT_FALSE(Line()) << "machine reset";

    Out(0x82AB, 0x10);
    Out(0x83AB, 0x10 | 0x04 | 0x40);
    W(0x005, 0x01);
    ConnectSocket0();
    ASSERT_TRUE(Line());
    _card.reset();
    EXPECT_FALSE(Line()) << "the card was removed";
    EXPECT_FALSE(_emulator->GetContext()->HasStepWork(EmulatorContext::kStepWorkDeviceInt));
}
