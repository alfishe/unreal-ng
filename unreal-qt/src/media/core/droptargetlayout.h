#pragma once

/// @file droptargetlayout.h
/// @brief Where the slot chooser's tiles go (Qt-free, tested in hud-core-tests):
/// as many columns as fit at full size, everything scaled down to fit the
/// area, each row centred - the last one too.

#include <vector>

struct DropTile
{
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;

    bool Contains(int px, int py) const { return px >= x && px < x + width && py >= y && py < y + height; }
};

struct DropTileMetrics
{
    int tileWidth = 156;
    int tileHeight = 168;
    int gap = 16;
};

/// `count` tiles in the area (`left`, `top`, `width`, `height`); empty when the
/// area has no room or there is nothing to lay out
std::vector<DropTile> LayoutDropTiles(int count, int left, int top, int width, int height,
                                      const DropTileMetrics& metrics = {});

/// The tile under (x, y), or -1
int DropTileAt(const std::vector<DropTile>& tiles, int x, int y);
