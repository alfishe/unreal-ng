// Z84Pio as a register file (Sprinter test-plan §2.10 T-Z84; moved with the model into the z84c15 library).

#include "stdafx.h"
#include "pch.h"

#include <gtest/gtest.h>

#include <3rdparty/z84c15/z84c15.h>

using Z84Lib::Z84Pio;

// BIOS 3.04 #0154: mode 3, all outputs, then the POST code on port A
TEST(Z84Pio_Test, BiosPostSequence)
{
    Z84Pio pio;
    pio.Reset();
    pio.Write(1, 0xCF);
    pio.Write(1, 0x00);
    pio.Write(0, 0xEA);
    EXPECT_EQ(pio.GetPort(0).mode, 3);
    EXPECT_EQ(pio.GetPort(0).direction, 0x00);
    EXPECT_EQ(pio.Read(0), 0xEA);

    // Input lines read the inputs
    pio.Write(1, 0xCF);
    pio.Write(1, 0xF0);
    pio.SetInputs(0, 0x5F);
    EXPECT_EQ(pio.Read(0), 0x5A) << "high nibble from the inputs, low from the latch";
}

TEST(Z84Pio_Test, InterruptControlTakesAMask)
{
    Z84Pio pio;
    pio.Write(3, 0x97);  // B: interrupt control, mask follows
    pio.Write(3, 0x0F);
    EXPECT_EQ(pio.GetPort(1).mask, 0x0F);
    pio.Write(3, 0x20);  // vector
    EXPECT_EQ(pio.GetPort(1).vector, 0x20);
}
