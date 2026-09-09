#pragma once

#include <QObject>
#include <QWidget>
#include <QPointer>
#include <memory>
#include <vector>
#include <functional>

class Emulator;
class TileGrid;
class TileGridGL;
class EmulatorTile;

/// Unified wrapper for TileGrid implementations.
/// Supports runtime switching between CPU (TileGrid) and GPU (TileGridGL) backends.
class TileGridWrapper : public QObject
{
    Q_OBJECT

public:
    /// Create wrapper with auto-detection (GPU if available, else CPU)
    explicit TileGridWrapper(QWidget* parent = nullptr);

    /// Create wrapper with explicit mode selection
    TileGridWrapper(QWidget* parent, bool useGPU);

    ~TileGridWrapper();

    /// Get the underlying widget for layout purposes
    QWidget* widget() const { return _widget; }

    /// Returns true if GPU-accelerated rendering is active
    bool isGPUAccelerated() const { return _useGPU; }

    /// Check if GPU acceleration is available on this system
    static bool isGPUAvailable();

    /// Add an emulator tile to the grid
    void addEmulator(std::shared_ptr<Emulator> emulator);

    /// Remove an emulator from the grid
    void removeEmulator(const std::string& emulatorId);

    /// Remove last emulator
    void removeLastEmulator();

    /// Clear all emulators
    void clearAllEmulators();

    /// Get emulator count
    int emulatorCount() const;

    /// Get emulator at index
    std::shared_ptr<Emulator> emulatorAt(int index) const;

    /// Get all emulator IDs (for state preservation during mode switch)
    std::vector<std::string> emulatorIds() const;

    /// Set explicit grid dimensions
    void setGridDimensions(int cols, int rows);

    /// Set fullscreen mode
    void setFullscreenMode(bool fullscreen);

    /// Set single sync mode
    void setSingleSyncMode(bool enable);

    /// Set sync emulator ID
    void setSyncEmulatorId(const std::string& emulatorId);

    /// Request repaint
    void repaintAllTiles();

    /// Get focused emulator index
    int focusedIndex() const;

    /// Set focused emulator
    void setFocusedIndex(int index);

signals:
    /// Emitted when a tile is clicked (index-based for both modes)
    void tileClicked(int index);

    /// Emitted when a file is dropped on a tile
    void fileDropped(int tileIndex, const QString& filePath);

private:
    void setupConnections();

    QPointer<QWidget> _widget;
    TileGrid* _cpuGrid = nullptr;
    TileGridGL* _gpuGrid = nullptr;
    bool _useGPU = false;

    int _explicitCols = -1;
    int _explicitRows = -1;
    bool _isFullscreen = false;
    bool _singleSyncMode = false;
    std::string _syncEmulatorId;
};
