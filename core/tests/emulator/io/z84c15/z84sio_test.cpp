// Z84Sio: register pointer, RR0/RR1, the 3-byte receive FIFO (Sprinter test-plan §2.10 T-Z84).

#include "stdafx.h"
#include "pch.h"

#include <gtest/gtest.h>

#include <vector>

#include "emulator/io/z84c15/z84sio.h"

// BIOS 3.04 SETUP KeyboardInit (#A373): WR1 = 0, WR3 = #C1, WR4 = #07, WR5 = #62 on channel A
TEST(Z84Sio_Test, KeyboardInitSequence_LeavesTheRegisters)
{
    Z84Sio sio;
    sio.Reset();
    for (uint8_t value : {0x00, 0x01, 0x00, 0x03, 0xC1, 0x04, 0x07, 0x05, 0x62})
        sio.Write(0x19, value);
    const Z84Sio::Channel& a = sio.GetChannel(0);
    EXPECT_EQ(a.wr[1], 0x00);
    EXPECT_EQ(a.wr[3], 0xC1);
    EXPECT_EQ(a.wr[4], 0x07);
    EXPECT_EQ(a.wr[5], 0x62);
    EXPECT_EQ(a.pointer, 0);
}

TEST(Z84Sio_Test, Receive_FifoAndStatus)
{
    Z84Sio sio;
    sio.Reset();
    EXPECT_EQ(sio.Read(0x19) & 0x01, 0x00) << "nothing received";
    EXPECT_EQ(sio.Read(0x19) & 0x04, 0x04) << "transmit buffer empty";

    EXPECT_TRUE(sio.Receive(0, 0x1C));
    EXPECT_TRUE(sio.Receive(0, 0xF0));
    EXPECT_TRUE(sio.Receive(0, 0x1C));
    EXPECT_FALSE(sio.Receive(0, 0x55)) << "the FIFO holds 3";
    EXPECT_EQ(sio.Read(0x19) & 0x01, 0x01);

    sio.Write(0x19, 0x01);  // point at RR1
    EXPECT_EQ(sio.Read(0x19) & 0x20, 0x20) << "overrun";
    sio.Write(0x19, 0x30);  // error reset
    sio.Write(0x19, 0x01);
    EXPECT_EQ(sio.Read(0x19) & 0x20, 0x00);

    EXPECT_EQ(sio.Read(0x18), 0x1C);
    EXPECT_EQ(sio.Read(0x18), 0xF0);
    EXPECT_EQ(sio.Read(0x18), 0x1C);
    EXPECT_EQ(sio.Read(0x19) & 0x01, 0x00);
    EXPECT_EQ(sio.Read(0x1B) & 0x01, 0x00) << "channel B is separate";
}

TEST(Z84Sio_Test, Transmit_GoesToTheSink)
{
    Z84Sio sio;
    std::vector<std::pair<uint8_t, uint8_t>> sent;
    sio.SetTransmitSink([&](uint8_t ch, uint8_t value) { sent.emplace_back(ch, value); });
    sio.Write(0x18, 0xFF);
    sio.Write(0x1A, 0x4D);
    ASSERT_EQ(sent.size(), 2u);
    EXPECT_EQ(sent[0], std::make_pair(uint8_t(0), uint8_t(0xFF)));
    EXPECT_EQ(sent[1], std::make_pair(uint8_t(1), uint8_t(0x4D)));
}

TEST(Z84Sio_Test, Rr2_ReadsTheChannelBVector)
{
    Z84Sio sio;
    sio.Write(0x1B, 0x02);
    sio.Write(0x1B, 0x60);
    sio.Write(0x1B, 0x02);
    EXPECT_EQ(sio.Read(0x1B), 0x60);
    sio.Write(0x1B, 0x18);  // channel reset keeps WR2
    sio.Write(0x1B, 0x02);
    EXPECT_EQ(sio.Read(0x1B), 0x60);
}
