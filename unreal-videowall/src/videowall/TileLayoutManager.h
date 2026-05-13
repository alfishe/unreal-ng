#pragma once

#include <cmath>

// ============================================================================
// TILE CONFIGURATION - Only change these to adjust display
// ============================================================================
// ZX Spectrum native resolution is 256x192
// Tile size determines scaling: 256x196 = 1:1, 512x384 = 2x, etc.
// static constexpr int TILE_WIDTH = 256;   // Tile width in pixels (15 cols on 4K)
// static constexpr int TILE_HEIGHT = 196;  // Tile height in pixels (12 rows on 4K, ceil(2160/196))
static constexpr int TILE_WIDTH = 512;
static constexpr int TILE_HEIGHT = 384;
// static constexpr int TILE_WIDTH = 768;
// static constexpr int TILE_HEIGHT = 576;
// ============================================================================

// Calculates grid layout for video wall tiles
//
// Grid dimensions are calculated dynamically from actual screen resolution
// and the configured tile size above. Uses ceil(screen / tile) formula to
// fill all available screen space (tiles at edges may extend beyond screen).
//
// Examples with 1x tiles (256x196), cols = ceil(W/256), rows = ceil(H/196):
//   4K (3840x2160):     15 x 12 = 180 tiles
//   QHD (2560x1600):    10 x  9 =  90 tiles
//   FullHD (1920x1080):  8 x  6 =  48 tiles
//
// Examples with 2x tiles (512x384), cols = ceil(W/512), rows = ceil(H/384):
//   4K (3840x2160):      8 x  6 =  48 tiles
//   QHD (2560x1600):     5 x  5 =  25 tiles
//   FullHD (1920x1080):  4 x  3 =  12 tiles
//
// Examples with 3x tiles (768x576), cols = ceil(W/768), rows = ceil(H/576):
//   4K (3840x2160):      5 x  4 =  20 tiles
//   QHD (2560x1600):     4 x  3 =  12 tiles
//   FullHD (1920x1080):  3 x  2 =   6 tiles
class TileLayoutManager
{
public:
    struct GridLayout
    {
        int rows = 0;
        int cols = 0;
        int totalTiles = 0;
        int tileWidth = TILE_WIDTH;
        int tileHeight = TILE_HEIGHT;
        int windowWidth = 0;
        int windowHeight = 0;
    };

    /**
     * @brief Calculate layout to fill screen with tiles
     * @param screenWidth Actual screen width in pixels
     * @param screenHeight Actual screen height in pixels
     * @return GridLayout with dimensions calculated to fill screen
     */
    static GridLayout getFullscreenLayout(int screenWidth, int screenHeight)
    {
        GridLayout layout;
        // Use ceiling division to fill all available space (tiles may be clipped at edges)
        layout.cols = (screenWidth + TILE_WIDTH - 1) / TILE_WIDTH;
        layout.rows = (screenHeight + TILE_HEIGHT - 1) / TILE_HEIGHT;
        layout.totalTiles = layout.cols * layout.rows;
        layout.tileWidth = TILE_WIDTH;
        layout.tileHeight = TILE_HEIGHT;
        layout.windowWidth = layout.cols * TILE_WIDTH;
        layout.windowHeight = layout.rows * TILE_HEIGHT;
        return layout;
    }

    /**
     * @brief Calculate near-square layout for given tile count
     * @param tileCount Number of tiles to arrange
     * @return GridLayout with near-square arrangement
     */
    static GridLayout calculateLayout(int tileCount)
    {
        GridLayout layout;

        if (tileCount <= 0)
        {
            return layout;
        }

        layout.cols = static_cast<int>(std::ceil(std::sqrt(tileCount)));
        layout.rows = static_cast<int>(std::ceil(static_cast<double>(tileCount) / layout.cols));
        layout.totalTiles = tileCount;
        layout.tileWidth = TILE_WIDTH;
        layout.tileHeight = TILE_HEIGHT;
        layout.windowWidth = layout.cols * TILE_WIDTH;
        layout.windowHeight = layout.rows * TILE_HEIGHT;

        return layout;
    }
};
