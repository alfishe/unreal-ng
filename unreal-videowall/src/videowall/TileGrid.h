#pragma once

#include <QWidget>
#include <vector>

class EmulatorTile;

/**
 * @brief Container widget managing the grid of emulator tiles
 *
 * Arranges tiles in a calculated grid layout and handles focus management.
 */
class TileGrid : public QWidget
{
    Q_OBJECT

public:
    explicit TileGrid(QWidget* parent = nullptr);
    ~TileGrid() override;

    /// Add a tile to the grid
    void addTile(EmulatorTile* tile);

    /// Remove a tile from the grid
    void removeTile(EmulatorTile* tile);

    /// Clear all tiles
    void clearAllTiles();

    /// Get all tiles
    const std::vector<EmulatorTile*>& tiles() const
    {
        return _tiles;
    }

    /// Recalculate and apply grid layout
    void updateLayout();

    /// Set explicit grid dimensions (bypasses automatic calculation)
    void setGridDimensions(int cols, int rows);

protected:
    void resizeEvent(QResizeEvent* event) override;

private:
    std::vector<EmulatorTile*> _tiles;
    EmulatorTile* _focusedTile = nullptr;

    // Explicit grid dimensions (if set, overrides automatic calculation)
    int _explicitCols = -1;
    int _explicitRows = -1;
};
