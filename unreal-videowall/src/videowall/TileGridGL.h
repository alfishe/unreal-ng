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
#include <common/threadpool.h>
#include <emulator/video/smoothtile.h>

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
    void detachAllEmulators();  // Release references without destroying emulators

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

    /// Smooth motion on a display that does not run at the machines' rate: the picture between two frames, per
    /// tile (off | blend | interpolate | interpolate-lite). Off (the default) costs nothing: no engine, no frame
    /// kept, the wall draws only when a frame comes, as before
    void setSmoothMotion(smoothmotion::Mode mode);
    smoothmotion::Mode smoothMotion() const { return _smoothMode; }
    struct SmoothStats
    {
        int tiles = 0;        ///< with an engine
        int moving = 0;       ///< drawing between frames at the last refresh
        int steppedDown = 0;  ///< on the cheap interpolation because the full one was too slow
        int tooSlow = 0;      ///< blending because the cheap one was too slow
        int framesOnly = 0;   ///< stepped down to the frames as they are: the wall's refresh did not fit them
        int wallSteps = 0;    ///< times the wall stepped its costliest tiles down
        uint64_t pictures = 0;   ///< tile pictures drawn, all tiles
        uint64_t between = 0;    ///< of them between two frames
        double lastStepMs = 0.0;  ///< the last refresh's smooth motion, all tiles, wall clock
        double maxStepMs = 0.0;
        int threads = 0;
    };
    SmoothStats smoothStats() const;

    /// Get focused emulator index (-1 if none)
    int focusedIndex() const { return _focusedIndex; }

    /// Set focused emulator by index
    void setFocusedIndex(int index);

    /// Get emulator at position
    int emulatorIndexAt(const QPoint& pos) const;

    /// While on, every painted frame is also read back into capturedFrame() (the recorder's source: the GL picture
    /// is not in the widget tree, so QWidget::grab() of the window container shows none of it)
    void setCaptureEnabled(bool enable);
    /// The last painted frame at framebuffer resolution, R,G,B,A top row first; null before the first paint with
    /// capture on
    const QImage& capturedFrame() const { return _captured; }

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
    void drawGrid();
    void readBackFrame();
    void updateLayout();
    void updateTextures();
    struct TileState;
    /// updateTextures with smooth motion on: every tile's picture for the coming refresh, the tiles over the pool
    void updateTexturesSmooth();
    void uploadTile(TileState& tile, const uchar* pixels, int width, int height, const PictureGeometry& geometry);
    int64_t refreshPeriodNs() const;
    QRect calculateTileRect(int index) const;
    void subscribeToNotifications();
    void unsubscribeFromNotifications();

    struct TileState
    {
        std::shared_ptr<Emulator> emulator;
        std::string emulatorId;
        QOpenGLTexture* texture = nullptr;
        QImage latchedFrame;
        double aspect = 4.0 / 3.0;  // Width : height of the picture on the CRT (DisplayGeometry::WindowAspect), kept inside the 4:3 plate
        QRectF source{0.0, 0.0, 1.0, 1.0};  // The working picture (paper) inside the texture, 0..1: where this machine puts it
        bool needsUpdate = true;
        /// Smooth motion's side of the tile; null while the wall's smooth motion is off
        std::unique_ptr<smoothmotion::Tile> smooth;
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

    bool _captureEnabled = false;
    QImage _captured;
    std::vector<uint8_t> _readBack;  ///< the framebuffer as GL returns it, bottom row first

    smoothmotion::Mode _smoothMode = smoothmotion::Mode::Off;
    std::unique_ptr<ThreadPool> _smoothPool;  ///< made with the first tile engine
    std::vector<uint8_t> _smoothStep;         ///< per tile: smoothmotion::Tile::Step of this refresh
    double _smoothLastStepMs = 0.0, _smoothMaxStepMs = 0.0;
    bool _smoothMoving = false;  ///< a tile is between frames: the next refresh draws too
    /// All the tiles' smooth motion took most of a refresh too often (4 refreshes of 20): the costliest quarter of
    /// the tiles steps one level down, again until it fits
    SmoothGuard _smoothWallGuard;
    int _smoothWallSteps = 0;
    std::vector<int> _smoothOrder;

    std::atomic<bool> _isRepaintPending{false};
    uint64_t _videoFrameObserverId = 0;
};
