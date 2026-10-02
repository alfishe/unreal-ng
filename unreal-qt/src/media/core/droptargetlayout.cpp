#include "droptargetlayout.h"

#include <algorithm>

std::vector<DropTile> LayoutDropTiles(int count, int left, int top, int width, int height, const DropTileMetrics& metrics)
{
    std::vector<DropTile> tiles;
    if (count <= 0 || width <= 0 || height <= 0)
        return tiles;

    const int fitting = std::max(1, (width + metrics.gap) / (metrics.tileWidth + metrics.gap));
    const int columns = std::min(count, fitting);
    const int rows = (count + columns - 1) / columns;
    const double needW = columns * metrics.tileWidth + (columns - 1) * metrics.gap;
    const double needH = rows * metrics.tileHeight + (rows - 1) * metrics.gap;
    const double scale = std::min({1.0, width / needW, height / needH});
    const int tileW = static_cast<int>(metrics.tileWidth * scale);
    const int tileH = static_cast<int>(metrics.tileHeight * scale);
    const int gap = static_cast<int>(metrics.gap * scale);
    const int gridHeight = rows * tileH + (rows - 1) * gap;

    for (int i = 0; i < count; i++)
    {
        const int row = i / columns;
        const int inRow = std::min(columns, count - row * columns);
        const int rowWidth = inRow * tileW + (inRow - 1) * gap;
        DropTile tile;
        tile.x = left + (width - rowWidth) / 2 + (i % columns) * (tileW + gap);
        tile.y = top + (height - gridHeight) / 2 + row * (tileH + gap);
        tile.width = tileW;
        tile.height = tileH;
        tiles.push_back(tile);
    }
    return tiles;
}

int DropTileAt(const std::vector<DropTile>& tiles, int x, int y)
{
    for (size_t i = 0; i < tiles.size(); i++)
    {
        if (tiles[i].Contains(x, y))
            return static_cast<int>(i);
    }
    return -1;
}
