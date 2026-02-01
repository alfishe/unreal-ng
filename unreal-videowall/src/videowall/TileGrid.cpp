#include "videowall/TileGrid.h"

#include <emulatormanager.h>

#include <QResizeEvent>

#include "videowall/EmulatorTile.h"
#include "videowall/TileLayoutManager.h"

TileGrid::TileGrid(QWidget* parent) : QWidget(parent) {}

TileGrid::~TileGrid()
{
    clearAllTiles();
}

void TileGrid::addTile(EmulatorTile* tile)
{
    if (!tile)
        return;

    _tiles.push_back(tile);
    tile->setParent(this);
    tile->show();

    updateLayout();
}

void TileGrid::removeTile(EmulatorTile* tile)
{
    if (!tile)
        return;

    auto it = std::find(_tiles.begin(), _tiles.end(), tile);
    if (it != _tiles.end())
    {
        _tiles.erase(it);
        tile->deleteLater();
        updateLayout();
    }
}

void TileGrid::clearAllTiles()
{
    for (EmulatorTile* tile : _tiles)
    {
        if (tile && tile->emulator())
        {
            // Get the emulator UUID before deleting the tile
            std::string emulatorId = tile->emulator()->GetUUID();

            // Stop and destroy the emulator instance via EmulatorManager
            EmulatorManager* manager = EmulatorManager::GetInstance();
            if (manager)
            {
                manager->RemoveEmulator(emulatorId);
            }

            // Now delete the tile widget
            tile->deleteLater();
        }
    }
    _tiles.clear();
    _focusedTile = nullptr;
}

void TileGrid::updateLayout()
{
    if (_tiles.empty())
    {
        return;
    }

    int cols, rows;

    // Use explicit dimensions if set, otherwise calculate from tile count
    if (_explicitCols > 0 && _explicitRows > 0)
    {
        cols = _explicitCols;
        rows = _explicitRows;
    }
    else
    {
        // Fallback to automatic calculation
        TileLayoutManager::GridLayout layout = TileLayoutManager::calculateLayout(_tiles.size());
        cols = layout.cols;
        rows = layout.rows;
    }

    // Position tiles in grid using configured tile size
    int x = 0;
    int y = 0;
    int col = 0;

    for (EmulatorTile* tile : _tiles)
    {
        tile->move(x, y);

        // Move to next column
        col++;
        x += TILE_WIDTH;

        // If we've filled a row, move to next row
        if (col >= cols)
        {
            col = 0;
            x = 0;
            y += TILE_HEIGHT;
        }
    }

    // Resize widget to fit grid (but NOT in fullscreen mode - size constraints break fullscreen on Linux)
    if (!_isFullscreen)
    {
        int windowWidth = cols * TILE_WIDTH;
        int windowHeight = rows * TILE_HEIGHT;
        setMinimumSize(windowWidth, windowHeight);
    }
}

void TileGrid::setGridDimensions(int cols, int rows)
{
    _explicitCols = cols;
    _explicitRows = rows;
    updateLayout();
}

void TileGrid::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    updateLayout();
}
