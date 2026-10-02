// Z84Ctc: control words, time constants, the timer down-counter, the vector (Sprinter test-plan §2.10 T-Z84; moved with the model into the z84c15 library).

#include "stdafx.h"
#include "pch.h"

#include <gtest/gtest.h>

#include <3rdparty/z84c15/z84c15.h>

using Z84Lib::Z84Ctc;

TEST(Z84Ctc_Test, Timer_CountsThroughThePrescaler)
{
    Z84Ctc ctc;
    uint64_t clock = 1000;
    ctc.SetClock([&] { return clock; });
    ctc.Reset();

    ctc.Write(1, 0x25);  // timer, prescaler 256, time constant follows
    ctc.Write(1, 0x0A);
    EXPECT_EQ(ctc.Read(1), 10);
    clock += 512;
    EXPECT_EQ(ctc.Read(1), 8) << "the header's worked example";
    clock += 8 * 256;
    EXPECT_EQ(ctc.Read(1), 10) << "reloads after zero";

    ctc.Write(2, 0x05);  // prescaler 16
    ctc.Write(2, 0x00);  // 256
    clock += 16;
    EXPECT_EQ(ctc.Read(2), 0xFF);
}

TEST(Z84Ctc_Test, ResetStopsAndVectorGoesToChannel0)
{
    Z84Ctc ctc;
    uint64_t clock = 0;
    ctc.SetClock([&] { return clock; });
    ctc.Write(0, 0xE8);  // bit 0 = 0 on channel 0: the vector
    EXPECT_EQ(ctc.Vector(), 0xE8);
    ctc.Write(3, 0x07);  // reset + time constant follows
    ctc.Write(3, 0x20);
    clock += 1600;
    EXPECT_NE(ctc.Read(3), 0x20);
    ctc.Write(3, 0x03);  // software reset
    EXPECT_EQ(ctc.Read(3), 0x20) << "stopped: the loaded value";
    EXPECT_FALSE(ctc.GetChannel(3).running);
}
