#pragma once

#include <QWidget>
#include <memory>

class Emulator;
class QImage;

/**
 * @brief Widget representing a single emulator instance tile
 *
 * Displays the 256x192 ZX Spectrum screen and handles input for one emulator.
 */
class EmulatorTile : public QWidget
{
    Q_OBJECT

public:
    explicit EmulatorTile(std::shared_ptr<Emulator> emulator, QWidget* parent = nullptr);
    ~EmulatorTile() override;

    /// Get the emulator instance bound to this tile
    std::shared_ptr<Emulator> emulator() const
    {
        return _emulator;
    }

    /// Check if this tile has keyboard focus
    bool hasTileFocus() const
    {
        return _hasTileFocus;
    }

protected:
    void paintEvent(QPaintEvent* event) override;
    void focusInEvent(QFocusEvent* event) override;
    void focusOutEvent(QFocusEvent* event) override;
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dropEvent(QDropEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;

private:
    void handleVideoFrameRefresh(int id, void* message);
    void subscribeToNotifications();
    void unsubscribeFromNotifications();
    QImage convertFramebuffer();

    std::shared_ptr<Emulator> _emulator;
    bool _hasTileFocus = false;

    // Fixed tile size: 256x192 pixels (ZX Spectrum screen, no border)
    static constexpr int TILE_WIDTH = 256;
    static constexpr int TILE_HEIGHT = 192;
};
