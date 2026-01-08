#include "videowall/TileGrid.h"

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
        if (tile)
        {
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

    // Calculate grid layout
    TileLayoutManager::GridLayout layout = TileLayoutManager::calculateLayout(_tiles.size());

    // Position tiles in grid
    int x = 0;
    int y = 0;
    int col = 0;

    for (EmulatorTile* tile : _tiles)
    {
        tile->move(x, y);

        // Move to next column
        col++;
        x += 256;

        // If we've filled a row, move to next row
        if (col >= layout.cols)
        {
            col = 0;
            x = 0;
            y += 192;
        }
    }

    // Resize widget to fit grid
    setMinimumSize(layout.windowWidth, layout.windowHeight);
}

void TileGrid::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    updateLayout();
}
