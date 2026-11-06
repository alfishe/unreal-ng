#include <gtest/gtest.h>

#include "emulator/io/pic/pic8259.h"

/// Intel 8259A behaviors the SMUC firmware relies on (pic8259.h): the init sequence, the vector, EOI, masking,
/// edge and level triggering, the register reads

namespace
{
/// The GMX 6.44 / ProfROM interrupt test's programming: ICW1 #1A (level, single, no ICW4), ICW2 #F8, OCW1 #FE
void ProgramLikeTheFirmware(Pic8259& pic)
{
    pic.Write(false, 0x1A);
    pic.Write(true, 0xF8);
    pic.Write(true, 0xFE);
}
}

TEST(Pic8259_Test, FirmwareProbeSequence)
{
    // GMX 6.44 page 23 #1F92: ICW1 #F3, ICW2 #C5, ICW4 #01; a read of A0 = 1 (IMR, cleared by ICW1) gives 0 or 1
    Pic8259 pic;
    EXPECT_FALSE(pic.Initialized());
    pic.Write(false, 0xF3);
    pic.Write(true, 0xC5);
    EXPECT_FALSE(pic.Initialized()) << "ICW4 still due";
    pic.Write(true, 0x01);
    EXPECT_TRUE(pic.Initialized());
    EXPECT_LE(pic.Read(true), 1);
    // Then ICW1 #F2 (no ICW4), ICW2 #C5, OCW1 #FF: the IMR reads back #FF
    pic.Write(false, 0xF2);
    pic.Write(true, 0xC5);
    pic.Write(true, 0xFF);
    EXPECT_EQ(pic.Read(true), 0xFF);
}

TEST(Pic8259_Test, VectorInServiceAndEoi)
{
    Pic8259 pic;
    ProgramLikeTheFirmware(pic);
    EXPECT_FALSE(pic.IntAsserted());
    pic.SetInput(0, true);
    ASSERT_TRUE(pic.IntAsserted());
    EXPECT_EQ(pic.Acknowledge(), 0xF8) << "ICW2 T7-T3 with the level";
    EXPECT_EQ(pic.GetState().isr, 0x01);
    EXPECT_FALSE(pic.IntAsserted()) << "IR0 in service holds itself off";
    pic.Write(false, 0x20);   // non-specific EOI, the handler's OUT #7EBE, #20
    EXPECT_EQ(pic.GetState().isr, 0x00);
    EXPECT_TRUE(pic.IntAsserted()) << "level triggered: the pin is still high";
    pic.SetInput(0, false);
    EXPECT_FALSE(pic.IntAsserted());
}

TEST(Pic8259_Test, MaskedLevelsDoNotInterrupt)
{
    Pic8259 pic;
    ProgramLikeTheFirmware(pic);
    pic.SetInput(3, true);
    EXPECT_FALSE(pic.IntAsserted()) << "IR3 masked by OCW1 #FE";
    EXPECT_TRUE((pic.Read(false) & 0x08) != 0) << "IRR shows the request anyway";
    pic.Write(true, 0xF6);   // unmask IR3
    EXPECT_TRUE(pic.IntAsserted());
    EXPECT_EQ(pic.Acknowledge(), 0xFB);
}

TEST(Pic8259_Test, EdgeTriggeredLatchesTheRisingEdge)
{
    Pic8259 pic;
    pic.Write(false, 0x12);   // edge, single, no ICW4
    pic.Write(true, 0x40);
    pic.Write(true, 0x00);
    pic.SetInput(2, true);
    pic.SetInput(2, false);
    ASSERT_TRUE(pic.IntAsserted()) << "the edge stays latched in IRR after the pin falls";
    EXPECT_EQ(pic.Acknowledge(), 0x42);
    pic.Write(false, 0x20);
    EXPECT_FALSE(pic.IntAsserted()) << "the acknowledge cleared IRR";
}

TEST(Pic8259_Test, PriorityAndIsrRead)
{
    Pic8259 pic;
    pic.Write(false, 0x1A);
    pic.Write(true, 0x08);
    pic.Write(true, 0x00);
    pic.SetInput(5, true);
    pic.SetInput(1, true);
    EXPECT_EQ(pic.Acknowledge(), 0x09) << "IR1 before IR5";
    EXPECT_FALSE(pic.IntAsserted()) << "IR5 is below IR1 in service";
    pic.Write(false, 0x0B);   // OCW3: read ISR
    EXPECT_EQ(pic.Read(false), 0x02);
    pic.Write(false, 0x61);   // specific EOI for IR1
    EXPECT_EQ(pic.Read(false), 0x00);
    pic.Write(false, 0x0A);   // OCW3: read IRR
    EXPECT_EQ(pic.Read(false), 0x22);
    EXPECT_EQ(pic.Acknowledge(), 0x09) << "IR1 still high, level triggered: first again";
}

TEST(Pic8259_Test, PollReadsTheHighestRequest)
{
    Pic8259 pic;
    ProgramLikeTheFirmware(pic);
    pic.Write(false, 0x0C);   // OCW3: poll
    EXPECT_EQ(pic.Read(false), 0x00) << "no request";
    pic.SetInput(0, true);
    pic.Write(false, 0x0C);
    EXPECT_EQ(pic.Read(false), 0x80);
    EXPECT_EQ(pic.GetState().isr, 0x01) << "the poll read acknowledged it";
}

TEST(Pic8259_Test, StateRoundTrip)
{
    Pic8259 pic;
    ProgramLikeTheFirmware(pic);
    pic.SetInput(0, true);
    (void)pic.Acknowledge();
    Pic8259 copy;
    copy.SetState(pic.GetState());
    EXPECT_EQ(copy.GetState().isr, 0x01);
    EXPECT_EQ(copy.GetState().imr, 0xFE);
    copy.Write(false, 0x20);
    EXPECT_TRUE(copy.IntAsserted());
}
