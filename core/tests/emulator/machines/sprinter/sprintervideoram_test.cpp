// SprinterVideoRam: storage, the mode-table layout and the INT notification
// (Sprinter tdd-video §1, §5; MAME vram_w).

#include "stdafx.h"
#include "pch.h"

#include <gtest/gtest.h>

#include "emulator/video/sprinter/sprintervideoram.h"

TEST(SprinterVideoRam_Test, ModeAddress_RowAndColumn)
{
    // Square (a, b): row 1 + 2a + #80 x page, column #300 + 4b (MAN §4.5)
    EXPECT_EQ(SprinterVideoRam::ModeAddress(0, 0, 0), 1u * 1024 + 0x300);
    EXPECT_EQ(SprinterVideoRam::ModeAddress(55, 39, 0), (1u + 110) * 1024 + 0x300 + 156);
    EXPECT_EQ(SprinterVideoRam::ModeAddress(0, 0, 1), (1u + 0x80) * 1024 + 0x300);
    EXPECT_TRUE(SprinterVideoRam::IsIntModeByte(SprinterVideoRam::ModeAddress(10, 20, 1)));
    EXPECT_FALSE(SprinterVideoRam::IsIntModeByte(SprinterVideoRam::ModeAddress(10, 20, 1) + 1)) << "Mode1";
    EXPECT_FALSE(SprinterVideoRam::IsIntModeByte(2u * 1024 + 0x300)) << "even row: the Line2 set";
    EXPECT_FALSE(SprinterVideoRam::IsIntModeByte(1u * 1024 + 0x3A0)) << "past the mode table";
}

TEST(SprinterVideoRam_Test, Write_NotifiesOnlyBlankIntChanges)
{
    SprinterVideoRam vram;
    int notified = 0;
    vram.SetIntModeListener([&] { notified++; });
    const uint32_t mode0 = SprinterVideoRam::ModeAddress(3, 4, 0);

    vram.Write(mode0, 0x10);
    EXPECT_EQ(notified, 0) << "text square";
    vram.Write(mode0, 0xFD);
    EXPECT_EQ(notified, 1) << "to blank + INT";
    vram.Write(mode0, 0xFD);
    EXPECT_EQ(notified, 1) << "no change";
    vram.Write(mode0, 0xFC);
    EXPECT_EQ(notified, 2) << "blank without INT (MAME recomputes on any #FC-pattern change)";
    vram.Write(mode0 + 1, 0xFD);
    EXPECT_EQ(notified, 2) << "Mode1 byte";
    vram.Write(0x10, 0xFD);
    EXPECT_EQ(notified, 2) << "screen data";
    EXPECT_EQ(vram.Read(mode0), 0xFC);
    vram.Write(SprinterVideoRam::kSize + 5, 0x42);
    EXPECT_EQ(vram.Read(5), 0x42) << "256 KB wraps";
}
