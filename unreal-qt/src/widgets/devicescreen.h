#ifndef DEVICESCREEN_H
#define DEVICESCREEN_H

#include <QImage>
#include <QWidget>
#include <functional>
#include <memory>

#include "emulator/video/screen.h"  // For DisplayViewport

class Emulator;  // Forward declaration

namespace Ui
{
class DeviceScreen;
}

class DeviceScreen : public QWidget
{
    Q_OBJECT

public:
    explicit DeviceScreen(QWidget* parent = nullptr);
    ~DeviceScreen() override;

public:
    void init(uint16_t width, uint16_t height, void* buffer);
    void detach();

    /// Copies the latched (tear-free) frame into dst; returns true on success.
    using FrameCopyFn = std::function<bool(uint8_t* dst, size_t dstSize)>;

    /// When set, paintEvent pulls frames into an owned backing image via this
    /// callback (Screen::CopyPresentedFramebuffer) instead of reading the
    /// emulator's live framebuffer, which the emulation thread overwrites
    /// concurrently and causes mid-frame tearing.
    void setFrameSource(FrameCopyFn frameSource)
    {
        _frameSource = std::move(frameSource);
    }

public:
    QSize sizeHint() const override
    {
        if (_hasViewport && devicePixels)
        {
            int w = devicePixels->width() - _displayViewport.cropLeft - _displayViewport.cropRight;
            int h = devicePixels->height() - _displayViewport.cropTop - _displayViewport.cropBottom;
            return QSize(w, h);
        }
        return QSize(352, 288);
    }

public slots:
    void refresh();
    void handleExternalKeyPress(QKeyEvent* event);
    void handleExternalKeyRelease(QKeyEvent* event);
    void prepareForShutdown();  // Block refreshes during shutdown

public:
    void setEmulator(std::shared_ptr<Emulator> emulator)
    {
        _emulator = emulator;
    }

    void setDisplayViewport(const DisplayViewport& viewport)
    {
        _displayViewport = viewport;
        _hasViewport = true;

        // Update aspect ratio for viewport dimensions
        if (devicePixels)
        {
            int w = devicePixels->width() - viewport.cropLeft - viewport.cropRight;
            int h = devicePixels->height() - viewport.cropTop - viewport.cropBottom;
            if (h > 0)
                ratio = static_cast<float>(w) / static_cast<float>(h);
        }

        update();  // Trigger repaint with new viewport
    }

    void clearDisplayViewport()
    {
        _hasViewport = false;
        update();
    }

protected:
    void paintEvent(QPaintEvent* event) override;

    void keyPressEvent(QKeyEvent* event) override;
    void keyReleaseEvent(QKeyEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;

    void resizeEvent(QResizeEvent* event) override;
    using QWidget::heightForWidth;  // Bring method declaration from QWidget
    int heightForWidth(int width);

private:
    Ui::DeviceScreen* ui = nullptr;

    QRectF devicePixelsRect;
    QImage* devicePixels = nullptr;
    QImage _latchedFrame;           // Owned backing store filled via _frameSource
    FrameCopyFn _frameSource;       // Tear-free frame provider (empty = legacy live-buffer path)

    float ratio = 352.0f / 288.0f;

    std::shared_ptr<Emulator> _emulator = nullptr;  // Reference to emulator for UUID tagging
    bool _isShuttingDown = false;  // Flag to block refreshes during shutdown

    // Viewport cropping for overscan mode
    DisplayViewport _displayViewport;
    bool _hasViewport = false;
};

#endif  // DEVICESCREEN_H
