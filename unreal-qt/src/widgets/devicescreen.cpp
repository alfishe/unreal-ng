#include "devicescreen.h"
#include "crtfilter.h"

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

    // Note: the widget's aspect ratio is fixed (352x288) regardless of the framebuffer size;
    // larger (overscan) framebuffers are scaled into the same frame

    devicePixelsRect = QRectF(0.0, 0.0, width, height);
    devicePixels = new QImage(static_cast<const unsigned char*>(buffer), width, height, QImage::Format_RGBA8888);

    // Owned backing store for the tear-free path (filled via _frameSource)
    _latchedFrame = QImage(width, height, QImage::Format_RGBA8888);
    _latchedFrame.fill(Qt::black);
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

    // Clear temporal history
    _frameHistory.clear();

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

    // Destination is always the full widget - clip to dirty region for efficiency.
    // Using event->rect() as destination would squeeze the entire source into
    // a partial rectangle when sibling widgets (HUD overlay) trigger partial repaints.
    QRect destRect = rect();
    painter.setClipRect(event->rect());

    // Determine which image to draw
    QImage* drawImage = nullptr;

    // Tear-free path: pull the latched full-frame snapshot into our owned
    // backing image (SIMD copy under the screen's present mutex, ~40us),
    // then draw without holding any lock. The legacy path below reads the
    // emulator's live framebuffer and can show a mid-frame seam.
    if (_frameSource && !_latchedFrame.isNull() &&
        _frameSource(_latchedFrame.bits(), static_cast<size_t>(_latchedFrame.sizeInBytes())))
    {
        drawImage = &_latchedFrame;
    }
    else if (devicePixels != nullptr)
    {
        drawImage = devicePixels;
    }

    if (!drawImage)
        return;

    // Temporal blending: push frame to history and use blended result
    if (_temporalEnabled)
    {
        int srcWidth = drawImage->width();
        int srcHeight = drawImage->height();

        // Initialize or re-initialize frame history if dimensions changed
        if (_frameHistory.width() != srcWidth || _frameHistory.height() != srcHeight)
        {
            _frameHistory.init(srcWidth, srcHeight, _frameHistory.historySize());
            _blendedFrame = QImage(srcWidth, srcHeight, QImage::Format_RGBA8888);
        }

        _frameHistory.pushFrame(drawImage->bits(), drawImage->sizeInBytes());

        if (_frameHistory.hasMinimumHistory())
        {
            _frameHistory.getBlendedFrame(_blendedFrame.bits(), _blendedFrame.sizeInBytes());
            drawImage = &_blendedFrame;
        }
    }

    // CRT effects: apply SIMD-accelerated filter at OUTPUT resolution
    // Effects like scanlines need output-resolution precision for fine lines
    if (_crtEnabled && _crtFilter && _crtParams.profile != CRTProfile::None)
    {
        int outWidth = destRect.width();
        int outHeight = destRect.height();

        // Reinitialize CRT buffer if output size changed
        if (_crtFrame.width() != outWidth || _crtFrame.height() != outHeight)
        {
            _crtFrame = QImage(outWidth, outHeight, QImage::Format_RGBA8888);
        }

        // Get source dimensions for scale-aware effects
        QRect srcRect = sourceRect.toRect();
        int srcWidth = srcRect.width();
        int srcHeight = srcRect.height();

        // Scale source to output size first - use nearest-neighbor for crisp pixels
        QImage scaled = drawImage->copy(srcRect).scaled(
            outWidth, outHeight, Qt::IgnoreAspectRatio, Qt::FastTransformation);
        scaled = scaled.convertToFormat(QImage::Format_RGBA8888);

        // Apply CRT filter at output resolution with source dimensions for scale-aware effects
        _crtFilter->apply(scaled.bits(), _crtFrame.bits(), outWidth, outHeight,
                          srcWidth, srcHeight, _crtParams);

        // Draw directly - already at output size
#if QT_VERSION >= QT_VERSION_CHECK(5, 13, 0)
        painter.setRenderHint(QPainter::LosslessImageRendering);
#endif
        painter.drawImage(destRect, _crtFrame);
        return;
    }

#if QT_VERSION >= QT_VERSION_CHECK(5, 13, 0)
    painter.setRenderHint(QPainter::LosslessImageRendering);
#endif
    painter.drawImage(destRect, *drawImage, sourceRect);
}

