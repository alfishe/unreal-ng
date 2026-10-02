#include "stdafx.h"
#include "pch.h"

#include "emulator/ports/models/profiboard.h"

/// @brief The board profile (docs/inprogress/2026-10-01-profi-v3-v5 design section 2) and the sync PROM table
///        (section 5.1)
TEST(ProfiBoard_Test, V5HasPaletteAndExtendedPortsV3HasNeither)
{
    constexpr ProfiBoard v5 = ProfiBoard::For(MM_PROFI);
    EXPECT_TRUE(v5.palette);
    EXPECT_TRUE(v5.extendedPorts);
    EXPECT_TRUE(v5.fePaletteBit7);

    constexpr ProfiBoard v3 = ProfiBoard::For(MM_PROFI3);
    EXPECT_FALSE(v3.palette);
    EXPECT_FALSE(v3.extendedPorts);
    EXPECT_FALSE(v3.fePaletteBit7);
}

TEST(ProfiBoard_Test, NonProfiModelsGetNothing)
{
    for (MEM_MODEL model : {MM_PENTAGON, MM_SPECTRUM128, MM_SCORP, MM_ATM710, MM_TSL})
    {
        const ProfiBoard board = ProfiBoard::For(model);
        EXPECT_FALSE(board.palette || board.extendedPorts || board.fePaletteBit7) << int(model);
        EXPECT_FALSE(IsProfiModel(model)) << int(model);
    }
    EXPECT_TRUE(IsProfiModel(MM_PROFI));
    EXPECT_TRUE(IsProfiModel(MM_PROFI3));
}

TEST(ProfiBoard_Test, MonochromeHiResOnV3OrWithTheIniKey)
{
    CONFIG config{};
    config.mem_model = MM_PROFI3;
    EXPECT_TRUE(ProfiMonochromeHires(config));
    config.mem_model = MM_PROFI;
    EXPECT_FALSE(ProfiMonochromeHires(config));
    config.profi_monochrome = 1;            // a v5 without its palette chips
    EXPECT_TRUE(ProfiMonochromeHires(config));
}

/// intstart = paper start (T 16152) - INT-to-paper - 1, wrapped into the frame. 0a1d gives today's 3571
TEST(ProfiBoard_Test, SyncPromIntStart)
{
    EXPECT_EQ(ProfiIntStart(ProfiSyncPromFrame(ProfiSyncProm::Default, MM_PROFI3)), 3571u);     // 0a1d
    EXPECT_EQ(ProfiIntStart(ProfiSyncPromFrame(ProfiSyncProm::Default, MM_PROFI)), 1783u);      // v503
    EXPECT_EQ(ProfiIntStart(ProfiSyncPromFrame(ProfiSyncProm::Samx6, MM_PROFI)), 3559u);
    EXPECT_EQ(ProfiIntStart(ProfiSyncPromFrame(ProfiSyncProm::Fb0579b6, MM_PROFI3)), 16103u);
    EXPECT_EQ(ProfiResolveSyncProm(ProfiSyncProm::Default, MM_PROFI3), ProfiSyncProm::Vr0a1d);
    EXPECT_EQ(ProfiResolveSyncProm(ProfiSyncProm::Default, MM_PROFI), ProfiSyncProm::V503);
    EXPECT_EQ(ProfiResolveSyncProm(ProfiSyncProm::Samx6, MM_PROFI), ProfiSyncProm::Samx6);
}
