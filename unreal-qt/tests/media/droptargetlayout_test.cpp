// The slot chooser's tile layout (media-drop-targets M4)

#include <gtest/gtest.h>

#include "media/core/droptargetlayout.h"

TEST(DropTargetLayout_Test, FourDrivesInOneRowCentred)
{
    // 4 x 156 + 3 x 16 = 672 wide fits a 700-wide area at full size
    const std::vector<DropTile> tiles = LayoutDropTiles(4, 0, 0, 700, 300);
    ASSERT_EQ(tiles.size(), 4u);
    for (const DropTile& tile : tiles)
    {
        EXPECT_EQ(tile.width, 156);
        EXPECT_EQ(tile.height, 168);
        EXPECT_EQ(tile.y, tiles[0].y) << "one row";
    }
    EXPECT_EQ(tiles[0].x, (700 - 672) / 2);
    EXPECT_EQ(tiles[1].x - tiles[0].x, 156 + 16);
    EXPECT_EQ(tiles[0].y, (300 - 168) / 2);
}

TEST(DropTargetLayout_Test, WrapsAndCentresTheLastRow)
{
    // Room for two columns: 3 tiles become 2 + 1, the single one centred
    const std::vector<DropTile> tiles = LayoutDropTiles(3, 10, 20, 340, 400);
    ASSERT_EQ(tiles.size(), 3u);
    EXPECT_EQ(tiles[0].y, tiles[1].y);
    EXPECT_GT(tiles[2].y, tiles[0].y);
    EXPECT_EQ(tiles[2].x + tiles[2].width / 2, 10 + 340 / 2) << "the last row is centred";
}

TEST(DropTargetLayout_Test, ScalesDownInASmallArea)
{
    const std::vector<DropTile> tiles = LayoutDropTiles(2, 0, 0, 200, 100);
    ASSERT_EQ(tiles.size(), 2u);
    EXPECT_LT(tiles[0].height, 168);
    EXPECT_LE(tiles[0].y + tiles[0].height, 100);
    EXPECT_LE(tiles[1].x + tiles[1].width, 200);
    EXPECT_GE(tiles[0].x, 0);
}

TEST(DropTargetLayout_Test, HitTest)
{
    const std::vector<DropTile> tiles = LayoutDropTiles(2, 0, 0, 400, 200);
    ASSERT_EQ(tiles.size(), 2u);
    EXPECT_EQ(DropTileAt(tiles, tiles[1].x + 5, tiles[1].y + 5), 1);
    EXPECT_EQ(DropTileAt(tiles, tiles[0].x + tiles[0].width + 2, tiles[0].y + 5), -1) << "the gap";
    EXPECT_EQ(DropTileAt(tiles, 0, 0), -1);
}

TEST(DropTargetLayout_Test, NothingToLayOut)
{
    EXPECT_TRUE(LayoutDropTiles(0, 0, 0, 400, 300).empty());
    EXPECT_TRUE(LayoutDropTiles(3, 0, 0, 0, 300).empty());
}
