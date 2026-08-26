#include "devicescreen.h"

#include <QDebug>
#include <QKeyEvent>
#include <QPainter>
#include <cmath>

#include "emulator/emulator.h"
#include "emulator/keyboardmanager.h"
#include "ui_devicescreen.h"
static inline bool isFloatsEqual(float x, float y, float epsilon = 0.01f)
{
    bool result = false;

    if (fabsf(x - y) < epsilon)
        result = true;

    return result;
}

DeviceScreen::DeviceScreen(QWidget* parent) : QWidget(parent), ui(new Ui::DeviceScreen)
{
    ui->setupUi(this);
}

DeviceScreen::~DeviceScreen()
{
    detach();

    delete ui;
}

void DeviceScreen::init(uint16_t width, uint16_t height, void* buffer)
{
    detach();

    ratio = static_cast<float>(width) / static_cast<float>(height);

    devicePixelsRect = QRectF(0.0, 0.0, width, height);
    // RGBX8888: same byte layout as RGBA8888, but alpha is IGNORED (treated as
    // opaque) - the emulated framebuffer has no alpha channel. RGBA8888 made any
    // 0x00-alpha pixel (freshly cleared buffer after a video mode switch) composite
    // transparently, flashing the window background through the frame.
    devicePixels = new QImage(static_cast<const unsigned char*>(buffer), width, height, QImage::Format_RGBX8888);

    // Owned backing store for the tear-free path (filled via _frameSource)
    _latchedFrame = QImage(width, height, QImage::Format_RGBX8888);
    _latchedFrame.fill(Qt::black);

    // Fit the widget into its parent with the new aspect ratio (grow-capable:
    // fixes the 1:1-stuck image after model switch - init() runs outside
    // showEvent/resizeEvent, and the old shrink-only conform could not recover)
    fitToParent();
}

void DeviceScreen::detach()
{
    if (devicePixels)
    {
        delete devicePixels;
        devicePixels = nullptr;
    }

    _frameSource = nullptr;
    _latchedFrame = QImage();

    // Drop our ownership share: a detached screen must not keep a Release()d emulator alive until
    // ~MainWindow (it was the last shared_ptr holder and destroyed the instance long after
    // EmulatorManager::RemoveEmulator - crash on shutdown)
    _emulator.reset();

    // Trigger immediate repaint to show default background when detached
    update();
}

void DeviceScreen::refresh()
{
    // Block updates during shutdown
    if (_isShuttingDown)
    {
        return;
    }

    update();  // Schedule paint event - Qt will read fresh buffer data in paintEvent
}

void DeviceScreen::handleExternalKeyPress(QKeyEvent* event)
{
    keyPressEvent(event);
}

void DeviceScreen::handleExternalKeyRelease(QKeyEvent* event)
{
    keyReleaseEvent(event);
}

void DeviceScreen::paintEvent(QPaintEvent* event)
{
    QPainter painter = QPainter(this);

    // Source rectangle with optional viewport cropping - applies to BOTH
    // paint paths (the tear-free latched frame and the legacy live buffer
    // share the same framebuffer geometry)
    QRectF sourceRect = devicePixelsRect;
    if (_hasViewport)
    {
        sourceRect = QRectF(
            _displayViewport.cropLeft,
            _displayViewport.cropTop,
            devicePixelsRect.width() - _displayViewport.cropLeft - _displayViewport.cropRight,
            devicePixelsRect.height() - _displayViewport.cropTop - _displayViewport.cropBottom
        );
    }

    // Tear-free path: pull the latched full-frame snapshot into our owned
    // backing image (SIMD copy under the screen's present mutex, ~40us),
    // then draw without holding any lock. The legacy path below reads the
    // emulator's live framebuffer and can show a mid-frame seam.
    // Draw into the widget's FULL rect - QPainter clips to event->rect()
    // automatically. Targeting event->rect() squeezed the whole framebuffer
    // into partial dirty rects (move/scroll damage), stretching the image.
    if (_frameSource && !_latchedFrame.isNull() &&
        _frameSource(_latchedFrame.bits(), static_cast<size_t>(_latchedFrame.sizeInBytes())))
    {
#if QT_VERSION >= QT_VERSION_CHECK(5, 13, 0)
        painter.setRenderHint(QPainter::LosslessImageRendering);
#endif
        painter.drawImage(rect(), _latchedFrame, sourceRect);
    }
    else if (devicePixels != nullptr)
    {
#if QT_VERSION >= QT_VERSION_CHECK(5, 13, 0)
        painter.setRenderHint(QPainter::LosslessImageRendering);
#endif
        // Render the emulator screen over the whole widget rect
        painter.drawImage(rect(), *devicePixels, sourceRect);
    }
}

