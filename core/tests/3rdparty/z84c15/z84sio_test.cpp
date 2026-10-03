// Z84Sio: register pointer, RR0/RR1, the 3-byte receive FIFO (Sprinter test-plan §2.10 T-Z84; moved with the model into the z84c15 library).

#include "stdafx.h"
#include "pch.h"

#include <gtest/gtest.h>

#include <vector>

#include <3rdparty/z84c15/z84c15.h>

using Z84Lib::Z84Sio;

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
    EXPECT_EQ(sio.Read(0x19) & 0x01, 0x01);
    sio.Write(0x19, 0x01);  // point at RR1
    EXPECT_EQ(sio.Read(0x19) & 0x20, 0x00) << "three characters fit";

    EXPECT_EQ(sio.Read(0x18), 0x1C);
    EXPECT_EQ(sio.Read(0x18), 0xF0);
    EXPECT_EQ(sio.Read(0x18), 0x1C);
    EXPECT_EQ(sio.Read(0x19) & 0x01, 0x00);
    EXPECT_EQ(sio.Read(0x1B) & 0x01, 0x00) << "channel B is separate";
}

// The fourth character overwrites the newest in the FIFO and carries the overrun flag; RR1 bit 5 shows it when that
// character reaches the top and stays latched until Error Reset (Zilog SIO technical manual RR1 D5; Toshiba
// TMPZ84C015B RR1 D5; MAME z80sio queue_received / update_rr1)
TEST(Z84Sio_Test, Overrun_OverwritesTheNewestAndLatchesAtTheTop)
{
    Z84Sio sio;
    sio.Reset();
    auto rr1 = [&]() {
        sio.Write(0x19, 0x01);
        return static_cast<uint8_t>(sio.Read(0x19));
    };

    // Up released: E0 F0 75; then E0 72 (Down) arrive with nobody reading
    for (uint8_t b : {0xE0, 0xF0, 0x75})
        EXPECT_TRUE(sio.Receive(0, b));
    EXPECT_FALSE(sio.Receive(0, 0xE0)) << "the FIFO holds 3";
    EXPECT_FALSE(sio.Receive(0, 0x72));
    EXPECT_EQ(rr1() & 0x20, 0x00) << "the top character is good: RR1 describes it";
    EXPECT_FALSE(sio.OverrunLatched(0));

    EXPECT_EQ(sio.Read(0x18), 0xE0);
    EXPECT_EQ(rr1() & 0x20, 0x00);
    EXPECT_EQ(sio.Read(0x18), 0xF0);
    EXPECT_EQ(rr1() & 0x20, 0x20) << "the written-over character is at the top";
    EXPECT_EQ(sio.Read(0x18), 0x72) << "the newest character replaced #75 (and #E0 before it)";
    EXPECT_EQ(sio.Read(0x19) & 0x01, 0x00);
    EXPECT_EQ(rr1() & 0x20, 0x20) << "latched after the read";

    EXPECT_TRUE(sio.Receive(0, 0x1C));
    EXPECT_EQ(rr1() & 0x20, 0x20) << "still latched: only Error Reset clears it";
    sio.Write(0x19, 0x30);  // WR0 command 6: error reset
    EXPECT_EQ(rr1() & 0x20, 0x00);
    EXPECT_EQ(sio.Read(0x18), 0x1C);
}

// Error Reset with a written-over character still queued: the latch clears, the character's own flag latches again
// when it reaches the top
TEST(Z84Sio_Test, Overrun_ErrorResetBeforeTheFlaggedCharacter)
{
    Z84Sio sio;
    sio.Reset();
    for (uint8_t b : {0x01, 0x02, 0x03, 0x04})
        sio.Receive(0, b);
    sio.Write(0x19, 0x30);
    EXPECT_FALSE(sio.OverrunLatched(0));
    EXPECT_EQ(sio.Read(0x18), 0x01);
    EXPECT_EQ(sio.Read(0x18), 0x02);
    EXPECT_TRUE(sio.OverrunLatched(0));
    EXPECT_EQ(sio.Read(0x18), 0x04);
}

// Interrupt on the first character (WR1 bits 4-3 = 01): a read does not advance the FIFO past the flagged character
// until Error Reset (MAME z80sio data_read)
TEST(Z84Sio_Test, Overrun_FirstCharacterModeHoldsTheFifo)
{
    Z84Sio sio;
    sio.Reset();
    sio.Write(0x19, 0x01);
    sio.Write(0x19, 0x08);  // WR1: Rx INT on the first character
    for (uint8_t b : {0x11, 0x22, 0x33, 0x44})
        sio.Receive(0, b);
    EXPECT_EQ(sio.Read(0x18), 0x11);
    EXPECT_EQ(sio.Read(0x18), 0x22);
    EXPECT_TRUE(sio.OverrunLatched(0));
    EXPECT_EQ(sio.Read(0x18), 0x44);
    EXPECT_EQ(sio.Read(0x18), 0x44) << "held until Error Reset";
    EXPECT_EQ(sio.Read(0x19) & 0x01, 0x01);
    sio.Write(0x19, 0x30);
    EXPECT_EQ(sio.Read(0x18), 0x44);
    EXPECT_EQ(sio.Read(0x19) & 0x01, 0x00);
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
