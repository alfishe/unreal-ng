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
    _baseRatio = ratio;
    _frameGeometry = PictureGeometry{};

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
    _smooth.SetLite(false);
    _smoothGuard.StartOver();
    _smoothSteppedDown = _smoothTooSlow = false;
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
    out.vrrSupport = "no";
    out.vrrReason = "the software screen presents on the refresh grid";
    const QScreen* display = screen();
    out.minRefreshHz = out.maxRefreshHz = display ? display->refreshRate() : 0.0;
    FillSmoothStats(_smooth, smoothActive(), false, out);
    FillSmoothStep(_smoothSteppedDown, _smoothTooSlow, out);
    out.sync = VideoSyncName(_sync);
    out.syncReason = _syncReason;
    out.machineHz = _frameRate.Hz();
    out.clock = FrameClockSourceName(_shownClock);
    out.lowLatency = _shownAtOnce;
    _paintTiming.Fill(out);
    if (_displayRate == DisplayRate::Match)
    {
        out.displayState = "unchanged";
        out.displayReason = "the software screen does not switch the display's mode (gpu on does)";
    }
    return out;
}

bool DeviceScreen::smoothActive() const
{
    // Low-latency video shows each frame at once: an interpolated picture would wait for the frame after it
    return _smooth.GetMode() != smoothmotion::Mode::Off && _nextFrameSource && !_temporalEnabled && !_shownAtOnce &&
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

    // A frame came. The frames are shown by time: while a paint waits for the next frame's time, it shows this one (a
    // paint now would show the frame on screen again). Smooth motion paints now all the same (PaintPacer)
    if (!_paintPacer.PaintNow())
        return;
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

const QImage* DeviceScreen::preparePicture(QImage& legacyCopy)
{
    // Determine which image to draw. Selection runs under the frame-source
    // mutex: clearFrameSource() may revoke both sources from the
    // MessageCenter worker while an instance is being destroyed, so the
    // legacy fallback deep-copies under the lock to keep every post-lock
    // read on owned memory.
    QImage* drawImage = nullptr;
    PictureGeometry geometry;
    uint64_t serial = 0;  ///< the frame's serial (0: the legacy buffer, no serial)
    _preparedSerial = 0;
    _preparedFrame = 0;
    _preparedTargetNs = 0;
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
            geometry = info.geometry;  // the geometry latched with the frame: the picture view cuts by it
            serial = info.serial;
            _shownClock = info.source;
            _shownAtOnce = info.atOnce;
            _preparedSerial = info.serial;
            _preparedFrame = info.serial != 0 ? info.geometry.frameNumber : 0;
            _preparedTargetNs = info.targetNs;
            if (_smoothAuto)
            {
                if (info.serial != 0 && info.serial != _meteredSerial && info.targetNs > 0)
                {
                    _meteredSerial = info.serial;
                    // A turbo frame (timed by its latch) says nothing about the machine's rate; a low-latency one is
                    // latched at the machine's own pace
                    if (info.source == FrameClockSource::Latch && !info.atOnce)
                        _frameRate.PushUnpaced();
                    else
                        _frameRate.Push(info.targetNs, info.periodNs);
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
            // Smooth motion: a repaint a refresh on, the picture moves in between (PaintPacer)
            if (info.nextSerial != 0 && !_paintPacer.Scheduled())
            {
                const QScreen* display = screen();
                const double hz = display && display->refreshRate() >= 20.0 ? display->refreshRate() : 60.0;
                const int64_t waitMs =
                    _paintPacer.Schedule(now, smooth, static_cast<int64_t>(1e9 / hz), info.nextTargetNs);
                QTimer::singleShot(static_cast<int>(waitMs), smooth ? Qt::CoarseTimer : Qt::PreciseTimer, this, [this] {
                    _paintPacer.Fired();
                    if (!_isShuttingDown)
                        update();
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
        return nullptr;

    // Smooth motion: the picture for now, drawn again only when it is another one
    if (drawImage == &_latchedFrame && smoothActive() && _smooth.Width() == drawImage->width() &&
        _smooth.Height() == drawImage->height())
    {
        using Kind = smoothmotion::SmoothMotion::Kind;
        const int64_t nowNs = SteadyNowNs();
        // A step down is for a while (SmoothGuard::SteppedDown): one level up again when due
        if ((_smoothTooSlow || _smoothSteppedDown) && _smoothGuard.RetryDue(nowNs))
        {
            if (_smoothTooSlow)
                _smoothTooSlow = false;
            else
            {
                _smoothSteppedDown = false;
                _smooth.SetLite(false);
            }
            _smoothGuard.Reset();
            if (_smoothSteppedDown)
                _smoothGuard.RetryLater(nowNs);
            _smoothShown = {};
            qInfo() << "DeviceScreen: smooth motion tries"
                    << (_smooth.Lite() ? "the cheap interpolation" : "the interpolation") << "again";
        }
        smoothmotion::SmoothMotion::Result plan = _smooth.Plan(nowNs);
        if (_smoothFrame.size() != drawImage->size())
        {
            _smoothFrame = QImage(drawImage->size(), QImage::Format_RGBX8888);
            _smoothShown = {};
        }
        // Too slow here even by the cheap interpolation: the blend of the same two frames; a game's cadence
        // picture: the frame
        const bool interpolates = plan.kind == Kind::Interpolate || plan.kind == Kind::Cadence || plan.kind == Kind::Carry;
        if (_smoothTooSlow && plan.kind == Kind::Interpolate)
            plan.kind = Kind::Blend;
        else if (_smoothTooSlow && interpolates)
            plan = {Kind::Frame, plan.serial, plan.serial, plan.serial, 0};
        bool ready = plan.kind != Kind::None && plan == _smoothShown;
        if (plan.kind != Kind::None && !ready)
        {
            const auto drawStart = std::chrono::steady_clock::now();
            ready = _smooth.Draw(plan, reinterpret_cast<uint32_t*>(_smoothFrame.bits()));
            const int64_t drawNs = std::chrono::duration_cast<std::chrono::nanoseconds>(
                                       std::chrono::steady_clock::now() - drawStart)
                                       .count();
            const QScreen* display = screen();
            const double hz = display && display->refreshRate() > 0.0 ? display->refreshRate() : 60.0;
            if (ready && interpolates && !_smoothTooSlow &&
                _smoothGuard.Note(0, drawNs, static_cast<int64_t>(1e9 / hz)))
            {
                if (!_smooth.Lite())
                {
                    _smoothSteppedDown = true;
                    _smooth.SetLite(true);
                }
                else
                    _smoothTooSlow = true;
                _smoothGuard.SteppedDown(nowNs);
                qWarning() << "DeviceScreen: smooth motion's interpolation is too slow here ("
                           << _smoothGuard.ClosedSlow() << "of its last" << SmoothGuard::kWindow
                           << "pictures drawn in most of a refresh) -"
                           << (_smoothTooSlow ? "blended" : "the cheap interpolation") << "for a while";
            }
        }
        if (ready)
        {
            _smoothShown = plan;
            drawImage = &_smoothFrame;
        }
    }
    if (_pictureView != PictureView::Mode::Full)
    {
        _frameGeometry = geometry;
        applyPictureViewRatio();
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

        // Each frame once: the window paints again without a new frame (a repaint, a screenshot, the recorder's
        // picture), and a frame pushed twice would blend with itself instead of the frame before it
        if (serial == 0 || serial != _historySerial || _frameHistory.frameCount() == 0)
        {
            _frameHistory.pushFrame(drawImage->bits(), drawImage->sizeInBytes());
            _historySerial = serial;
        }

        if (_frameHistory.hasMinimumHistory())
        {
            _frameHistory.getBlendedFrame(_blendedFrame.bits(), _blendedFrame.sizeInBytes());
            drawImage = &_blendedFrame;
        }
    }

    return drawImage;
}

bool DeviceScreen::composePicture(Composer& composer, const QImage& picture, QSize pixels)
{
    // CRTRenderer (or a CRTBeamModel for a shader model) at device pixels, with or without CRT effects: the GPU screen
    // composes the same picture from the same tables, so the two backends match bit for bit
    const CRTProfileParams& params = _crtEnabled ? _crtParams : kPlainParams;
    const bool model = CRTBeamModel::isModel(params.profile);
    if (!composer.renderer)
        composer.renderer = std::make_unique<CRTRenderer>();
    if (model && !composer.model)
        composer.model = std::make_unique<CRTBeamModel>();
    CRTRenderer::Layout layout;
    layout.outWidth = std::max(1, pixels.width());
    layout.outHeight = std::max(1, pixels.height());
    layout.source = displaySourceRect().toRect().intersected(picture.rect());
    layout.frameWidth = picture.width();
    layout.frameHeight = picture.height();
    if (layout.source.isEmpty())
        return false;

    if (model)
    {
        composer.model->setLayout(layout, params.profile);
        composer.model->prepare(picture.constBits(), static_cast<int>(picture.bytesPerLine()));
    }
    else
    {
        composer.renderer->setLayout(layout, params);
        composer.renderer->grade(picture.constBits(), static_cast<int>(picture.bytesPerLine()), params);
    }
    // Opaque in the backing store's byte order: the 1:1 draw is a copy, no conversion, no blending (RGB32 is
    // B G R 0xFF bytes on little-endian; an image with alpha would be blended pixel by pixel)
    constexpr bool kBgra = Q_BYTE_ORDER == Q_LITTLE_ENDIAN;
    constexpr QImage::Format kFormat = kBgra ? QImage::Format_RGB32 : QImage::Format_RGBX8888;
    QImage& frame = composer.frame;
    if (frame.width() != layout.outWidth || frame.height() != layout.outHeight || frame.format() != kFormat)
        frame = QImage(layout.outWidth, layout.outHeight, kFormat);
    if (model)
        composer.model->compose(frame.bits(), static_cast<int>(frame.bytesPerLine()), kBgra);
    else
        composer.renderer->compose(frame.bits(), static_cast<int>(frame.bytesPerLine()), kBgra);
    return true;
}

void DeviceScreen::paintEvent(QPaintEvent* event)
{
    // A new frame's paint time, taken when this paint is done (on every way out); declared before the painter, so the
    // drawing has ended when it is read. Only the window's paints: a screenshot or the recorder's picture is none
    uint64_t paintedSerial = 0;
    int64_t paintedTargetNs = 0;
    struct PaintDone
    {
        PaintTiming& timing;
        const uint64_t& serial;
        const int64_t& targetNs;
        ~PaintDone() { timing.Painted(serial, targetNs, SteadyNowNs()); }
    } paintDone{_paintTiming, paintedSerial, paintedTargetNs};

    QPainter painter = QPainter(this);

    // Destination is always the full widget - clip to dirty region for efficiency.
    // Using event->rect() as destination would squeeze the entire source into
    // a partial rectangle when sibling widgets (HUD overlay) trigger partial repaints.
    const QRect destRect = rect();
    painter.setClipRect(event->rect());

    QImage legacyCopy;
    const QImage* drawImage = preparePicture(legacyCopy);
    paintedSerial = _preparedSerial;
    paintedTargetNs = _preparedTargetNs;
    if (!drawImage)
        return;
    // Drawn 1:1 at device pixels
    const qreal dpr = devicePixelRatioF();
    const QSize pixels(static_cast<int>(std::lround(destRect.width() * dpr)),
                       static_cast<int>(std::lround(destRect.height() * dpr)));
    if (!composePicture(_windowComposer, *drawImage, pixels))
        return;
    _windowComposer.frame.setDevicePixelRatio(dpr);
#if QT_VERSION >= QT_VERSION_CHECK(5, 13, 0)
    painter.setRenderHint(QPainter::LosslessImageRendering);
#endif
    painter.drawImage(destRect.topLeft(), _windowComposer.frame);
}

QImage DeviceScreen::renderPicture(int pictureHeight)
{
    // The window's shape at the height asked: the CRT's lines drawn for that height, as the GPU screen draws them,
    // instead of the window's picture scaled afterwards (scanlines blur into a grey)
    QImage legacyCopy;
    const QImage* drawImage = preparePicture(legacyCopy);
    if (!drawImage || pictureHeight <= 0 || height() <= 0)
        return {};
    const int width = std::max(1, static_cast<int>(std::lround(pictureHeight * static_cast<double>(this->width()) /
                                                               static_cast<double>(height()))));
    if (!composePicture(_pictureComposer, *drawImage, QSize(width, pictureHeight)))
        return {};
    QImage picture = _pictureComposer.frame;
    _pictureComposer.frame = QImage();  // the caller keeps this one: the next picture is drawn into a new image
    return picture;
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

void DeviceScreen::setPictureView(PictureView::Mode mode)
{
    if (mode == _pictureView)
        return;
    _pictureView = mode;
    _frameHistory.clear();
    applyPictureViewRatio();
    update();
}

void DeviceScreen::applyPictureViewRatio()
{
    float wanted = _baseRatio;
    if (_pictureView != PictureView::Mode::Full && !devicePixelsRect.isEmpty())
    {
        const PictureView::Cut cut = PictureView::CutOf(_frameGeometry, static_cast<uint16_t>(devicePixelsRect.width()),
                                                        static_cast<uint16_t>(devicePixelsRect.height()), _pictureView);
        if (cut.aspect > 0.0)
            wanted = static_cast<float>(cut.aspect);
    }
    if (wanted != ratio)
    {
        ratio = wanted;
        fitToParent();  // the widget's shape is the picture's
    }
}

QRectF DeviceScreen::displaySourceRect() const
{
    if (_pictureView != PictureView::Mode::Full && !devicePixelsRect.isEmpty())
    {
        const PictureView::Cut cut = PictureView::CutOf(_frameGeometry, static_cast<uint16_t>(devicePixelsRect.width()),
                                                        static_cast<uint16_t>(devicePixelsRect.height()), _pictureView);
        return QRectF(cut.source.x, cut.source.y, cut.source.width, cut.source.height);
    }
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
