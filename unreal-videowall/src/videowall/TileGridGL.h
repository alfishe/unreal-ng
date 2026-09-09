#pragma once

#include <QOpenGLWindow>
#include <QOpenGLFunctions>
#include <QOpenGLShaderProgram>
#include <QOpenGLTexture>
#include <QImage>
#include <vector>
#include <memory>
#include <atomic>
#include <3rdparty/message-center/eventqueue.h>

#include "TileLayoutManager.h"

class Emulator;
class EmulatorTile;

/// GPU-accelerated grid renderer using a single QOpenGLWindow.
/// Renders all emulator tiles to one surface for maximum efficiency on 4K displays.
class TileGridGL : public QOpenGLWindow, protected QOpenGLFunctions
{
    Q_OBJECT

public:
    explicit TileGridGL(QWindow* parent = nullptr);
    ~TileGridGL() override;

    /// Add an emulator to the grid (creates internal tile state)
    void addEmulator(std::shared_ptr<Emulator> emulator);

    /// Remove an emulator from the grid
    void removeEmulator(const std::string& emulatorId);

    /// Clear all emulators
    void clearAllEmulators();

    /// Get emulator count
    int emulatorCount() const { return static_cast<int>(_emulators.size()); }

    /// Get emulator at index
    std::shared_ptr<Emulator> emulatorAt(int index) const;

    /// Get all emulator IDs
    std::vector<std::string> emulatorIds() const;

    /// Set explicit grid dimensions
    void setGridDimensions(int cols, int rows);

    /// Set fullscreen mode
    void setFullscreenMode(bool fullscreen) { _isFullscreen = fullscreen; }

    /// Set single sync mode (all tiles show same frame)
    void setSingleSyncMode(bool enable);

    /// Set the emulator ID for frame synchronization
    void setSyncEmulatorId(const std::string& emulatorId);

    /// Request repaint of all tiles
    Q_INVOKABLE void repaintAllTiles();

    /// Get focused emulator index (-1 if none)
    int focusedIndex() const { return _focusedIndex; }

    /// Set focused emulator by index
    void setFocusedIndex(int index);

    /// Get emulator at position
    int emulatorIndexAt(const QPoint& pos) const;

signals:
    /// Emitted when a tile is clicked
    void tileClicked(int index);

    /// Emitted when a file is dropped on a tile
    void fileDropped(int tileIndex, const QString& filePath);

protected:
    void initializeGL() override;
    void resizeGL(int w, int h) override;
    void paintGL() override;

    void keyPressEvent(QKeyEvent* event) override;
    void keyReleaseEvent(QKeyEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    bool event(QEvent* event) override;

private:
    void updateLayout();
    void updateTextures();
    void subscribeToNotifications();
    void unsubscribeFromNotifications();

    struct TileState
    {
        std::shared_ptr<Emulator> emulator;
        std::string emulatorId;
        QOpenGLTexture* texture = nullptr;
        QImage latchedFrame;
        bool needsUpdate = true;
    };

    std::vector<TileState> _emulators;

    QOpenGLShaderProgram* _shader = nullptr;

    int _explicitCols = -1;
    int _explicitRows = -1;
    int _currentCols = 0;
    int _currentRows = 0;

    int _focusedIndex = -1;
    int _dragHoverIndex = -1;

    bool _isFullscreen = false;
    bool _singleSyncMode = false;
    std::string _syncEmulatorId;

    std::atomic<bool> _isRepaintPending{false};
    uint64_t _videoFrameObserverId = 0;

    static constexpr int FB_WIDTH = 352;
    static constexpr int FB_HEIGHT = 288;
};