void DeviceScreen::keyPressEvent(QKeyEvent* event)
{
    event->accept();

    // Don't react on auto-repeat
    if (!event->isAutoRepeat())
    {
        quint8 zxKey = KeyboardManager::mapQtKeyToEmulatorKeyWithModifiers(event->key(), event->modifiers());

        // Skip unknown keys
        if (zxKey != 0)
        {
            // Create keyboard event with optional UUID tagging for multi-instance routing
            KeyboardEvent* keyEvent = nullptr;
            if (_emulator)
            {
                // Tag event with emulator UUID for selective routing (multi-instance support)
                std::string targetId = _emulator->GetUUID();
                keyEvent = new KeyboardEvent(static_cast<uint8_t>(zxKey), KEY_PRESSED, targetId);
            }
            else
            {
                // Fallback to broadcast mode (backward compatible)
                keyEvent = new KeyboardEvent(static_cast<uint8_t>(zxKey), KEY_PRESSED);
            }

            // Send valid key combinations to emulator instance
            MessageCenter& messageCenter = MessageCenter::DefaultMessageCenter();
            messageCenter.Post(MC_KEY_PRESSED, keyEvent);
        }

        QString message = QString("DeviceScreen : keyPressEvent, key : 0x%1 (%2), mods: 0x%3, zxKey: 0x%4")
                              .arg(event->key(), 2, 16)
                              .arg(event->key())
                              .arg((int)event->modifiers(), 2, 16)
                              .arg(zxKey, 2, 16);
        qDebug() << message;
    }
}

void DeviceScreen::keyReleaseEvent(QKeyEvent* event)
{
    event->accept();

    // Don't react on auto-repeat
    if (!event->isAutoRepeat())
    {
        quint8 zxKey = KeyboardManager::mapQtKeyToEmulatorKeyWithModifiers(event->key(), event->modifiers());

        // Skip unknown keys
        if (zxKey != 0)
        {
            // Create keyboard event with optional UUID tagging for multi-instance routing
            KeyboardEvent* keyEvent = nullptr;
            if (_emulator)
            {
                // Tag event with emulator UUID for selective routing (multi-instance support)
                std::string targetId = _emulator->GetUUID();
                keyEvent = new KeyboardEvent(static_cast<uint8_t>(zxKey), KEY_RELEASED, targetId);
            }
            else
            {
                // Fallback to broadcast mode (backward compatible)
                keyEvent = new KeyboardEvent(static_cast<uint8_t>(zxKey), KEY_RELEASED);
            }

            // Send valid key combinations to emulator instance
            MessageCenter& messageCenter = MessageCenter::DefaultMessageCenter();
            messageCenter.Post(MC_KEY_RELEASED, keyEvent);
        }

        QString message = QString("DeviceScreen : keyReleaseEvent, key : 0x%1 (%2), mods: 0x%3, zxKey: 0x%4")
                              .arg(event->key(), 2, 16)
                              .arg(event->key())
                              .arg((int)event->modifiers(), 2, 16)
                              .arg(zxKey, 2, 16);
        qDebug() << message;
    }
}

void DeviceScreen::mousePressEvent(QMouseEvent* event) {}

void DeviceScreen::conformToAspectRatio()
{
    // Compute the largest ratio-conforming rect inside the current size
    float width = static_cast<float>(size().width());
    float height = static_cast<float>(size().height());

    // Guard against zero/invalid dimensions during early initialization
    if (width < 1 || height < 1 || ratio <= 0)
        return;

    int newWidth;
    int newHeight;

    if (height * ratio < width)
    {
        newWidth = static_cast<int>(height * ratio);
        newHeight = static_cast<int>(height);
    }
    else
    {
        newWidth = static_cast<int>(width);
        newHeight = static_cast<int>(width / ratio);
    }

    if (newWidth > 0 && newHeight > 0)
        resize(newWidth, newHeight);
}

void DeviceScreen::fitToParent()
{
    // Largest ratio-conforming rect inside the parent, centered - GROW-capable
    // (unlike conformToAspectRatio, which only shrinks the current size).
    // Used when the framebuffer geometry changes (model/video-mode switch):
    // init() runs outside showEvent/resizeEvent, so nobody else re-fits us.
    QWidget* parent = parentWidget();
    if (!parent)
    {
        conformToAspectRatio();
        return;
    }

    const int pw = parent->width();
    const int ph = parent->height();
    if (pw < 1 || ph < 1 || ratio <= 0)
        return;

    int newWidth;
    int newHeight;
    if (static_cast<float>(ph) * ratio <= static_cast<float>(pw))
    {
        newHeight = ph;
        newWidth = static_cast<int>(static_cast<float>(ph) * ratio);
    }
    else
    {
        newWidth = pw;
        newHeight = static_cast<int>(static_cast<float>(pw) / ratio);
    }

    if (newWidth < 1 || newHeight < 1)
        return;

    setGeometry(
        (pw - newWidth) / 2,
        (ph - newHeight) / 2,
        newWidth,
        newHeight);
}

void DeviceScreen::resizeEvent(QResizeEvent* event)
{
    conformToAspectRatio();
    QWidget::resizeEvent(event);
}

void DeviceScreen::prepareForShutdown()
{
    qDebug() << "DeviceScreen::prepareForShutdown()";
    _isShuttingDown = true;
}
