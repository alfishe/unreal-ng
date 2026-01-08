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
};
