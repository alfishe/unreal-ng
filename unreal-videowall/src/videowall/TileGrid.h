#pragma once

#include <QWidget>
#include <vector>
#include <functional>
#include <atomic>
#include <3rdparty/message-center/eventqueue.h>

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

    /// Remove a tile from the grid (set skipLayout=true for batch operations)
    void removeTile(EmulatorTile* tile, bool skipLayout = false);

    /// Clear all tiles
    void clearAllTiles();

    /// Get all tiles
    const std::vector<EmulatorTile*>& tiles() const
    {
        return _tiles;
    }

    /// Get the currently focused tile
    EmulatorTile* focusedTile() const
    {
        return _focusedTile;
    }

    /// Recalculate and apply grid layout
    void updateLayout();

    /// Repaint all child tiles synchronously
    Q_INVOKABLE void repaintAllTiles();

    /// Set explicit grid dimensions (bypasses automatic calculation)
    void setGridDimensions(int cols, int rows);

    /// Set fullscreen mode (disables size constraints that interfere with fullscreen)
    void setFullscreenMode(bool fullscreen)
    {
        _isFullscreen = fullscreen;
    }

    /// Set single sync mode (stretches single tile to fill grid)
    void setSingleSyncMode(bool enable, const std::string& primaryEmulatorId = "");

protected:
    void resizeEvent(QResizeEvent* event) override;

private:
    void subscribeToNotifications();
    void unsubscribeFromNotifications();

    std::vector<EmulatorTile*> _tiles;
    EmulatorTile* _focusedTile = nullptr;

    // Explicit grid dimensions (if set, overrides automatic calculation)
    int _explicitCols = -1;
    int _explicitRows = -1;

    // Re-entry guard for updateLayout
    bool _inUpdateLayout = false;
    
    // Fullscreen mode flag (disables setMinimumSize in updateLayout)
    bool _isFullscreen = false;
    
    // Single sync mode flag
    bool _singleSyncMode = false;
    std::string _primaryEmulatorId;
    std::atomic<bool> _isRepaintPending {false};
    std::function<void(int, Message*)> _videoFrameCallback;
};
