#include "videowall/TileGrid.h"

#include <emulatormanager.h>
#include <emulator/notifications.h>
#include <3rdparty/message-center/messagecenter.h>

#include <QResizeEvent>

#include "videowall/EmulatorTile.h"
#include "videowall/TileLayoutManager.h"
#include "videowall/VideowallRecorder.h"

TileGrid::TileGrid(QWidget* parent) : QWidget(parent) {}

TileGrid::~TileGrid()
{
    unsubscribeFromNotifications();
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

void TileGrid::removeTile(EmulatorTile* tile, bool skipLayout)
{
    if (!tile)
        return;

    auto it = std::find(_tiles.begin(), _tiles.end(), tile);
    if (it != _tiles.end())
    {
        _tiles.erase(it);

        if (tile->emulator())
        {
            std::string emulatorId = tile->emulator()->GetUUID().toString();
            tile->prepareForDeletion();
            
            EmulatorManager* manager = EmulatorManager::GetInstance();
            if (manager && !emulatorId.empty())
            {
                manager->StopEmulator(emulatorId);
                manager->RemoveEmulator(emulatorId);
            }
        }
        else
        {
            tile->prepareForDeletion();
        }

        tile->deleteLater();
        
        // Skip layout during batch removal to prevent crashes
        if (!skipLayout)
        {
            updateLayout();
        }
    }
}

void TileGrid::clearAllTiles()
{
    EmulatorManager* manager = EmulatorManager::GetInstance();

    // PHASE 1: Pre-stop all emulator threads before releasing any.
    // This prevents race conditions where one emulator is being released while
    // another's thread is still running and potentially accessing shared resources.
    for (EmulatorTile* tile : _tiles)
    {
        if (tile && tile->emulator())
        {
            if (manager)
            {
                manager->StopEmulator(tile->emulator()->GetUUID());
            }
        }
    }

    // PHASE 2: Release emulators and delete tiles
    for (EmulatorTile* tile : _tiles)
    {
        if (tile)
        {
            // Get emulator ID before clearing the reference
            std::string emulatorId;
            if (tile->emulator())
            {
                emulatorId = tile->emulator()->GetUUID();
            }

            // Clear tile's reference to emulator before destroying it
            tile->prepareForDeletion();

            // Destroy the emulator instance via EmulatorManager
            if (!emulatorId.empty() && manager)
            {
                manager->RemoveEmulator(emulatorId);
            }

            // Now delete the tile widget (shared_ptr already cleared)
            tile->deleteLater();
        }
    }
    _tiles.clear();
    _focusedTile = nullptr;
}

void TileGrid::updateLayout()
{
    // Prevent re-entrant calls (e.g., from resizeEvent triggered by setMinimumSize)
    if (_inUpdateLayout)
    {
        return;
    }
    _inUpdateLayout = true;

    if (_tiles.empty())
    {
        _inUpdateLayout = false;
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
        if (!tile)  // Safety check
        {
            continue;
        }
        tile->setFixedSize(TILE_WIDTH, TILE_HEIGHT); // Restore fixed size constraint
        tile->move(x, y);
        tile->show(); // Ensure it's visible

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
        resize(windowWidth, windowHeight);
    }

    _inUpdateLayout = false;
}

void TileGrid::setSingleSyncMode(bool enable, const std::string& primaryEmulatorId)
{
    _singleSyncMode = enable;
    _primaryEmulatorId = primaryEmulatorId;
    
    unsubscribeFromNotifications();
    if (_singleSyncMode && !_primaryEmulatorId.empty())
    {
        subscribeToNotifications();
    }
    updateLayout();
}

void TileGrid::subscribeToNotifications()
{
    _videoFrameCallback = [this](int id, Message* message) {
        if (_singleSyncMode && message && message->obj) {
            auto* payload = dynamic_cast<EmulatorFramePayload*>(message->obj);
            if (payload && payload->_emulatorId.toString() == _primaryEmulatorId) {
                // Drop frame if UI is still rendering the previous one (prevents event queue flooding)
                bool expected = false;
                if (_isRepaintPending.compare_exchange_strong(expected, true)) {
                    QMetaObject::invokeMethod(this, [this]() {
                        repaintAllTiles();
                        _isRepaintPending = false;
                    }, Qt::QueuedConnection);
                }
            }
        }
    };
    MessageCenter::DefaultMessageCenter().AddObserver(NC_VIDEO_FRAME_REFRESH, _videoFrameCallback);
}

void TileGrid::unsubscribeFromNotifications()
{
    if (_videoFrameCallback)
    {
        MessageCenter::DefaultMessageCenter().RemoveObserver(NC_VIDEO_FRAME_REFRESH, _videoFrameCallback);
        _videoFrameCallback = nullptr;
    }
}

void TileGrid::repaintAllTiles()
{
    for (EmulatorTile* tile : _tiles)
    {
        if (tile && tile->isVisible())
        {
            tile->repaint();
        }
    }

    if (VideowallRecorder::instance().isRecording())
    {
        VideowallRecorder::instance().captureVideoFrameSync();
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
