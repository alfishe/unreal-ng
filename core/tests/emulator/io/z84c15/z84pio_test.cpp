// Z84Pio as a register file (Sprinter test-plan §2.10 T-Z84).

#include "stdafx.h"
#include "pch.h"

#include <gtest/gtest.h>

#include "emulator/io/z84c15/z84c15.h"
#include "emulator/io/z84c15/z84pio.h"

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

// The package: low address byte decode and the system control registers
TEST(Z84C15_Test, OwnsItsPortsAndChipSelects)
{
    for (uint8_t port : {0x10, 0x13, 0x18, 0x1F, 0xEE, 0xEF, 0xF0, 0xF1, 0xF4})
        EXPECT_TRUE(Z84C15::Owns(port)) << std::hex << +port;
    for (uint8_t port : {0x0F, 0x14, 0x17, 0x20, 0xED, 0xF2, 0xF3, 0xFE, 0xFF})
        EXPECT_FALSE(Z84C15::Owns(port)) << std::hex << +port;

    Z84C15 chip;
    chip.PowerOn();
    EXPECT_EQ(chip.system.Cs0End(), 0x10000u) << "CSBR #FF, MCR 1: everything CS0";
    chip.Write(0xEE, 0x02);
    chip.Write(0xEF, 0xFE);
    EXPECT_EQ(chip.Read(0xEF), 0xFE);
    EXPECT_EQ(chip.system.Cs0End(), 0xF000u) << "the loader's boundary";
    chip.Write(0xEF, 0xF0);
    EXPECT_EQ(chip.system.Cs0End(), 0x1000u) << "a reload: the stream at fast RAM #1000";
    EXPECT_EQ(chip.Read(0xF0), 0xFB) << "WDTMR power-on value";
}
