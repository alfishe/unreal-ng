#include "devicescreen.h"
#include "emulator/remotecontrolguard.h"
#include "crtbeammodel.h"
#include "crtrenderer.h"

#include <QDebug>
#include <QScreen>
#include <QTimer>
#include <QFocusEvent>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QWheelEvent>
#include <algorithm>
#include <cmath>

#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/io/mouse/mouse.h"
#include "emulator/keyboardmanager.h"
#include "emulator/mousecapturecontroller.h"
#include "ui_devicescreen.h"
/// The picture without CRT effects (CRTRenderer's plain nearest scale)
static const CRTProfileParams kPlainParams = CRTProfileParams::None();

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

void DeviceScreen::init(uint16_t width, uint16_t height, void* buffer, float aspect)
{
    detach();

    // Note: the widget's aspect ratio is the CRT frame's 352x288 regardless of the framebuffer size;
    // larger (overscan) framebuffers are scaled into the same frame. A video card's picture brings its own
    ratio = aspect > 0.0f ? aspect : kNativeRatio;

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
    {
        std::lock_guard<std::mutex> lock(_frameSourceMutex);
        if (devicePixels)
        {
            delete devicePixels;
            devicePixels = nullptr;
        }
        _frameSource = nullptr;
        _nextFrameSource = nullptr;
    }
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

void DeviceScreen::setFrameSource(ScreenFrames source)
{
    _displayRate = source.displayRate;
    smoothmotion::Mode mode = source.smooth;
    const QByteArray forced = qgetenv("UNREAL_SMOOTH_MOTION");
    if (!forced.isEmpty() && !smoothmotion::ParseMode(forced.constData(), mode))
        qWarning() << "DeviceScreen: UNREAL_SMOOTH_MOTION=" << forced << "is not off | blend | interpolate | auto";
    {
        std::lock_guard<std::mutex> lock(_frameSourceMutex);
        _frameSource = std::move(source.at);
        _nextFrameSource = std::move(source.next);
    }
    _smoothAuto = mode == smoothmotion::Mode::Auto;
    _sync = VideoSync::Off;
    _syncReason.clear();
    _smooth.Reset();
    _smooth.SetMode(mode);
    _smoothShown = {};
    updateVideoSync();
}

void DeviceScreen::updateVideoSync()
{
    if (!_smoothAuto)
    {
        _sync = VideoSync::Off;
        _syncReason.clear();
        return;
    }
    const QScreen* display = screen();
    const double displayHz = display ? display->refreshRate() : 0.0;
    const double machineHz = _frameRate.Hz();
    const VideoSync sync = PickVideoSync(false, displayHz, machineHz, _sync);
    _syncReason = "no VRR and no display rate switch on the software screen (gpu on has them)";
    if (sync == VideoSync::Interpolate && machineHz <= 0.0)
        _syncReason += "; the machine's frame rate is not steady yet";
    if (sync == _sync)
        return;
    _sync = sync;
    _smooth.Reset();
    _smoothShown = {};
}

WindowPresentStats DeviceScreen::presentStats() const
{
    WindowPresentStats out;
    out.presenter = "software";
    const QScreen* display = screen();
    out.minRefreshHz = out.maxRefreshHz = display ? display->refreshRate() : 0.0;
    FillSmoothStats(_smooth, smoothActive(), false, out);
    out.sync = VideoSyncName(_sync);
    out.syncReason = _syncReason;
    out.machineHz = _frameRate.Hz();
    out.clock = FrameClockSourceName(_shownClock);
    if (_displayRate == DisplayRate::Match)
    {
        out.displayState = "unchanged";
        out.displayReason = "the software screen does not switch the display's mode (gpu on does)";
    }
    return out;
}

bool DeviceScreen::smoothActive() const
{
    return _smooth.GetMode() != smoothmotion::Mode::Off && _nextFrameSource && !_temporalEnabled &&
           (!_smoothAuto || _sync == VideoSync::Interpolate);
}

void DeviceScreen::clearFrameSource()
{
    // Runs on the MessageCenter worker inside the pre-free drain window:
    // after this returns, a concurrent paint finds both sources empty and
    // touches only owned memory. No Qt calls here (cross-thread safe).
    std::lock_guard<std::mutex> lock(_frameSourceMutex);
    _frameSource = nullptr;
    _nextFrameSource = nullptr;
    if (devicePixels)
    {
        delete devicePixels;
        devicePixels = nullptr;
    }
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
    const QRectF sourceRect = displaySourceRect();

    // Destination is always the full widget - clip to dirty region for efficiency.
    // Using event->rect() as destination would squeeze the entire source into
    // a partial rectangle when sibling widgets (HUD overlay) trigger partial repaints.
    QRect destRect = rect();
    painter.setClipRect(event->rect());

    // Determine which image to draw. Selection runs under the frame-source
    // mutex: clearFrameSource() may revoke both sources from the
    // MessageCenter worker while an instance is being destroyed, so the
    // legacy fallback deep-copies under the lock to keep every post-lock
    // read on owned memory.
    QImage legacyCopy;
    QImage* drawImage = nullptr;
    {
        std::lock_guard<std::mutex> lock(_frameSourceMutex);

        // Tear-free path: pull the latched full-frame snapshot into our owned
        // backing image (SIMD copy under the screen's present mutex, ~40us),
        // then draw without holding any lock. The legacy path below reads the
        // emulator's live framebuffer and can show a mid-frame seam.
        // Draw into the widget's FULL rect - QPainter clips to event->rect()
        // automatically. Targeting event->rect() squeezed the whole framebuffer
        // into partial dirty rects (move/scroll damage), stretching the image.
        const int64_t now = SteadyNowNs();
        PresentFrameInfo info;
        if (_frameSource && !_latchedFrame.isNull() &&
            _frameSource(now, _latchedFrame.bits(), static_cast<size_t>(_latchedFrame.sizeInBytes()), &info))
        {
            drawImage = &_latchedFrame;
            _shownClock = info.source;
            if (_smoothAuto)
            {
                if (info.serial != 0 && info.serial != _meteredSerial && info.targetNs > 0)
                {
                    _meteredSerial = info.serial;
                    _frameRate.Push(info.targetNs, info.source == FrameClockSource::Latch ? 0 : info.periodNs);
                }
                updateVideoSync();
            }
            const bool smooth = smoothActive();
            if (smooth)
            {
                // Each frame once, in order (the screen holds a few)
                if (_nextFrame.size() != _latchedFrame.size())
                    _nextFrame = QImage(_latchedFrame.size(), QImage::Format_RGBX8888);
                for (size_t k = 0; k < smoothmotion::SmoothMotion::kFrames; k++)
                {
                    PresentFrameInfo next;
                    if (!_nextFrameSource(_smooth.LastSerial(), _nextFrame.bits(),
                                          static_cast<size_t>(_nextFrame.sizeInBytes()), &next) ||
                        next.serial == 0)
                        break;
                    _smooth.Push(next.serial, next.targetNs, reinterpret_cast<const uint32_t*>(_nextFrame.constBits()),
                                 _nextFrame.width(), _nextFrame.height());
                }
            }
            // The next frame is due later than this paint: a repaint at its time (frames by time, no vsync here).
            // Smooth motion: a repaint a refresh on, the picture moves in between
            if (info.nextSerial != 0 && !_repaintScheduled)
            {
                _repaintScheduled = true;
                const QScreen* display = screen();
                const double hz = display && display->refreshRate() >= 20.0 ? display->refreshRate() : 60.0;
                const int64_t waitMs =
                    smooth ? std::max<int64_t>(1, static_cast<int64_t>(1000.0 / hz))
                           : std::clamp<int64_t>((info.nextTargetNs - now + 999'999) / 1'000'000, 1, 100);
                QTimer::singleShot(static_cast<int>(waitMs), this, [this] {
                    _repaintScheduled = false;
                    refresh();
                });
            }
        }
        // A frame source is installed: never fall back to devicePixels. It wraps the emulator's framebuffer, which is
        // freed and reallocated when the guest switches video mode (Profi 352x288 <-> 512x240 hi-res, ATM, AlCo)
        // before the queued re-attach runs - a null or dangling image would be drawn / handed to the GPU.
        else if (!_frameSource && devicePixels != nullptr)
        {
            legacyCopy = devicePixels->copy();
            drawImage = &legacyCopy;
        }
    }

    if (!drawImage || drawImage->isNull())
        return;

    // Smooth motion: the picture for now, drawn again only when it is another one
    if (drawImage == &_latchedFrame && smoothActive() && _smooth.Width() == drawImage->width() &&
        _smooth.Height() == drawImage->height())
    {
        const smoothmotion::SmoothMotion::Result plan = _smooth.Plan(SteadyNowNs());
        if (_smoothFrame.size() != drawImage->size())
        {
            _smoothFrame = QImage(drawImage->size(), QImage::Format_RGBX8888);
            _smoothShown = {};
        }
        if (plan.kind != smoothmotion::SmoothMotion::Kind::None &&
            (plan == _smoothShown || _smooth.Draw(plan, reinterpret_cast<uint32_t*>(_smoothFrame.bits()))))
        {
            _smoothShown = plan;
            drawImage = &_smoothFrame;
        }
    }

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

    // CRTRenderer (or a CRTBeamModel for a shader model) at device pixels, drawn 1:1, with or without CRT effects: the
    // GPU screen composes the same picture from the same tables, so the two backends match bit for bit
    {
        const CRTProfileParams& params = _crtEnabled ? _crtParams : kPlainParams;
        const bool model = CRTBeamModel::isModel(params.profile);
        if (!_crtRenderer)
            _crtRenderer = std::make_unique<CRTRenderer>();
        if (model && !_crtModel)
            _crtModel = std::make_unique<CRTBeamModel>();
        const qreal dpr = devicePixelRatioF();
        CRTRenderer::Layout layout;
        layout.outWidth = std::max(1, static_cast<int>(std::lround(destRect.width() * dpr)));
        layout.outHeight = std::max(1, static_cast<int>(std::lround(destRect.height() * dpr)));
        layout.source = sourceRect.toRect().intersected(drawImage->rect());
        layout.frameWidth = drawImage->width();
        layout.frameHeight = drawImage->height();
        if (layout.source.isEmpty())
            return;

        if (model)
        {
            _crtModel->setLayout(layout, params.profile);
            _crtModel->prepare(drawImage->constBits(), static_cast<int>(drawImage->bytesPerLine()));
        }
        else
        {
            _crtRenderer->setLayout(layout, params);
            _crtRenderer->grade(drawImage->constBits(), static_cast<int>(drawImage->bytesPerLine()), params);
        }
        // Opaque in the backing store's byte order: the 1:1 draw is a copy, no conversion, no blending (RGB32 is
        // B G R 0xFF bytes on little-endian; an image with alpha would be blended pixel by pixel)
        constexpr bool kBgra = Q_BYTE_ORDER == Q_LITTLE_ENDIAN;
        constexpr QImage::Format kFormat = kBgra ? QImage::Format_RGB32 : QImage::Format_RGBX8888;
        if (_crtFrame.width() != layout.outWidth || _crtFrame.height() != layout.outHeight ||
            _crtFrame.format() != kFormat)
            _crtFrame = QImage(layout.outWidth, layout.outHeight, kFormat);
        if (model)
            _crtModel->compose(_crtFrame.bits(), static_cast<int>(_crtFrame.bytesPerLine()), kBgra);
        else
            _crtRenderer->compose(_crtFrame.bits(), static_cast<int>(_crtFrame.bytesPerLine()), kBgra);
        _crtFrame.setDevicePixelRatio(dpr);

#if QT_VERSION >= QT_VERSION_CHECK(5, 13, 0)
        painter.setRenderHint(QPainter::LosslessImageRendering);
#endif
        painter.drawImage(destRect.topLeft(), _crtFrame);
    }
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
    setTemporalWeightMode(_temporalWeightMode);  // a new size starts with equal weights: the mode chosen stays
}

void DeviceScreen::setTemporalWeightMode(int mode)
{
    _temporalWeightMode = mode;
    if (mode == 0)
        _frameHistory.setEqualWeights();
    else
        _frameHistory.setExponentialWeights(_temporalDecay);
}

void DeviceScreen::setTemporalDecay(float decay)
{
    _temporalDecay = std::clamp(decay, 0.1f, 0.9f);
    setTemporalWeightMode(_temporalWeightMode);
}

void DeviceScreen::setCRTEffectsEnabled(bool enabled)
{
    _crtEnabled = enabled;

    // Don't auto-select profile - respect current profile selection
    // If profile is None, no effects will be applied even if enabled

    update();
}

void DeviceScreen::setCRTProfile(CRTProfile profile)
{
    _crtParams = CRTProfileParams::FromProfile(profile);
    _crtEnabled = (profile != CRTProfile::None);

    update();
}

void DeviceScreen::setCRTParams(const CRTProfileParams& params)
{
    _crtParams = params;
    update();
}

void DeviceScreen::setCRTProfile(const CRTProfileParams& params)
{
    _crtParams = params;
    _crtEnabled = (params.profile != CRTProfile::None);

    update();
}

QRectF DeviceScreen::displaySourceRect() const
{
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
    return sourceRect;
}

QImage DeviceScreen::grabFramebuffer()
{
    QImage frame;
    QImage grabbed(_latchedFrame.size(), _latchedFrame.format());
    if (_frameSource && !grabbed.isNull() &&
        _frameSource(SteadyNowNs(), grabbed.bits(), static_cast<size_t>(grabbed.sizeInBytes()), nullptr))
    {
        frame = grabbed;  // Tear-free latched frame, the one on screen now
    }
    else if (!_frameSource && devicePixels != nullptr)
    {
        frame = *devicePixels;  // Legacy live buffer (only when no tear-free source exists - see paintEvent)
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

void DeviceScreen::setEmulator(std::shared_ptr<Emulator> emulator)
{
    _emulator = emulator;
}

bool DeviceScreen::event(QEvent* event)
{
    // The machine owns Ctrl combinations (Symbol Shift) and, with a PC keyboard, Alt combinations and the bare
    // F-keys while the view has focus: accepting the ShortcutOverride keeps the shortcut from firing, the key
    // press comes here.
    // Leased: automation may be removing the instance on another thread
    if (event->type() == QEvent::ShortcutOverride)
    {
        const Emulator::ContextLease lease = _emulator ? _emulator->LeaseContext() : Emulator::ContextLease{};
        if (KeyboardManager::machineOwnsKey(static_cast<QKeyEvent*>(event), lease ? lease->pKeyboard : nullptr))
        {
            event->accept();
            return true;
        }
    }
    return QWidget::event(event);
}

void DeviceScreen::keyPressEvent(QKeyEvent* event)
{
    if (KeyboardManager::isFullScreenKey(event))
    {
        event->ignore();
        return;
    }

    event->accept();

    // Mouse capture release key never reaches the ZX keyboard
    if (_mouseCapture && _mouseCapture->handleKeyPress(event))
        return;

    // Don't react on auto-repeat
    // An automation session holds the machine: no keys from the GUI (releases still go: nothing stays held)
    if (RemoteControlGuard::InputBlocked(_emulator.get()))
        return;

    if (!event->isAutoRepeat())
    {
        // Two messages: the ZX key for the matrix, the physical key for a PS/2 machine
        const std::string targetId = _emulator ? std::string(_emulator->GetUUID()) : std::string();
        KeyboardManager::postHostKey(event, KEY_PRESSED, targetId);
    }
}

void DeviceScreen::keyReleaseEvent(QKeyEvent* event)
{
    if (KeyboardManager::isFullScreenKey(event))
    {
        event->ignore();
        return;
    }

    event->accept();

    if (_mouseCapture && _mouseCapture->handleKeyRelease(event))
        return;

    // Don't react on auto-repeat
    if (!event->isAutoRepeat())
    {
        // Two messages: the ZX key for the matrix, the physical key for a PS/2 machine
        const std::string targetId = _emulator ? std::string(_emulator->GetUUID()) : std::string();
        KeyboardManager::postHostKey(event, KEY_RELEASED, targetId);
    }
}

void DeviceScreen::mousePressEvent(QMouseEvent* event)
{
    // An automation session holds the machine: no capture, no clicks from the GUI
    if (RemoteControlGuard::InputBlocked(_emulator.get()))
    {
        event->accept();
        return;
    }
    if (_mouseCapture && _mouseCapture->handleMousePress(event))
        event->accept();
    else
        QWidget::mousePressEvent(event);
}

void DeviceScreen::mouseReleaseEvent(QMouseEvent* event)
{
    if (_mouseCapture && _mouseCapture->handleMouseRelease(event))
        event->accept();
    else
        QWidget::mouseReleaseEvent(event);
}

void DeviceScreen::mouseMoveEvent(QMouseEvent* event)
{
    if (_mouseCapture && _mouseCapture->handleMouseMove(event))
        event->accept();
    else
        QWidget::mouseMoveEvent(event);
}

void DeviceScreen::wheelEvent(QWheelEvent* event)
{
    if (_mouseCapture && _mouseCapture->handleWheel(event))
        event->accept();
    else
        QWidget::wheelEvent(event);
}

void DeviceScreen::focusOutEvent(QFocusEvent* event)
{
    if (_mouseCapture)
        _mouseCapture->handleFocusOut();

    // Keys held while the focus left never send their release here: let a PS/2
    // machine see them go up
    KeyboardManager::postHeldKeyReleases(_emulator ? std::string(_emulator->GetUUID()) : std::string());

    QWidget::focusOutEvent(event);
}

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

    // The fit DeviceScreenGLWindow::drawnPictureSize() draws: both backends show the picture at the same size
    const double windowW = pw;
    const double windowH = ph;
    int newWidth;
    int newHeight;
    if (windowW / windowH > ratio)
    {
        newHeight = ph;
        newWidth = static_cast<int>(windowH * ratio);
    }
    else
    {
        newWidth = pw;
        newHeight = static_cast<int>(windowW / ratio);
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
    // Fitted from the parent, not shrunk from the current size: conforming a size already conformed shrinks it again
    // by a pixel (the rounding of width / ratio), and every resize event walked the picture smaller
    if (parentWidget())
        fitToParent();
    else
        conformToAspectRatio();
    QWidget::resizeEvent(event);

    update();
}

void DeviceScreen::prepareForShutdown()
{
    qDebug() << "DeviceScreen::prepareForShutdown()";
    _isShuttingDown = true;
}
