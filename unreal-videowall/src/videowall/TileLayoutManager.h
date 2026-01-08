#pragma once

#include <cmath>

/**
 * @brief Calculates optimal grid layout for N emulator tiles
 *
 * Determines rows and columns to create a near-square grid arrangement.
 * Each tile is 256x192 pixels.
 */
class TileLayoutManager
{
public:
    struct GridLayout
    {
        int rows = 0;
        int cols = 0;
        int totalTiles = 0;
        int windowWidth = 0;   // cols * 256
        int windowHeight = 0;  // rows * 192
    };

    /**
     * @brief Calculate grid layout for given number of tiles
     * @param tileCount Number of tiles to arrange
     * @return GridLayout struct with calculated dimensions
     */
    static GridLayout calculateLayout(int tileCount)
    {
        GridLayout layout;

        if (tileCount <= 0)
        {
            return layout;
        }

        // Calculate near-square grid
        layout.cols = static_cast<int>(std::ceil(std::sqrt(tileCount)));
        layout.rows = static_cast<int>(std::ceil(static_cast<double>(tileCount) / layout.cols));
        layout.totalTiles = tileCount;

        // Each tile is 256x192 pixels
        layout.windowWidth = layout.cols * 256;
        layout.windowHeight = layout.rows * 192;

        return layout;
    }

    /**
     * @brief Calculate layout for 4K full-screen (15x11 = 165 tiles)
     * @return GridLayout optimized for 4K UHD (3840x2160)
     */
    static GridLayout calculate4KLayout()
    {
        GridLayout layout;
        layout.cols = 15;  // 15 * 256 = 3840 (perfect fit)
        layout.rows = 11;  // 11 * 192 = 2112 (48px gap at top/bottom)
        layout.totalTiles = 165;
        layout.windowWidth = 3840;
        layout.windowHeight = 2112;
        return layout;
    }
};
