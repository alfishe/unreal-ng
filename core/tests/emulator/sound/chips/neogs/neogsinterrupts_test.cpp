// NeoGS interrupt controller and timer (neogs-tdd.md §3.5; FPGA interrupts.v, timer.v)

#include <gtest/gtest.h>

#include "emulator/sound/chips/neogs/neogsinterrupts.h"

TEST(NeoGSInterrupts, ResetState)
{
    NeoGSInterrupts irq;
    irq.writeEnable(0x87);
    irq.raise(0x07);
    irq.reset();
    EXPECT_EQ(irq.enable(), 0x01);
    EXPECT_EQ(irq.readRequest(), 0x00);
    EXPECT_EQ(irq.timFreq(), 0);
    EXPECT_FALSE(irq.intLine());
}

TEST(NeoGSInterrupts, SetClearEncodingOnBothRegisters)
{
    NeoGSInterrupts irq;
    irq.writeEnable(0x86); // d7 = 1: set bits 1 and 2
    EXPECT_EQ(irq.enable(), 0x07);
    irq.writeEnable(0x01); // d7 = 0: clear bit 0
    EXPECT_EQ(irq.enable(), 0x06);

    irq.writeRequest(0x85);
    EXPECT_EQ(irq.readRequest(), 0x05) << "software can set requests";
    irq.writeRequest(0x04);
    EXPECT_EQ(irq.readRequest(), 0x01);
}

TEST(NeoGSInterrupts, LevelHeldUntilAcknowledged)
{
    NeoGSInterrupts irq;
    irq.raise(NeoGSInterrupts::REQ_TIMER);
    EXPECT_TRUE(irq.intLine());
    EXPECT_TRUE(irq.intLine()) << "a level, not a pulse";
    irq.acknowledge();
    EXPECT_FALSE(irq.intLine());
}

TEST(NeoGSInterrupts, MaskedRequestStaysPendingWithoutInt)
{
    NeoGSInterrupts irq;
    irq.raise(NeoGSInterrupts::REQ_SD_DMA); // not enabled at reset
    EXPECT_FALSE(irq.intLine());
    EXPECT_EQ(irq.readRequest(), 0x02);
    irq.writeEnable(0x82);
    EXPECT_TRUE(irq.intLine());
}

TEST(NeoGSInterrupts, PriorityAndVectors)
{
    NeoGSInterrupts irq;
    irq.writeEnable(0x87);
    irq.raise(0x07);
    EXPECT_EQ(irq.vector(), 0xFF) << "timer first";
    irq.acknowledge();
    EXPECT_EQ(irq.readRequest(), 0x06);
    EXPECT_EQ(irq.vector(), 0xF7) << "then SD DMA";
    irq.acknowledge();
    EXPECT_EQ(irq.vector(), 0xEF) << "then MP3 DMA";
    irq.acknowledge();
    EXPECT_EQ(irq.readRequest(), 0x00);
}

TEST(NeoGSInterrupts, TimerPeriodsForEveryRate)
{
    // 24 MHz / 5 / 128 = 37,500 Hz at rate 0, then /2 /4 /8 /16 /64 /256 /1024
    const int64_t expected[8] = {640, 1280, 2560, 5120, 10240, 40960, 163840, 655360};
    for (uint8_t rate = 0; rate < 8; rate++)
        EXPECT_EQ(NeoGSInterrupts::tickPeriodCrystal(rate), expected[rate]) << "rate " << int(rate);
    EXPECT_EQ(NeoGSInterrupts::nextTickCrystal(0, 0), 640);
    EXPECT_EQ(NeoGSInterrupts::nextTickCrystal(0, 639), 640);
    EXPECT_EQ(NeoGSInterrupts::nextTickCrystal(0, 640), 1280);
}

TEST(NeoGSInterrupts, TimFreqSwitchFromHighBitToLowBitTicksOnce)
{
    NeoGSInterrupts irq;
    // Crystal 400: counter 80 = 0b1010000 -> bit 6 = 1; bit 7 = 0
    EXPECT_TRUE(NeoGSInterrupts::selectedBit(0, 400));
    EXPECT_FALSE(NeoGSInterrupts::selectedBit(1, 400));
    EXPECT_TRUE(irq.writeTimFreq(1, 400)) << "1 -> 0 on the selected bit is a falling edge";
    EXPECT_FALSE(irq.writeTimFreq(0, 400)) << "0 -> 1 is not";
}
