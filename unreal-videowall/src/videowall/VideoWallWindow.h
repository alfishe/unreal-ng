#pragma once

#include <QMainWindow>
#include <memory>
#include <vector>

class TileGrid;
class EmulatorManager;

/**
 * @brief Main window for the video wall application
 *
 * Manages multiple emulator instances displayed as a grid of tiles.
 * Supports different window modes: full-screen, frameless, windowed.
 */
class VideoWallWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit VideoWallWindow(QWidget* parent = nullptr);
    ~VideoWallWindow() override;

    /// Add a new emulator tile to the grid
    void addEmulatorTile();

    /// Remove an emulator tile from the grid
    void removeEmulatorTile(int index);

    /// Clear all emulator tiles
    void clearAllTiles();

    /// Load a preset configuration
    void loadPreset(const QString& presetName);

    /// Save current configuration as preset
    void savePreset(const QString& presetName);

    /// Toggle frameless window mode (auto-preset)
    void toggleFramelessMode();

    /// Toggle fullscreen mode (auto-preset)
    void toggleFullscreenMode();

    /// Calculate optimal grid layout for screen size
    void calculateAndApplyOptimalLayout(QSize screenSize);

    // Tile management
    void removeLastTile();

    // Window state restoration
    void restoreSavedEmulators();

    // Async batch creation
    void createEmulatorsAsync(int total);
    void createNextBatch();

    // Platform-specific fullscreen methods
    void toggleFullscreenMacOS();
    void toggleFullscreenWindows();
    void toggleFullscreenLinux();

protected:
    void keyPressEvent(QKeyEvent* event) override;
    void keyReleaseEvent(QKeyEvent* event) override;

private:
    void setupUI();
    void createMenus();
    void createDefaultPresets();

    // UI Components
    TileGrid* _tileGrid = nullptr;

    // Emulator management (singleton, not owned)
    EmulatorManager* _emulatorManager = nullptr;

    // Configuration
    int _currentPresetIndex = -1;

    // Window state
    bool _isFrameless = false;
    bool _isFullscreen = false;

    // Track mode for auto-preset behavior
    enum class WindowMode
    {
        Windowed,   // Manual tile management
        Frameless,  // Auto-preset for frameless
        Fullscreen  // Auto-preset for fullscreen
    };
    WindowMode _windowMode = WindowMode::Windowed;

    // Saved state for restoration when exiting auto-preset modes
    QRect _savedGeometry;
    std::vector<std::string> _savedEmulatorIds;

    // Async batch creation state
    int _pendingEmulators = 0;
    QTimer* _batchTimer = nullptr;
    static constexpr int BATCH_SIZE = 4;
    static constexpr int BATCH_DELAY_MS = 100;
};
