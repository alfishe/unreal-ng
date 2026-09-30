// ZXNETUSB card port decoding (network adapters TDD §4.1, the card's CPLD:
// zbus.v, ports.v, wizmap.v). The chip behind it runs without a network.

#include <gtest/gtest.h>

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