void DeviceScreen::setTemporalBlendingEnabled(bool enabled)
{
    _temporalEnabled = enabled;
    _frameHistory.setBlendingEnabled(enabled);

    // Frame history will be initialized lazily in paintEvent() with actual frame dimensions
    // This handles overscan mode where framebuffer size differs from native display size

    update();
}

void DeviceScreen::setTemporalHistorySize(int frames)
{
    _frameHistory.setHistorySize(frames);
}

void DeviceScreen::setTemporalWeightMode(int mode)
{
    _temporalWeightMode = mode;
    if (mode == 0)
        _frameHistory.setEqualWeights();
    else
        _frameHistory.setExponentialWeights(0.5f);
}

void DeviceScreen::setCRTEffectsEnabled(bool enabled)
{
    _crtEnabled = enabled;

    if (enabled && !_crtFilter)
    {
        _crtFilter = std::make_unique<CRTFilter>();
    }

    // Don't auto-select profile - respect current profile selection
    // If profile is None, no effects will be applied even if enabled

    update();
}

void DeviceScreen::setCRTProfile(CRTProfile profile)
{
    _crtParams = CRTProfileParams::FromProfile(profile);
    _crtEnabled = (profile != CRTProfile::None);

    if (_crtEnabled && !_crtFilter)
    {
        _crtFilter = std::make_unique<CRTFilter>();
    }

    update();
}

void DeviceScreen::setCRTProfile(const CRTProfileParams& params)
{
    _crtParams = params;
    _crtEnabled = (params.profile != CRTProfile::None);

    if (_crtEnabled && !_crtFilter)
    {
        _crtFilter = std::make_unique<CRTFilter>();
    }

    update();
}

QImage DeviceScreen::grabFramebuffer()
{
    QImage frame;
    if (_frameSource && !_latchedFrame.isNull() &&
        _frameSource(_latchedFrame.bits(), static_cast<size_t>(_latchedFrame.sizeInBytes())))
    {
        frame = _latchedFrame;  // Tear-free latched frame (shared bits; copied below)
    }
    else if (devicePixels != nullptr)
    {
        frame = *devicePixels;  // Legacy live buffer
    }
    if (frame.isNull())
        return QImage();

    QRect crop = frame.rect();
    if (_hasViewport)
    {
        crop = QRect(_displayViewport.cropLeft, _displayViewport.cropTop,
                     frame.width() - _displayViewport.cropLeft - _displayViewport.cropRight,
                     frame.height() - _displayViewport.cropTop - _displayViewport.cropBottom);
    }
    // Deep copy in a format every clipboard / PNG writer accepts
    return frame.copy(crop).convertToFormat(QImage::Format_ARGB32);
}

void DeviceScreen::keyPressEvent(QKeyEvent* event)
{
    if (event->key() == Qt::Key_F && (event->modifiers() & (Qt::ControlModifier | Qt::MetaModifier)))
    {
        event->ignore();
        return;
    }

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

        // QString message = QString("DeviceScreen : keyPressEvent, key : 0x%1 (%2), mods: 0x%3, zxKey: 0x%4")
        //                       .arg(event->key(), 2, 16)
        //                       .arg(event->key())
        //                       .arg((int)event->modifiers(), 2, 16)
        //                       .arg(zxKey, 2, 16);
        // qDebug() << message;
    }
}

void DeviceScreen::keyReleaseEvent(QKeyEvent* event)
{
    if (event->key() == Qt::Key_F && (event->modifiers() & (Qt::ControlModifier | Qt::MetaModifier)))
    {
        event->ignore();
        return;
    }

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

        // QString message = QString("DeviceScreen : keyReleaseEvent, key : 0x%1 (%2), mods: 0x%3, zxKey: 0x%4")
        //                       .arg(event->key(), 2, 16)
        //                       .arg(event->key())
        //                       .arg((int)event->modifiers(), 2, 16)
        //                       .arg(zxKey, 2, 16);
        // qDebug() << message;
    }
}

void DeviceScreen::mousePressEvent(QMouseEvent* event) {}

void DeviceScreen::resizeEvent(QResizeEvent* event)
{
    // int oldWidth = event->oldSize().width();
    // int oldHeight = event->oldSize().height();

    float width = static_cast<float>(event->size().width());
    float height = static_cast<float>(event->size().height());
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

    resize(newWidth, newHeight);

    QWidget::resizeEvent(event);

    update();
}

void DeviceScreen::prepareForShutdown()
{
    qDebug() << "DeviceScreen::prepareForShutdown()";
    _isShuttingDown = true;
}
