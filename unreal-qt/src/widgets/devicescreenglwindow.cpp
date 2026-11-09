#include "devicescreenglwindow.h"
#include "emulator/remotecontrolguard.h"
#include "emulator/mousecapturecontroller.h"
#include "hud/qt/hudoverlay.h"

#include <QDebug>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <QPainter>
#include <QDragEnterEvent>
#include <QFocusEvent>
#include <QGuiApplication>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QDropEvent>
#include <QEvent>
#include <QKeyEvent>
#include <QMimeData>
#include <QOpenGLContext>
#include <QOffscreenSurface>
#include <QOpenGLFramebufferObject>
#include <QOpenGLPaintDevice>
#include <QOpenGLPixelTransferOptions>
#include <QScreen>
#include <QSurfaceFormat>
#include <QTimer>
#include <QUrl>
#include <QVector2D>
#include <QVector4D>
#include <cmath>
#include <cstring>

#include "emulator/emulator.h"
#include "platform/displaymodeswitch.h"
#include "platform/nativepresenter.h"
#include "platform/linuxpresent.h"
#ifdef __linux__
#include "platform/linux/x11display.h"
#endif
#include "emulator/emulatorcontext.h"
#include "emulator/keyboardmanager.h"
#include "3rdparty/message-center/messagecenter.h"
#include "crtprofiles.h"

// The vertex shader of every CRT program: the viewport-filling quad from its corners (0, 0) top left .. (1, 1), the
// texture coordinate custom shaders read
static const char* CRT_VERTEX_SHADER = R"(
#version 330
layout(location = 0) in vec2 corner;
uniform vec4 texRect;
out vec2 texCoord;
void main() {
    gl_Position = vec4(corner.x * 2.0 - 1.0, 1.0 - corner.y * 2.0, 0.0, 1.0);
    texCoord = mix(texRect.xy, texRect.zw, corner);
}
)";


namespace
{
/// The native presenter where the platform has one, unless UNREAL_PRESENTER=gl. Linux: the timed GL swap on X11
/// and Wayland (the window picks it or the plain swap by the policy and the feedback, chooseLinuxPresenter)
bool UseNativePresenter()
{
#if defined(__APPLE__) || defined(_WIN32)
    return qEnvironmentVariable("UNREAL_PRESENTER").compare(QStringLiteral("gl"), Qt::CaseInsensitive) != 0;
#elif defined(__linux__)
    return qEnvironmentVariable("UNREAL_PRESENTER").compare(QStringLiteral("gl"), Qt::CaseInsensitive) != 0 &&
           x11::CurrentWindowSystem() != linuxpresent::WindowSystem::Other;
#else
    return false;
#endif
}

/// Native way, occluded or minimized: the poll that notices the window shown again
constexpr int kHiddenPollMs = 100;

/// Linux: why the plain GL swap presents in place of the timed one (after "the native presenter")
[[maybe_unused]] constexpr const char* kNoFeedbackReason =
    "is off: no screen times here (Wayland / EGL, or GLX without GLX_OML_sync_control), the plain GL swap keeps the "
    "refresh grid; present_policy=vrr swaps each frame at its time";

// The last probe (the GUI thread): -1 none yet, 0 no GPU, 1 a hardware one; the renderer it saw
std::atomic<int> gGpuProbe{-1};
QString gGpuRenderer;
}  // namespace

DeviceScreenGLWindow::DeviceScreenGLWindow(QWindow* parent) : QWindow(parent), _native(UseNativePresenter())
{
#ifdef __APPLE__
    setSurfaceType(_native ? QSurface::MetalSurface : QSurface::OpenGLSurface);
#else
    // Windows: the swap chain takes the HWND as it is; a GL surface keeps the fallback to GL open (no interop)
    setSurfaceType(QSurface::OpenGLSurface);
#endif
    setupVSync();
    if (UseNativePresenter())
    {
        bool ok = false;
        const int leadMs = qEnvironmentVariableIntValue("UNREAL_PRESENT_LEAD_MS", &ok);
        if (ok && leadMs >= 1 && leadMs <= 40)
            _presentLeadNs = static_cast<int64_t>(leadMs) * 1'000'000;
        _presentTimer = new QTimer(this);
        _presentTimer->setSingleShot(true);
        _presentTimer->setTimerType(Qt::PreciseTimer);
        connect(_presentTimer, &QTimer::timeout, this, [this]() { renderNative(false); });
    }
    _smoothTraced = smoothmotion::TraceWriter::FromEnvironment(_smoothTrace);
#ifdef UNREAL_TEST_HOOKS
    // UNREAL_TEST_GPU_HANG=gl:<s>: the recovery test, the window's GL work never ends that long from now
    if (const QByteArray hang = qgetenv("UNREAL_TEST_GPU_HANG"); hang.startsWith("gl:"))
        _glHangAtNs = SteadyNowNs() + static_cast<int64_t>(hang.mid(3).toInt()) * 1'000'000'000;
    // UNREAL_TEST_PRESENT_FAIL=<s>: the native presenter's BeginFrame fails from that long from now on (its restarts
    // and the fall back to GL)
    if (const QByteArray fail = qgetenv("UNREAL_TEST_PRESENT_FAIL"); !fail.isEmpty())
        _presentFailAtNs = SteadyNowNs() + static_cast<int64_t>(fail.toInt()) * 1'000'000'000;
#endif
    // [VIDEO] DisplayRate: the display's mode follows the application's focus and the window's display
    connect(qApp, &QGuiApplication::applicationStateChanged, this, [this]() { updateDisplayRate(); });
    connect(this, &QWindow::screenChanged, this, [this]() {
        restoreDisplayRate("the window moved to another display");
        updateDisplayRate();
        updateVideoSync();
    });
    // Twice a second while frames come to a shown window (VRR engaging, the machine's rate); it stops once it is idle
    // or hidden, frames to a shown window start it again (refresh, Expose)
    _syncTimer = new QTimer(this);
    _syncTimer->setInterval(500);
    connect(_syncTimer, &QTimer::timeout, this, [this]() {
        updateVideoSync();
        if (_idleRefreshes > idleLimit() || !isExposed())
            _syncTimer->stop();
    });
}

bool DeviceScreenGLWindow::prepareContext()
{
    if (!_context)
    {
        auto context = std::make_unique<QOpenGLContext>();
        context->setFormat(surfaceFormat());
        context->setShareContext(QOpenGLContext::globalShareContext());
        if (!context->create())
        {
            qWarning() << "DeviceScreenGLWindow: GL context creation failed";
            return false;
        }
        if (_native)
        {
            _offscreen = std::make_unique<QOffscreenSurface>();
            _offscreen->setFormat(context->format());
            _offscreen->create();
        }
        _context = std::move(context);
    }
    if (!makeCurrent())
    {
        // A graphics reset (the GPU hung and the system recovered it, seen on .101) loses the context: every GL
        // object of the screen, and the native presenter, are made anew in a new one
        if (_context->isValid() || !recreateLostContext())
            return false;
    }
    if (!_glInitialized)
    {
        initializeGL();
        _glInitialized = true;
    }
    return true;
}

bool DeviceScreenGLWindow::recreateLostContext()
{
    qWarning() << "DeviceScreenGLWindow: the GL context was lost (a graphics reset); the screen's GL objects and the"
               << "native presenter are made anew";
    // The presenter first, as when it stops presenting: its slots and interop registrations go with the old device.
    // A restart of the native presenter: too many of them and GL presents (renderNative)
    _presenter.reset();
    if (_native && !_recovery.Restart(SteadyNowNs()))
        _presenterFallback = "lost its GL context too often (" + std::to_string(PresenterRecovery::kMaxRestarts + 1) +
                             " graphics resets or restarts within a minute)";
    auto context = std::make_unique<QOpenGLContext>();
    context->setFormat(surfaceFormat());
    context->setShareContext(QOpenGLContext::globalShareContext());
    if (!context->create())
    {
        qWarning() << "DeviceScreenGLWindow: GL context creation failed after the graphics reset";
        return false;
    }
    std::unique_ptr<QOffscreenSurface> offscreen;
    if (_native)
    {
        offscreen = std::make_unique<QOffscreenSurface>();
        offscreen->setFormat(context->format());
        offscreen->create();
    }
    if (!(_native ? context->makeCurrent(offscreen.get()) : handle() && context->makeCurrent(this)))
    {
        qWarning() << "DeviceScreenGLWindow: the new GL context is not made current after the graphics reset";
        return false;
    }
    // The old objects' names died with the old context: deleted in the new, empty one they touch nothing; the Qt
    // objects see another context and leave them
    _gpuTimes.Release(true);
    releaseGLObjects();
    _context = std::move(context);  // the old one goes after its objects
    _offscreen = std::move(offscreen);
    _glInitialized = false;
    _glSize = QSize();
    _smoothShown = {};
    _uploadedSerial = 0;
    _gradedSerial = ~0ull;
    _modelTablesUploaded = false;
    _modelSerial = ~0ull;
    return true;
}

void DeviceScreenGLWindow::releaseGLObjects()
{
    delete _texture;
    _texture = nullptr;
    delete _gradedTexture;
    _gradedTexture = nullptr;
    delete _indexTexture;
    _indexTexture = nullptr;
    delete _factorTexture;
    _factorTexture = nullptr;
    delete _composeShader;
    _composeShader = nullptr;
    _modelGpu.destroy();
    _smooth.SetDevice(nullptr);
    _smoothGpu.Destroy();
    delete _customShader;
    _customShader = nullptr;
    if (_slangRunner)
        _slangRunner->release();
    _slangRunner.reset();
    _quadBuffer.destroy();
    _quadVao.destroy();
    _gpuTimes.Release(false);
    _presenter.reset();  // its GL objects in this context
}

bool DeviceScreenGLWindow::makeCurrent()
{
    if (!_context)
        return false;
    if (_native)
        return _offscreen && _context->makeCurrent(_offscreen.get());
    return handle() && _context->makeCurrent(this);
}

void DeviceScreenGLWindow::doneCurrent()
{
    if (_context)
        _context->doneCurrent();
}

void DeviceScreenGLWindow::update()
{
    if (_isShuttingDown)
        return;
    if (_native)
    {
        // Frames flowing: the next timed present draws the change; else a present at once
        _presentPending = true;
        if (!_presentTimer->isActive())
            _presentTimer->start(0);
        return;
    }
    requestUpdate();
}

void DeviceScreenGLWindow::renderGL()
{
    if (_isShuttingDown || !isExposed() || !prepareContext())
        return;
    const QSize pixels = (QSizeF(size()) * devicePixelRatio()).toSize();
    glBindFramebuffer(GL_FRAMEBUFFER, _context->defaultFramebufferObject());
    if (pixels != _glSize)
    {
        _glSize = pixels;
        resizeGL(pixels.width(), pixels.height());
    }
    updateTexture(refreshTargetNs());
    paintGL();
    _context->swapBuffers(this);
    onFrameSwapped();
}


void DeviceScreenGLWindow::renderNative(bool force)
{
    if (_isShuttingDown || !isExposed() || !handle() || !prepareContext())
    {
        // Once per stall: the surface or the context is not there yet
        if (!_isShuttingDown && !_toldWaiting)
        {
            _toldWaiting = true;
            qInfo() << "DeviceScreenGLWindow: native present waits: exposed" << isExposed() << "handle"
                    << (handle() != nullptr) << "context" << (_context != nullptr);
        }
        return;
    }
    _toldWaiting = false;
    force = force || _presentPending;
    _presentPending = false;
    if (!_presenter)
    {
        if (!_recovery.GlOnly())
#ifdef __linux__
            _presenter = NativePresenter::Create(static_cast<QWindow*>(this));
#else
            _presenter = NativePresenter::Create(reinterpret_cast<void*>(winId()));
#endif
        if (!_presenter)
        {
            const bool restarted = _recovery.RetriesLeft() > 0;
            const PresenterRecovery::CreateFailure next =
                _recovery.GlOnly() ? PresenterRecovery::CreateFailure::FallBack : _recovery.CreateFailed();
            if (next == PresenterRecovery::CreateFailure::FallBack)
            {
                if (_presenterFallback.empty())
                    _presenterFallback = restarted ? "was not made again after it stopped (" +
                                                         std::to_string(PresenterRecovery::kRetries) + " tries)"
                                                   : "is not available for the window";
                fallBackToGl(_presenterFallback);
                return;
            }
            // Made anew after it stopped (a GPU reset): the system may still hold the window for a moment
            qWarning() << "DeviceScreenGLWindow: the native presenter not made again yet," << _recovery.RetriesLeft()
                       << "tries left";
#ifdef _WIN32
            if (next == PresenterRecovery::CreateFailure::RetryNewWindow)
            {
                // The old device may hold the window's swap chain for good (a device the GPU reset removed: its
                // swap chain refused, E_ACCESSDENIED, seen on .101): a new native window takes the next one
                qWarning() << "DeviceScreenGLWindow: the window's native handle made anew for the presenter";
                const bool visible = isVisible();
                destroy();
                create();
                setVisible(visible);
            }
#endif
            _presentTimer->start(PresenterRecovery::kRetryMs);
            return;
        }
        _recovery.Created();
#ifdef __linux__
        // Without screen times (Wayland, EGL) the timed swap is for policy vrr only: the plain swap otherwise
        if (!linuxpresent::UseTimedSwap(_presentPolicy, NativePresenter::PresentFeedback() == 1))
        {
            useGlSwap(kNoFeedbackReason);
            return;
        }
#endif
        _presenter->SetPolicy(_presentPolicy);
        // b-vsync on the display's refreshes where the presenter can pace them (macOS: a display link)
        _presenter->SetRefreshHandler(&DeviceScreenGLWindow::onDisplayRefresh, this);
        qInfo().noquote() << "DeviceScreenGLWindow: native presenter, lead" << presentLeadNs() / 1'000'000 << "ms;"
                          << QString::fromStdString(_presenter->Describe());
    }

    // Occluded or minimized: the system would drop every present. Nothing is copied, interpolated or drawn (the GPU
    // rests); a slow poll notices the window shown again, a forced repaint waits for it. Latches keep it polling, a
    // paused machine stops it as an idle one (the window's Expose starts it again)
    if (!_presenter->IsVisible())
    {
        _presentPending = _presentPending || force;
        _presentHidden = true;
        _smoothRefreshNs = 0;
        _idleRefreshes++;
        scheduleNative();
        return;
    }
    _presentHidden = false;

    // The presenter paces (b-vsync on a display link): its tick gives the refresh to draw for, the timer only forced
    // repaints. A wake of the timer left over from before draws nothing. Ticks that stop (the system stops a display
    // link whose view it does not show: an occluded window) leave the timer's way in charge until they come again
    const bool paced = linkPacing();
    const int64_t linkRefreshNs = _linkRefreshNs;
    _linkRefreshNs = 0;
    if (paced && linkRefreshNs == 0 && !force)
    {
        scheduleNative();
        return;
    }

    // The frame due a lead from now: drawn now, shown at its own target
    const int64_t now = SteadyNowNs();
    const uint64_t uploads = _frameSerial;
    const uint64_t shownSerial = _uploadedSerial;
    const int64_t lead = presentLeadNs();
    // Smooth motion on a fixed display: the picture of a refresh, presented half a refresh before it (shown at the
    // first refresh after the time asked for). The lead counts to that time asked, as for a frame's own target: the
    // first refresh whose present is a lead away, drawn now (counted to the refresh itself, every present missed its
    // refresh by one, the lead at its bound)
    const bool smooth = smoothActive();
    int64_t refreshNs = 0;
    int64_t periodNs = 0;
    const int64_t lastRefreshNs = _smoothRefreshNs;
    _smoothRefreshNs = 0;
    int64_t pacedRefreshNs = 0;
    if (paced && linkRefreshNs != 0)
    {
        // The display link's grid: the first refresh a present handed over now can make (asked for half a refresh
        // before it, a lead ahead: the link's own target, a refresh away, came a refresh late for 30-100 % of the
        // presents on the Mac at 60 Hz), the frame (or smooth motion's picture) due when it reaches the screen,
        // presented for it (frame presenter design, section 4.2). One refresh per tick: a lead grown by a step does not
        // ask for the same refresh twice (a queued present shows after the one before)
        const int64_t period = _linkPeriodNs;
        const int64_t ahead = now + lead + period / 2 - linkRefreshNs;
        pacedRefreshNs = linkRefreshNs + (ahead > 0 ? (ahead + period - 1) / period : 0) * period;
        if (_lastPacedRefreshNs != 0 && pacedRefreshNs < _lastPacedRefreshNs + period / 2)
            pacedRefreshNs = _lastPacedRefreshNs + period;
        _lastPacedRefreshNs = pacedRefreshNs;
        if (smooth)
        {
            _smoothRefreshNs = pacedRefreshNs;
            _smoothPeriodNs = period;
        }
        updateTexture(pacedRefreshNs, smooth);
    }
    else if (smooth && _presenter->RefreshGrid(refreshNs, periodNs))
    {
        const int64_t half = periodNs / 2;
        const int64_t ahead = now + lead + half - refreshNs;
        _smoothRefreshNs = refreshNs + (ahead > 0 ? (ahead + periodNs - 1) / periodNs : 0) * periodNs;
        // The wake for the refresh after the last picture comes a timer tick late (whole milliseconds, the event
        // loop): still that refresh while half a refresh of the lead is left, or every other refresh is skipped
        const int64_t following = _smoothRefreshNs - periodNs;
        if (lastRefreshNs != 0 && following > lastRefreshNs + half && following - half - now >= lead - half)
            _smoothRefreshNs = following;
        _smoothPeriodNs = periodNs;
        updateTexture(_smoothRefreshNs, true);
    }
    else
    {
        updateTexture(now + lead, false);
    }
    // New: another serial (or a source without serials uploaded). refresh() marks the texture on every latch, so the
    // frame on screen is uploaded again when the wake comes before the next one is due: presenting that again would
    // start an extra refresh on a VRR display (Windows: 10 % of the frames moved by up to a scan-out). Smooth motion:
    // another picture
    bool newFrame = smooth && _smoothUsed
                        ? _smoothChanged
                        : _frameSerial != uploads && (_uploadedSerial == 0 || _uploadedSerial != shownSerial);
    // Smooth motion presents every refresh: one present shown a refresh late leaves all after it late (a queuing
    // presenter shows each after the one before), the picture a refresh behind and the lag stepping by a refresh each
    // time it changes. A present late since the last one left out: this refresh's left out, the queue drains (the
    // picture before stays a refresh); the presents still queued then come late too, but before the drain
    // Once per run of late presents: when the presents handed over after a drain still come a refresh late (none on
    // time since), the system shows them all a refresh late for a while (the compositor behind under load), evenly;
    // leaving more refreshes out would only stutter (one held picture every few refreshes). Again when the lag grows.
    if (newFrame && _smoothRefreshNs != 0 && _presenter->LastLatePresent() > _smoothDrained)
    {
        const uint32_t lag = _presenter->LastLateLag();
        if (_presenter->LastOnTimePresent() > _smoothDrained || lag > _smoothLag)
        {
            _smoothLag = lag;
            newFrame = false;
        }
        _smoothDrained = _presenter->Presented();
    }
    if (newFrame || force)
    {
        const QSize pixels = (QSizeF(size()) * devicePixelRatio()).toSize();
        const bool failHook = _presentFailAtNs != 0 && now >= _presentFailAtNs;  // UNREAL_TEST_PRESENT_FAIL
        if (!failHook && _presenter->BeginFrame(pixels.width(), pixels.height()) != 0)
        {
            if (pixels != _glSize)
            {
                _glSize = pixels;
                resizeGL(pixels.width(), pixels.height());
            }
            paintGL();
            // A forced repaint of the frame on screen goes at once; a new frame at its target
            int64_t at = newFrame && _shownInfo.targetNs > 0 ? _shownInfo.targetNs : 0;
            // The picture and its time on the frame clock, for the beat and the screen-to-sound offset: the frame, or
            // smooth motion's picture for its refresh
            NativePresenter::TraceInfo trace{_shownInfo.serial, _shownInfo.latchNs, _presentPlannedNs, now,
                                             _shownInfo.serial, at};
            if (newFrame && pacedRefreshNs != 0)
                at = pacedRefreshNs - _linkPeriodNs / 2;  // shown at the first refresh after it: that one
            if (newFrame && _smoothRefreshNs != 0)
            {
                at = _smoothRefreshNs - _smoothPeriodNs / 2;
                trace.picture = ++_smoothPictures;
                trace.targetNs = _smoothRefreshNs;
            }
            _presenter->PresentFrame(at, trace);
            if (const int64_t failedNs = _recovery.Presented(now); failedNs > 0)
                qInfo().noquote() << "DeviceScreenGLWindow: presenting again after" << failedNs / 1'000'000 << "ms,"
                                  << _recovery.Failures() << "pictures not presented";
        }
        else if (pixels.isEmpty())
        {
            // A window of no size has nothing to present
        }
        else
        {
            const int64_t failingSinceNs = _recovery.FailingSinceNs();
            const bool lost = _presenter->Lost();
            const std::string failure = failHook ? "UNREAL_TEST_PRESENT_FAIL" : _presenter->LastFailure();
            switch (_recovery.BeginFailed(now, lost))
            {
                case PresenterRecovery::BeginFailure::First:
                    // Told: a picture not presented is a black or frozen window
                    qWarning().noquote() << "DeviceScreenGLWindow: picture not presented:"
                                         << QString::fromStdString(failure);
                    break;
                case PresenterRecovery::BeginFailure::Wait:
                    break;
                case PresenterRecovery::BeginFailure::Restart:
                    // No framebuffer for a while (seen once on .101 after a snapshot load: the presenter thread idle,
                    // every BeginFrame failing, the window black until a restart), or the device gone: a new
                    // presenter, made on the next paint
                    qWarning().noquote() << "DeviceScreenGLWindow: no picture presented"
                                         << (lost ? QStringLiteral("(the device is gone)")
                                                  : QStringLiteral("for %1 ms").arg((now - failingSinceNs) / 1'000'000))
                                         << "(" << _recovery.Failures() << "tries):" << QString::fromStdString(failure)
                                         << "; the native presenter is made anew";
                    _presenter.reset();
                    update();  // the timer's present at once makes it
                    return;
                case PresenterRecovery::BeginFailure::FallBack:
                    _presenterFallback = "stopped presenting " + std::to_string(PresenterRecovery::kMaxRestarts + 1) +
                                         " times within a minute (" + failure + ")";
                    fallBackToGl(_presenterFallback);
                    return;
            }
        }
    }

    static const bool logStats = qEnvironmentVariableIsSet("UNREAL_PRESENT_LOG");
    if (logStats && now - _statusLogNs > 5'000'000'000)
    {
        _statusLogNs = now;
        qInfo().noquote() << "DeviceScreenGLWindow:" << presenterStatus();
        if (smoothActive())
        {
            const smoothmotion::SmoothMotion::Stats& st = _smooth.GetStats();
            qInfo("DeviceScreenGLWindow: smooth motion %s%s on %s: %llu frames, %llu game pictures, %llu renders "
                  "(%llu blended, %llu interpolated), %llu motion fields, %llu steps on the GPU",
                  smoothmotion::ModeName(_smooth.GetMode()), _smooth.HasGame() ? " + game" : "",
                  _smooth.GetDevice() ? "GPU" : "CPU", static_cast<unsigned long long>(st.frames),
                  static_cast<unsigned long long>(st.pictures), static_cast<unsigned long long>(st.renders),
                  static_cast<unsigned long long>(st.blended), static_cast<unsigned long long>(st.interpolated),
                  static_cast<unsigned long long>(st.estimates), static_cast<unsigned long long>(st.onDevice));
        }
    }
    scheduleNative();
}

#ifdef __linux__
void DeviceScreenGLWindow::useGlSwap(const char* why)
{
    qInfo() << "DeviceScreenGLWindow: the timed GL swap" << why << "- the plain GL swap presents";
    makeCurrent();  // the presenter's GL objects are in the draw context
    _presenter.reset();
    _native = false;
    _glSwapChosen = true;
    _presenterFallback = why;
    _presentTimer->stop();
    _presentHidden = false;
    _smoothRefreshNs = 0;
    requestUpdate();
}

void DeviceScreenGLWindow::chooseLinuxPresenter()
{
    // A presenter given up for failing stays given up
    if (!UseNativePresenter() || (!_native && !_glSwapChosen))
        return;
    const int feedback = NativePresenter::PresentFeedback();
    const bool timed = linuxpresent::UseTimedSwap(_presentPolicy, feedback != 0);
    if (timed && !_native)
    {
        qInfo() << "DeviceScreenGLWindow: present policy" << PresentPolicyName(_presentPolicy)
                << "- the timed GL swap presents";
        _native = true;
        _glSwapChosen = false;
        _presenterFallback.clear();
        if (_context && !_offscreen)
        {
            _offscreen = std::make_unique<QOffscreenSurface>();
            _offscreen->setFormat(_context->format());
            _offscreen->create();
        }
        // The window's surface for the present thread: the draw context leaves it
        if (_context)
            _context->doneCurrent();
        update();
    }
    else if (!timed && _native && feedback == 0)
    {
        useGlSwap(kNoFeedbackReason);
    }
}
#endif

void DeviceScreenGLWindow::fallBackToGl(const std::string& why)
{
    qWarning().noquote() << "DeviceScreenGLWindow: the native presenter" << QString::fromStdString(why)
                         << "- presenting with GL from now on";
    _presenter.reset();  // the context current (renderNative)
    _native = false;
    _presentTimer->stop();
    _presentHidden = false;
    _smoothRefreshNs = 0;
#ifdef __APPLE__
    // The window is a Metal surface: GL presents on a GL one. Not inside the event that got here (an Expose)
    QMetaObject::invokeMethod(
        this,
        [this]() {
            const bool visible = isVisible();
            destroy();
            setSurfaceType(QSurface::OpenGLSurface);
            create();
            setVisible(visible);
            requestUpdate();
        },
        Qt::QueuedConnection);
#else
    requestUpdate();
#endif
}

int DeviceScreenGLWindow::idleLimit() const
{
    return std::max(15, static_cast<int>(1e9 / static_cast<double>(refreshPeriodNs()) / 4.0));
}

void DeviceScreenGLWindow::scheduleNative()
{
    if (_isShuttingDown)
        return;
    const int64_t now = SteadyNowNs();
    int64_t wakeNs = 0;
    const int idleLimit = this->idleLimit();
    if (_presenter)
        _presenter->WantRefreshes(!_presentHidden && _idleRefreshes <= idleLimit);
    if (_presentHidden)
    {
        if (_idleRefreshes > idleLimit)
            return;
        _presentPlannedNs = 0;
        _presentTimer->start(kHiddenPollMs);
        return;
    }
    if (linkPacing())
    {
        // The display link's ticks draw, until the window is idle (WantRefreshes above); each one sets this again,
        // so it wakes only when they stop
        _presentPlannedNs = 0;
        _presentTimer->start(kLinkWatchdogMs);
        return;
    }
    if (_smoothRefreshNs != 0)
    {
        // Smooth motion on a fixed display: the next refresh's picture, while frames come
        if (_idleRefreshes > idleLimit)
            return;
        wakeNs = _smoothRefreshNs + _smoothPeriodNs / 2 - presentLeadNs();  // the next refresh's present, a lead ahead
    }
    else if (_shownInfo.nextSerial != 0 && _shownInfo.nextSerial != _uploadedSerial && _shownInfo.nextTargetNs > 0)
    {
        wakeNs = _shownInfo.nextTargetNs - presentLeadNs();  // the next frame is known: wake for it
    }
    else
    {
        // Nothing timed after the frame on screen: poll a refresh while frames may come, stop after a quarter second
        // idle (a paused machine); the next latch starts it again (refresh)
        if (_idleRefreshes > idleLimit)
            return;
        wakeNs = now + refreshPeriodNs();
    }
    // Rounded up: a wake before the target - lead would find the same frame and spin
    const int64_t delayMs = std::max<int64_t>(0, (wakeNs - now + 999'999) / 1'000'000);
    _presentPlannedNs = wakeNs;
    _presentTimer->start(static_cast<int>(std::min<int64_t>(delayMs, 1000)));
}

QString DeviceScreenGLWindow::presenterStatus() const
{
    if (_presenter)
        return QString::fromStdString(_presenter->Describe());
    if (!_presenterFallback.empty())
        return QStringLiteral("GL swapBuffers: the native presenter %1").arg(QString::fromStdString(_presenterFallback));
    return {};
}

WindowPresentStats DeviceScreenGLWindow::presentStats() const
{
    WindowPresentStats out;
    if (_presenter)
    {
#ifdef __APPLE__
        out.presenter = "metal";
#elif defined(_WIN32)
        out.presenter = "dxgi";
#else
        out.presenter = "gl-timed";
#endif
        const NativePresenter::Stats s = _presenter->GetStats();
        out.minRefreshHz = s.minRefreshHz;
        out.maxRefreshHz = s.maxRefreshHz;
        out.presented = s.presented;
        out.shown = s.shown;
        out.dropped = s.dropped;
        out.late = s.late;
        out.leadMs = s.leadMs;
        out.errorP50Ms = s.errorP50;
        out.errorP99Ms = s.errorP99;
        out.errorMaxMs = s.errorMax;
        out.gpuWaitP50Ms = s.gpuWaitP50;
        out.gpuWaitMaxMs = s.gpuWaitMax;
        out.vrr = s.vrr;
        out.beatFrames = s.beatFrames;
        out.offBeat = s.offBeat;
        out.soundOffsetP50Ms = s.soundOffsetP50;
        out.soundOffsetP99Ms = s.soundOffsetP99;
        out.vrrSupport = s.vrrSupport;
        out.vrrReason = s.vrrReason;
        if (out.vrrSupport.empty())
        {
            // macOS reports the display's range; Windows has no query, the presenter watches the vblanks
            const bool range = s.maxRefreshHz - s.minRefreshHz > s.minRefreshHz * 0.01;
#ifdef __APPLE__
            out.vrrSupport = range ? "yes" : "no";
            out.vrrReason = range ? "" : "the display has a fixed refresh (NSScreen)";
#else
            out.vrrSupport = s.vrr ? "yes" : "unknown";
            out.vrrReason = s.vrr ? "" : "Windows has no query: the presenter watches the vblanks";
#endif
        }
    }
    else
    {
        out.presenter = "gl";
        const QScreen* display = screen();
        out.minRefreshHz = out.maxRefreshHz = display ? display->refreshRate() : 0.0;
#ifdef __linux__
        const linuxpresent::VrrDecision vrr =
            x11::DecideWindowVrr(this, gGpuRenderer.contains(QStringLiteral("NVIDIA"), Qt::CaseInsensitive));
        out.vrrSupport = linuxpresent::VrrSupportName(vrr.support);
        out.vrrReason = vrr.reason;
#else
        out.vrrSupport = "unknown";
        out.vrrReason = "the GL swap presents on the refresh grid";
#endif
    }
    out.fixedDisplay = out.maxRefreshHz - out.minRefreshHz <= out.minRefreshHz * 0.01;
    FillSmoothStats(_smooth, smoothActive(), _smoothOnGpu && _smoothGpu.IsCreated(), out);
    const GpuPassTimes::Times smoothGpu = _gpuTimes.Get(GpuPassTimes::Smooth);
    const GpuPassTimes::Times smoothCpu = _gpuTimes.Get(GpuPassTimes::SmoothCpu);
    const GpuPassTimes::Times pictureGpu = _gpuTimes.Get(GpuPassTimes::Picture);
    out.gpuSmooth = {smoothGpu.p50Ms, smoothGpu.maxMs, smoothGpu.longestMs, smoothGpu.measured};
    out.gpuSmoothCpu = {smoothCpu.p50Ms, smoothCpu.maxMs, smoothCpu.longestMs, smoothCpu.measured};
    out.gpuPicture = {pictureGpu.p50Ms, pictureGpu.maxMs, pictureGpu.longestMs, pictureGpu.measured};
    out.displayState = _displayState;
    out.displayReason = _displayReason;
    out.sync = VideoSyncName(_sync);
    out.syncReason = _syncReason;
    out.displayHz = _displayHz;
    out.originalHz = _originalHz;
    out.machineHz = _frameRate.Hz();
    out.clock = FrameClockSourceName(_shownInfo.source);
    return out;
}

void DeviceScreenGLWindow::setupVSync()
{
    setFormat(surfaceFormat());
}

QSurfaceFormat DeviceScreenGLWindow::surfaceFormat()
{
    // 3.3 core: the slang presets' passes need uint, textureSize, texelFetch and float framebuffers. macOS answers a
    // 3.2+ core request with 4.1 core (its compatibility context stops at 2.1)
    QSurfaceFormat fmt;
    fmt.setRenderableType(QSurfaceFormat::OpenGL);
    fmt.setVersion(3, 3);
    fmt.setProfile(QSurfaceFormat::CoreProfile);
    fmt.setSwapBehavior(QSurfaceFormat::DoubleBuffer);
    fmt.setSwapInterval(1);  // Enable vsync: wait for vertical retrace before swap
    // A graphics reset tells: the context reads as lost (isValid), and the window makes everything anew
    fmt.setOption(QSurfaceFormat::ResetNotification);
    return fmt;
}

bool DeviceScreenGLWindow::probeGPUAcceleration()
{
    // Once until a change asks again (reprobeGPUAcceleration): a context made and its functions resolved take ~80 ms,
    // and the settings published to the automation planes ask it (a stall the screen's present timer showed as 4-5
    // skipped refreshes)
    if (gGpuProbe.load() < 0)
        reprobeGPUAcceleration();
    return gGpuProbe.load() == 1;
}

bool DeviceScreenGLWindow::reprobeGPUAcceleration()
{
    QString renderer;
    const int available = probeGPUAccelerationOnce(renderer) ? 1 : 0;
    const bool changed = available != gGpuProbe.load() || renderer != gGpuRenderer;
    gGpuRenderer = renderer;
    gGpuProbe.store(available);
    return changed;
}

bool DeviceScreenGLWindow::probeGPUAccelerationOnce(QString& rendererName)
{
    QOpenGLContext ctx;
    ctx.setFormat(surfaceFormat());
    if (!ctx.create())
    {
        qInfo() << "DeviceScreenGLWindow: OpenGL context creation failed";
        return false;
    }
    const QSurfaceFormat created = ctx.format();
    if (ctx.isOpenGLES() || created.version() < qMakePair(3, 3) || created.profile() == QSurfaceFormat::CompatibilityProfile)
    {
        qInfo() << "DeviceScreenGLWindow: no OpenGL 3.3 core context (got" << created.majorVersion() << "."
                 << created.minorVersion() << "), using the software screen";
        return false;
    }

    QOffscreenSurface surface;
    surface.create();
    if (!ctx.makeCurrent(&surface))
    {
        qInfo() << "DeviceScreenGLWindow: Failed to make context current";
        return false;
    }

    QOpenGLFunctions* gl = ctx.functions();
    const char* renderer = reinterpret_cast<const char*>(gl->glGetString(GL_RENDERER));
    rendererName = QString::fromLatin1(renderer ? renderer : "");
    const char* vendor = reinterpret_cast<const char*>(gl->glGetString(GL_VENDOR));
    const char* version = reinterpret_cast<const char*>(gl->glGetString(GL_VERSION));

    qInfo() << "DeviceScreenGLWindow: GL Renderer:" << (renderer ? renderer : "unknown");
    qInfo() << "DeviceScreenGLWindow: GL Vendor:" << (vendor ? vendor : "unknown");
    qInfo() << "DeviceScreenGLWindow: GL Version:" << (version ? version : "unknown");

    bool isSoftware = false;
    if (renderer)
    {
        isSoftware = (std::strstr(renderer, "llvmpipe") != nullptr) || (std::strstr(renderer, "softpipe") != nullptr) ||
                     (std::strstr(renderer, "swrast") != nullptr) || (std::strstr(renderer, "Software") != nullptr) ||
                     (std::strstr(renderer, "Microsoft Basic") != nullptr) ||
                     (std::strstr(renderer, "GDI Generic") != nullptr);
    }

    ctx.doneCurrent();

    // A software rasterizer is slower than the CPU path, but it is the only GL in a container or a VM: the GL path
    // and the presenter can be checked there (Mesa llvmpipe on Xvfb)
    if (isSoftware && qEnvironmentVariableIntValue("UNREAL_GPU_ALLOW_SOFTWARE") == 1)
    {
        qInfo() << "DeviceScreenGLWindow: Software renderer allowed (UNREAL_GPU_ALLOW_SOFTWARE=1)";
        isSoftware = false;
    }

    if (isSoftware)
    {
        qInfo() << "DeviceScreenGLWindow: Software renderer detected, using CPU path";
        return false;
    }

    qInfo() << "DeviceScreenGLWindow: Hardware GPU acceleration available";
    return true;
}

DeviceScreenGLWindow::~DeviceScreenGLWindow()
{
    _isShuttingDown = true;  // nothing starts the timers again (detach's update)
    _displaySwitch.reset();  // the display's own mode back
    if (_presentTimer)
        _presentTimer->stop();
    _syncTimer->stop();
    if (!makeCurrent())
    {
        detach();
        return;
    }
    releaseGLObjects();
    doneCurrent();
    detach();
}

void DeviceScreenGLWindow::init(uint16_t width, uint16_t height, void* buffer, float aspect)
{
    detach();
    ratio = aspect > 0.0f ? aspect : kNativeRatio;

    _devicePixelsRect = QRectF(0.0, 0.0, width, height);
    _devicePixels = new QImage(static_cast<const unsigned char*>(buffer), width, height, QImage::Format_RGBA8888);

    _latchedFrame = QImage(width, height, QImage::Format_RGBA8888);
    _latchedFrame.fill(Qt::black);

    _textureNeedsUpdate = true;
}

void DeviceScreenGLWindow::detach()
{
    {
        std::lock_guard<std::mutex> lock(_frameSourceMutex);
        if (_devicePixels)
        {
            delete _devicePixels;
            _devicePixels = nullptr;
        }
        _frameSource = nullptr;
        _nextFrameSource = nullptr;
    }
    _latchedFrame = QImage();
    _emulator.reset();
    _frameHistory.clear();

    update();
}

void DeviceScreenGLWindow::setFrameSource(ScreenFrames source)
{
    smoothmotion::Mode mode = source.smooth;
    const QByteArray forced = qgetenv("UNREAL_SMOOTH_MOTION");
    if (!forced.isEmpty() && !smoothmotion::ParseMode(forced.constData(), mode))
        qWarning() << "DeviceScreenGLWindow: UNREAL_SMOOTH_MOTION=" << forced << "is not off | blend | interpolate | auto";
    {
        std::lock_guard<std::mutex> lock(_frameSourceMutex);
        _frameSource = std::move(source.at);
        _nextFrameSource = std::move(source.next);
    }
    // UNREAL_EFFECTS_DEVICE overrides [VIDEO] EffectsDevice
    _smoothOnGpu = !source.onCpu;
    const QByteArray device = qgetenv("UNREAL_EFFECTS_DEVICE").toLower();
    if (device == "cpu" || device == "gpu")
        _smoothOnGpu = device == "gpu";
    else if (!device.isEmpty())
        qWarning() << "DeviceScreenGLWindow: UNREAL_EFFECTS_DEVICE=" << device << "is not gpu | cpu";
    // UNREAL_PRESENT_POLICY overrides [VIDEO] PresentPolicy
    _presentPolicy = source.policy;
    const QByteArray policy = qgetenv("UNREAL_PRESENT_POLICY");
    if (!policy.isEmpty() && !ParsePresentPolicy(policy.constData(), _presentPolicy))
        qWarning() << "DeviceScreenGLWindow: UNREAL_PRESENT_POLICY=" << policy << "is not auto | vrr | vsync";
    if (_presenter)
        _presenter->SetPolicy(_presentPolicy);
#ifdef __linux__
    chooseLinuxPresenter();
#endif
    _displayRate = source.displayRate;
    _smoothAuto = mode == smoothmotion::Mode::Auto;
    _sync = VideoSync::Off;
    _syncReason.clear();
    _smooth.Reset();
    _smooth.SetMode(mode);
    _smoothShown = {};
    _uploadedSerial = 0;
    updateDisplayRate();
    updateVideoSync();
    if (_smoothAuto)
        _syncTimer->start();
    else
        _syncTimer->stop();
}

void DeviceScreenGLWindow::updateVideoSync()
{
    if (!_smoothAuto)
    {
        _sync = VideoSync::Off;
        _syncReason.clear();
        return;
    }
    bool vrr = false;
    double displayHz = 0.0;
    double presenterMinHz = 0.0;
    if (_presenter)
    {
        const NativePresenter::Stats s = _presenter->GetStats();
        vrr = s.vrr;
        displayHz = s.maxRefreshHz;
        presenterMinHz = s.minRefreshHz;
    }
    if (_displayState == "matched" && _displayHz > 0.0)
        displayHz = _displayHz;
    if (displayHz <= 0.0 && screen())
        displayHz = screen()->refreshRate();
    const double machineHz = _frameRate.Hz();
    // The display rate step follows what the presenter sees now: a VRR it reported before the vblanks showed a fixed
    // grid left "VRR" as the display rate's reason for good
    if (vrr != _syncVrr && _displayRate == DisplayRate::Match)
    {
        _syncVrr = vrr;
        updateDisplayRate();
    }
    const VideoSync sync = PickVideoSync(vrr, displayHz, machineHz, _sync);

    // Why the steps before the one in effect are not
    std::string reason;
    if (sync != VideoSync::Vrr)
    {
        reason = !_presenter && !_presenterFallback.empty()
                     ? "no VRR: the native presenter " + _presenterFallback + ", the window presents with GL"
                 : !_presenter                            ? "no VRR: the window presents with GL (no present-at-time)"
                 : _presentPolicy == PresentPolicy::Vsync ? "no VRR: present policy vsync"
                 : presenterMinHz >= displayHz * 0.99
                     ? "no VRR: the display's refreshes stay on their grid (a fixed display, or its variable refresh "
                       "is off for this window: in the background, a remote session)"
                     : "no VRR: the display's variable refresh did not engage";
        if (sync == VideoSync::Interpolate)
        {
            if (machineHz <= 0.0)
                reason += "; the machine's frame rate is not steady yet";
            else
                reason += "; display rate: " + (_displayReason.empty()
                                                    ? QString::number(displayHz).toStdString() + " Hz does not fit " +
                                                          QString::number(machineHz).toStdString() + " Hz"
                                                    : _displayReason);
        }
    }
    _syncReason = reason;
    if (sync == _sync)
        return;
    qInfo() << "DeviceScreenGLWindow: video sync" << VideoSyncName(_sync) << "->" << VideoSyncName(sync) << "("
            << QString::fromStdString(reason) << ")";
    _sync = sync;
    // Smooth motion on or off: the interpolation starts from the frames that come from now on
    std::lock_guard<std::mutex> lock(_frameSourceMutex);
    _smooth.Reset();
    _smoothShown = {};
}

void DeviceScreenGLWindow::restoreDisplayRate(const char* reason)
{
    if (_displaySwitch && _displaySwitch->Switched())
    {
        _displaySwitch->Restore();
        qInfo() << "DeviceScreenGLWindow: display rate back to" << _originalHz << "Hz:" << reason;
    }
    _displayHz = _originalHz;
    if (_displayRate == DisplayRate::Match)
    {
        _displayState = "unchanged";
        _displayReason = reason;
    }
}

void DeviceScreenGLWindow::updateDisplayRate()
{
    if (_displayRate != DisplayRate::Match)
    {
        restoreDisplayRate("display_rate keep");
        _displayState = "keep";
        _displayReason.clear();
        return;
    }
    if (QGuiApplication::applicationState() != Qt::ApplicationActive)
        return restoreDisplayRate("the emulator is not the active application");
    if (_presenter && _presenter->GetStats().vrr)
        return restoreDisplayRate("the display shows each frame at its own time (VRR)");
    const double machineHz = _frameRate.Hz();
    if (machineHz <= 0.0)
    {
        // Paused, turbo or just started: whatever runs stays
        if (!_displaySwitch || !_displaySwitch->Switched())
        {
            _displayState = "unchanged";
            _displayReason = "the machine's frame rate is not steady yet";
        }
        return;
    }
    if (!_displaySwitch)
        _displaySwitch = DisplayModeSwitch::Create();
    const QScreen* display = screen();
    std::vector<double> rates;
    double currentHz = 0.0;
    if (!_displaySwitch || !display)
        return restoreDisplayRate(DisplayModeSwitch::Unavailable().c_str());
    if (!_displaySwitch->Rates(display, rates, currentHz))
        return restoreDisplayRate("the display's modes are unknown");
    if (!_displaySwitch->Switched())
        _originalHz = currentHz;
    DisplayRateFit pick;
    if (!DisplayRatePick(machineHz, currentHz, rates, pick))
    {
        // The rate running fits as well as any (the switched one, or the display's own)
        if (FitDisplayRate(currentHz, machineHz).error <= kDisplayRateMaxError)
        {
            _displayHz = currentHz;
            _displayState = _displaySwitch->Switched() ? "matched" : "unchanged";
            _displayReason = _displaySwitch->Switched() ? "" : "the display already runs a fitting rate";
            return;
        }
        std::string list;
        for (double hz : rates)
            list += (list.empty() ? "" : ", ") + QString::number(hz).toStdString();
        restoreDisplayRate("");
        _displayReason = "no mode within 5 % of " + QString::number(machineHz).toStdString() +
                         " Hz or its multiples (" + list + ")";
        return;
    }
    std::string error;
    if (!_displaySwitch->Set(display, pick.hz, error))
        return restoreDisplayRate(error.c_str());
    qInfo() << "DeviceScreenGLWindow: display rate" << pick.hz << "Hz for the machine's" << machineHz << "(was"
            << _originalHz << ")";
    _displayHz = pick.hz;
    _displayState = "matched";
    _displayReason.clear();
}

bool DeviceScreenGLWindow::smoothActive() const
{
    return _smooth.GetMode() != smoothmotion::Mode::Off && _nextFrameSource && !_temporalEnabled &&
           (!_smoothAuto || _sync == VideoSync::Interpolate);
}

void DeviceScreenGLWindow::feedSmooth()
{
    // Each frame once, in order. The screen holds a few: a backlog past them is gone anyway. Only the copies here,
    // under the mutex; the engine's work on them (its flicker search) in pushSmooth
    uint64_t last = _smoothFed > 0 ? _smoothFeedInfo[_smoothFed - 1].serial : _smooth.LastSerial();
    while (_smoothFed < _smoothFeed.size())
    {
        QImage& frame = _smoothFeed[_smoothFed];
        if (frame.size() != _latchedFrame.size())
            frame = QImage(_latchedFrame.size(), QImage::Format_RGBA8888);
        PresentFrameInfo next;
        if (!_nextFrameSource(last, frame.bits(), static_cast<size_t>(frame.sizeInBytes()), &next) || next.serial == 0)
            break;
        _smoothFeedInfo[_smoothFed++] = next;
        last = next.serial;
    }
}

void DeviceScreenGLWindow::pushSmooth()
{
    for (size_t k = 0; k < _smoothFed; k++)
    {
        const QImage& frame = _smoothFeed[k];
        const PresentFrameInfo& info = _smoothFeedInfo[k];
        _smooth.Push(info.serial, info.targetNs, reinterpret_cast<const uint32_t*>(frame.constBits()), frame.width(),
                     frame.height());
        _smoothTrace.Frame(info.serial, info.targetNs, reinterpret_cast<const uint32_t*>(frame.constBits()),
                           frame.width(), frame.height());
    }
    _smoothFed = 0;
}

void DeviceScreenGLWindow::clearFrameSource()
{
    // Runs on the MessageCenter worker inside the pre-free drain window:
    // after this returns, a concurrent paint finds both sources empty and
    // touches only owned memory. No Qt calls here (cross-thread safe).
    std::lock_guard<std::mutex> lock(_frameSourceMutex);
    _frameSource = nullptr;
    _nextFrameSource = nullptr;
    if (_devicePixels)
    {
        delete _devicePixels;
        _devicePixels = nullptr;
    }
}

void DeviceScreenGLWindow::refresh()
{
    if (_isShuttingDown)
        return;

    // A source without serials (the legacy buffer, a ZX-Poly display) has a new frame now
    _textureNeedsUpdate = true;
    _idleRefreshes = 0;
    if (_smoothAuto && !_syncTimer->isActive() && isExposed())
        _syncTimer->start();
    if (_native)
    {
        // The display link runs while frames come; started again after an idle stop (the timer's way draws until it
        // ticks)
        if (_presenter && _presenter->PacesRefreshes())
        {
            _presenter->WantRefreshes(true);
            if (linkPacing())
                return;
        }
        // The timer runs while frames come; started again after an idle stop
        if (!_presentTimer->isActive())
            _presentTimer->start(0);
        return;
    }
    requestUpdate();  // one update request however many come before it
}

void DeviceScreenGLWindow::onFrameSwapped()
{
    // Frames by time (frame presenter design, section 4.2): each refresh draws the frame due when it reaches the
    // screen, so the window repaints on every refresh while frames come. A quarter second with nothing new and nothing
    // pending stops it (a paused machine); the next latch starts it again
    if (_isShuttingDown || !isExposed())
        return;
    if (_idleRefreshes <= idleLimit())
        requestUpdate();
}

void DeviceScreenGLWindow::onDisplayRefresh(void* context, int64_t refreshNs, int64_t periodNs)
{
    auto* self = static_cast<DeviceScreenGLWindow*>(context);
    self->_linkRefreshNs = refreshNs;
    self->_linkPeriodNs = periodNs;
    self->_lastLinkTickNs = SteadyNowNs();
    self->renderNative(false);
    self->_linkRefreshNs = 0;  // a tick that drew nothing (a stall) leaves none behind for a timer's wake
}

bool DeviceScreenGLWindow::linkPacing() const
{
    return _presenter && _presenter->PacesRefreshes() &&
           (_linkRefreshNs != 0 || SteadyNowNs() - _lastLinkTickNs < kLinkWatchdogMs * 1'000'000);
}

int64_t DeviceScreenGLWindow::refreshPeriodNs() const
{
    const QScreen* display = screen();
    const double hz = display ? display->refreshRate() : 60.0;
    return static_cast<int64_t>(1e9 / (hz >= 20.0 && hz <= 1000.0 ? hz : 60.0));
}

int64_t DeviceScreenGLWindow::presentLeadNs() const
{
    if (_presentLeadNs > 0)
        return _presentLeadNs;
    return _presenter ? _presenter->LeadNs(refreshPeriodNs()) : refreshPeriodNs() + 6'000'000;
}

int64_t DeviceScreenGLWindow::refreshTargetNs() const
{
    return SteadyNowNs() + refreshPeriodNs();
}

void DeviceScreenGLWindow::prepareForShutdown()
{
    qDebug() << "DeviceScreenGLWindow::prepareForShutdown()";
    _isShuttingDown = true;
}

void DeviceScreenGLWindow::setDisplayViewport(const DisplayViewport& viewport)
{
    _displayViewport = viewport;
    _hasViewport = true;
    _frameHistory.clear();
    _textureNeedsUpdate = true;
    update();
}

void DeviceScreenGLWindow::clearDisplayViewport()
{
    _hasViewport = false;
    _frameHistory.clear();
    _textureNeedsUpdate = true;
    update();
}

QImage DeviceScreenGLWindow::grabFramebuffer()
{
    // Selection runs under the frame-source mutex: clearFrameSource() may
    // revoke both sources from the MessageCenter worker during instance
    // teardown, so the legacy fallback deep-copies under the lock to keep
    // every post-lock read on owned memory.
    QImage frame;
    {
        std::lock_guard<std::mutex> lock(_frameSourceMutex);
        // The frame on screen now; into an image of its own, the texture's frame stays as it is
        QImage grabbed(_latchedFrame.size(), _latchedFrame.format());
        if (_frameSource && !grabbed.isNull() &&
            _frameSource(SteadyNowNs(), grabbed.bits(), static_cast<size_t>(grabbed.sizeInBytes()), nullptr))
        {
            frame = grabbed;
        }
        // A frame source is installed: never fall back to _devicePixels. It wraps the emulator's framebuffer, which
        // is freed and reallocated when the guest switches video mode (Profi 352x288 <-> 512x240 hi-res, ATM, AlCo)
        // before the queued re-attach runs - a null or dangling image would be uploaded to the GPU.
        else if (!_frameSource && _devicePixels != nullptr)
        {
            frame = _devicePixels->copy();
        }
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
    return frame.copy(crop).convertToFormat(QImage::Format_ARGB32);
}

void DeviceScreenGLWindow::initializeGL()
{
    initializeOpenGLFunctions();
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);

    _texture = new QOpenGLTexture(QOpenGLTexture::Target2D);
    _texture->setMinificationFilter(QOpenGLTexture::Nearest);
    _texture->setMagnificationFilter(QOpenGLTexture::Nearest);
    _texture->setWrapMode(QOpenGLTexture::ClampToEdge);

    // The quad: two triangles as a strip, its corners top left, top right, bottom left, bottom right
    static const float kCorners[] = {0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f};
    _quadVao.create();
    _quadVao.bind();
    _quadBuffer.create();
    _quadBuffer.bind();
    _quadBuffer.allocate(kCorners, static_cast<int>(sizeof(kCorners)));
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, nullptr);
    _quadBuffer.release();
    _quadVao.release();

    _composeShader = new QOpenGLShaderProgram(this);
    if (!_composeShader->addShaderFromSourceCode(QOpenGLShader::Vertex, CRT_VERTEX_SHADER) ||
        !_composeShader->addShaderFromSourceCode(QOpenGLShader::Fragment, CRTRenderer::kCompositionShader) ||
        !_composeShader->link())
        qWarning() << "CRT composition shader failed:" << _composeShader->log();
    if (!_modelGpu.create())
        qWarning() << "CRT model shader failed:" << _modelGpu.log();

    // A custom shader or preset chosen before the context existed
    if (!_customShaderSource.isEmpty())
        compileCustomShader(_customShaderSource);
    if (!_slangPresetName.isEmpty() && !applySlangPreset(_slangPresetName))
        _slangPresetName.clear();
    if (!_gpuTimes.Init(QOpenGLContext::currentContext()))
        qInfo() << "DeviceScreenGLWindow: no GL timer queries, the GPU pass times stay 0";
}

void DeviceScreenGLWindow::setCRTEffectsEnabled(bool enabled)
{
    _crtEnabled = enabled;
    update();
}

void DeviceScreenGLWindow::setCRTProfile(CRTProfile profile)
{
    if (!customShader().isEmpty())
        loadCustomShader(QString());  // a built-in profile replaces the custom shader
    _crtParams = CRTProfileParams::FromProfile(profile);
    _crtEnabled = (profile != CRTProfile::None);
    update();
}

void DeviceScreenGLWindow::setCRTProfile(const CRTProfileParams& params)
{
    if (!customShader().isEmpty())
        loadCustomShader(QString());
    _crtParams = params;
    _crtEnabled = (params.profile != CRTProfile::None);
    update();
}

void DeviceScreenGLWindow::setCRTParams(const CRTProfileParams& params)
{
    _crtParams = params;
    update();
}

std::vector<SlangPresetRunner::Parameter> DeviceScreenGLWindow::presetParameters() const
{
    if (_slangPresetName.isEmpty())
        return {};
    if (_slangRunner)
        return _slangRunner->parameters();
    std::vector<SlangPresetRunner::Parameter> list =
        SlangPresetRunner::readParameters(SlangPresetRunner::presetDirectory(_slangPresetName));
    for (SlangPresetRunner::Parameter& parameter : list)
    {
        const auto kept = _slangOverrides.find(parameter.name);
        if (kept != _slangOverrides.end())
            parameter.value = std::clamp(kept->second, std::min(parameter.minimum, parameter.maximum),
                                         std::max(parameter.minimum, parameter.maximum));
    }
    return list;
}

bool DeviceScreenGLWindow::setPresetParameter(const QString& name, float value)
{
    for (const SlangPresetRunner::Parameter& parameter : presetParameters())
    {
        if (parameter.name != name)
            continue;
        const float clamped = std::clamp(value, std::min(parameter.minimum, parameter.maximum),
                                         std::max(parameter.minimum, parameter.maximum));
        _slangOverrides[name] = clamped;
        if (_slangRunner)
            _slangRunner->setParameter(name, clamped);
        update();
        return true;
    }
    return false;
}

void DeviceScreenGLWindow::resetPresetParameters()
{
    _slangOverrides.clear();
    if (_slangRunner)
        for (const SlangPresetRunner::Parameter& parameter : _slangRunner->parameters())
            _slangRunner->setParameter(parameter.name, parameter.initial);
    update();
}

void DeviceScreenGLWindow::setPresetOverrides(const std::map<QString, float>& overrides)
{
    _slangOverrides = overrides;
    if (_slangRunner)
        for (const auto& [name, value] : _slangOverrides)
            _slangRunner->setParameter(name, value);
    update();
}

bool DeviceScreenGLWindow::loadCustomShader(const QString& name)
{
    if (name != _slangPresetName)
        _slangOverrides.clear();  // the values set belong to the preset they were set on

    // A preset replaces a custom shader and the other way round; the empty name drops both
    const bool preset = SlangPresetRunner::isPreset(name);
    if (preset || name.isEmpty())
    {
        bool ok = true;
        if (context())
        {
            makeCurrent();
            ok = applySlangPreset(name);
            if (ok)
                compileCustomShader(QString());
            doneCurrent();
        }
        if (ok)
        {
            _slangPresetName = name;
            _customShaderName.clear();
            _customShaderSource.clear();
        }
        update();
        return ok;
    }

    QString source;
    if (!name.isEmpty())
    {
        source = CRTShaderManager::instance().loadShader(name);
        if (source.isEmpty())
        {
            CRTShaderManager::instance().scanShaderDirectory();  // a file added since the menu was built
            source = CRTShaderManager::instance().loadShader(name);
        }
        if (source.isEmpty())
            return false;
    }

    bool ok = true;
    if (context())
    {
        makeCurrent();
        ok = compileCustomShader(source);
        doneCurrent();
    }
    if (ok && context())
    {
        makeCurrent();
        applySlangPreset(QString());
        doneCurrent();
    }
    if (ok)
    {
        _customShaderName = name;
        _customShaderSource = source;
        _slangPresetName.clear();
    }
    update();
    return ok;
}

bool DeviceScreenGLWindow::applySlangPreset(const QString& name)
{
    if (_slangRunner)
        _slangRunner->release();
    _slangRunner.reset();
    if (name.isEmpty())
        return true;
    auto runner = std::make_unique<SlangPresetRunner>();
    if (!runner->load(SlangPresetRunner::presetDirectory(name)))
    {
        qWarning() << "CRT preset" << name << "failed:" << runner->error();
        return false;
    }
    for (const auto& [parameter, value] : _slangOverrides)
        runner->setParameter(parameter, value);
    _slangRunner = std::move(runner);
    return true;
}

bool DeviceScreenGLWindow::compileCustomShader(const QString& source)
{
    delete _customShader;
    _customShader = nullptr;
    if (source.isEmpty())
        return true;
    _customShader = new QOpenGLShaderProgram(this);
    if (_customShader->addShaderFromSourceCode(QOpenGLShader::Vertex, CRT_VERTEX_SHADER) &&
        _customShader->addShaderFromSourceCode(QOpenGLShader::Fragment, CRTRenderer::coreFragmentSource(source)) &&
        _customShader->link())
        return true;
    qWarning() << "Custom CRT shader failed:" << _customShader->log();
    delete _customShader;
    _customShader = nullptr;
    _customShaderName.clear();
    _customShaderSource.clear();
    return false;
}

std::vector<QString> DeviceScreenGLWindow::availableShaders()
{
    const QStringList presets = SlangPresetRunner::availablePresets();
    std::vector<QString> names(presets.begin(), presets.end());
    CRTShaderManager::instance().scanShaderDirectory();
    for (const QString& name : CRTShaderManager::instance().availableShaders())
    {
        if (!presets.contains(name))  // a preset's name chooses the preset
            names.push_back(name);
    }
    return names;
}

void DeviceScreenGLWindow::setTemporalBlendingEnabled(bool enabled)
{
    _temporalEnabled = enabled;
    _frameHistory.setBlendingEnabled(enabled);
    update();
}

void DeviceScreenGLWindow::setTemporalHistorySize(int frames)
{
    _frameHistory.setHistorySize(frames);
    setTemporalWeightMode(_temporalWeightMode);  // a new size starts with equal weights: the mode chosen stays
}

int DeviceScreenGLWindow::temporalHistorySize() const
{
    return _frameHistory.historySize();
}

void DeviceScreenGLWindow::setTemporalWeightMode(int mode)
{
    _temporalWeightMode = mode;
    if (mode == 0)
        _frameHistory.setEqualWeights();
    else
        _frameHistory.setExponentialWeights(_temporalDecay);
    update();
}

void DeviceScreenGLWindow::setTemporalDecay(float decay)
{
    _temporalDecay = std::clamp(decay, 0.1f, 0.9f);
    setTemporalWeightMode(_temporalWeightMode);
}

void DeviceScreenGLWindow::resizeGL(int w, int h)
{
    glViewport(0, 0, w, h);
}

QImage DeviceScreenGLWindow::renderWindowImage(bool hud, int pictureHeight)
{
    if (!isValid() || !makeCurrent())
        return {};
    const qreal ratio = devicePixelRatio();
    if (pictureHeight > 0)
        _renderPicture = QSize(static_cast<int>(pictureHeight * static_cast<double>(this->ratio)), pictureHeight);
    const QSize pixels = _renderPicture.isEmpty() ? (QSizeF(size()) * ratio).toSize() : _renderPicture;
    QImage image;
    {
        QOpenGLFramebufferObject fbo(pixels);
        fbo.bind();
        glViewport(0, 0, pixels.width(), pixels.height());
        // The HUD's QPainter would paint into the window, not this FBO: it goes onto the image below
        _renderingImage = true;
        updateTexture(refreshTargetNs());
        // The window paints once per emulated frame too, so the count it keeps would give every picture of a
        // recording the same parity: an interlace preset would never alternate its fields. The pictures count their own
        const uint32_t windowCount = _slangRunner ? _slangRunner->frameCount() : 0;
        if (_slangRunner)
            _slangRunner->setFrameCount(_imageFrameCount++);
        paintGL();
        if (_slangRunner)
            _slangRunner->setFrameCount(windowCount);
        _renderingImage = false;
        fbo.release();
        image = fbo.toImage();
    }
    _renderPicture = {};
    doneCurrent();
    image.setDevicePixelRatio(ratio);
    if (hud && _hudOverlay && _hudOverlay->isVisible())
    {
        QPainter painter(&image);
        _hudOverlay->renderHUD(painter, width(), height(), ratio);
    }
    return image;
}

void DeviceScreenGLWindow::updateTexture(int64_t hostNs, bool atRefresh)
{
    // The source is asked on every refresh (the frame due changes with the time), the legacy buffer only when a frame
    // came. Selection under the frame-source mutex (see clearFrameSource): the legacy fallback deep-copies so
    // post-lock texture upload only touches owned memory.
    const bool forced = _textureNeedsUpdate.exchange(false, std::memory_order_relaxed);
    QImage* sourceImage = nullptr;
    bool newFrame = forced;
    {
        std::lock_guard<std::mutex> lock(_frameSourceMutex);
        PresentFrameInfo info;
        if (_frameSource && !_latchedFrame.isNull() &&
            _frameSource(hostNs, _latchedFrame.bits(), static_cast<size_t>(_latchedFrame.sizeInBytes()), &info))
        {
            sourceImage = &_latchedFrame;
            newFrame = info.serial == 0 || info.serial != _uploadedSerial;
            // Nothing new and nothing pending: one more idle refresh
            if (newFrame || info.nextSerial != 0)
                _idleRefreshes = 0;
            else
                _idleRefreshes++;
            _shownInfo = info;
            // The machine's rate: a change (another machine, the first steady second) decides the display's
            // (the nominal period of a frame paced to real time; a turbo frame, timed by its latch, has none)
            const int64_t period = info.source == FrameClockSource::Latch ? 0 : info.periodNs;
            if (newFrame && info.serial != 0 && info.targetNs > 0 && _frameRate.Push(info.targetNs, period) &&
                (_displayRate == DisplayRate::Match || _smoothAuto))
                QMetaObject::invokeMethod(this, [this]() {
                    updateDisplayRate();
                    updateVideoSync();
                }, Qt::QueuedConnection);
            if (smoothActive())
            {
                // UNREAL_SMOOTH_TRACE: from a reset on, so the trace holds every frame smooth motion has
                if (_smoothTraced && atRefresh && !_renderingImage && _smoothTrace.StartsNow(_smooth.GetMode()))
                {
                    _smooth.Reset();
                    _smoothShown = {};
                    _smoothFed = 0;
                }
                feedSmooth();
            }
        }
        // A frame source is installed: never fall back to _devicePixels. It wraps the emulator's framebuffer, which
        // is freed and reallocated when the guest switches video mode (Profi 352x288 <-> 512x240 hi-res, ATM, AlCo)
        // before the queued re-attach runs - a null or dangling image would be uploaded to the GPU.
        else if (!_frameSource && _devicePixels != nullptr)
        {
            if (!forced && _texture->isCreated())
            {
                _idleRefreshes++;
                return;
            }
            _legacyFrame = _devicePixels->copy();
            sourceImage = &_legacyFrame;
        }
        else
        {
            _idleRefreshes++;
        }
    }

    pushSmooth();
    if (!sourceImage || sourceImage->isNull())
        return;

    // Smooth motion: the picture for the time, drawn again only when it is another one
    _smoothChanged = false;
    _smoothUsed = false;
    QImage* smoothImage = nullptr;
    if (sourceImage == &_latchedFrame && smoothActive() && _smooth.Width() == sourceImage->width() &&
        _smooth.Height() == sourceImage->height())
    {
        // The GPU passes in this context (current: updateTexture draws), made on the first use
        if (_smoothOnGpu && !_smoothGpu.IsCreated() && !_smoothGpuFailed && !_smoothGpu.Create())
        {
            _smoothGpuFailed = true;
            qWarning() << "DeviceScreenGLWindow: smooth motion on the CPU, the GPU passes failed:" << _smoothGpu.Log();
        }
        _smoothGpu.SetPassTimes(&_gpuTimes);
        _smooth.SetDevice(_smoothOnGpu && _smoothGpu.IsCreated() ? &_smoothGpu : nullptr);
        const int64_t planNs = atRefresh ? hostNs : _shownInfo.targetNs;
        const smoothmotion::SmoothMotion::Result plan = _smooth.Plan(planNs);
        bool kept = false;
        if (plan.kind != smoothmotion::SmoothMotion::Kind::None)
        {
            _smoothUsed = true;
            kept = plan == _smoothShown && !forced && _texture->isCreated();
            if (!kept)
            {
                if (_smoothFrame.size() != sourceImage->size())
                {
                    _smoothFrame = QImage(sourceImage->size(), QImage::Format_RGBA8888);
                    _smoothShown = {};  // a new image holds no picture yet
                }
                // The GPU's passes alone (the device's segments) and the refresh on the CPU's clock
                const bool draws = !(plan == _smoothShown);
                const bool timed = draws && _smooth.GetDevice() != nullptr;
                if (timed)
                    _gpuTimes.Open(GpuPassTimes::Smooth);
                const auto drawStart = std::chrono::steady_clock::now();
                const bool drawn = !draws || _smooth.Draw(plan, reinterpret_cast<uint32_t*>(_smoothFrame.bits()));
                if (draws)
                    _gpuTimes.Record(GpuPassTimes::SmoothCpu,
                                     std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() -
                                                                               drawStart)
                                         .count());
                if (timed)
                    _gpuTimes.Close(GpuPassTimes::Smooth);
                if (drawn)
                {
                    _smoothChanged = !(plan == _smoothShown);
                    _smoothShown = plan;
                    smoothImage = &_smoothFrame;
                }
            }
        }
        if (_smoothTrace.Active() && atRefresh && !_renderingImage)
        {
            const QImage& shown = kept || smoothImage ? _smoothFrame : *sourceImage;
            _smoothTrace.Refresh(planNs, plan, !kept && smoothImage && _smoothChanged,
                                 reinterpret_cast<const uint32_t*>(shown.constBits()), shown.width(), shown.height());
        }
        if (kept)
            return;
    }
    else if (!smoothActive() && _smoothGpu.HoldsBuffers())
    {
        _smoothGpu.ReleaseBuffers();  // smooth motion off: its textures back to 1 x 1
    }
    // The frame is in the texture already (a viewport change forces the upload, it does not repeat the frame)
    if (!smoothImage && !newFrame && !forced && _texture->isCreated())
        return;

    QImage* uploadImage = smoothImage ? smoothImage : sourceImage;

    if (_temporalEnabled)
    {
        int srcWidth = sourceImage->width();
        int srcHeight = sourceImage->height();

        if (_frameHistory.width() != srcWidth || _frameHistory.height() != srcHeight)
        {
            _frameHistory.init(srcWidth, srcHeight, _frameHistory.historySize());
            _blendedFrame = QImage(srcWidth, srcHeight, QImage::Format_RGBA8888);
        }

        if (newFrame)
            _frameHistory.pushFrame(sourceImage->bits(), sourceImage->sizeInBytes());

        if (_frameHistory.hasMinimumHistory())
        {
            _frameHistory.getBlendedFrame(_blendedFrame.bits(), _blendedFrame.sizeInBytes());
            uploadImage = &_blendedFrame;
        }
    }

    const int w = uploadImage->width();
    const int h = uploadImage->height();
    if (!_texture->isCreated() || _texture->width() != w || _texture->height() != h)
    {
        if (_texture->isCreated())
            _texture->destroy();
        _texture->create();
        _texture->setSize(w, h);
        _texture->setFormat(QOpenGLTexture::RGBA8_UNorm);
        _texture->setMipLevels(1);
        _texture->allocateStorage(QOpenGLTexture::RGBA, QOpenGLTexture::UInt8);
        _texture->setMinificationFilter(QOpenGLTexture::Nearest);
        _texture->setMagnificationFilter(QOpenGLTexture::Nearest);
        _texture->setWrapMode(QOpenGLTexture::ClampToEdge);
    }

    QOpenGLPixelTransferOptions transfer;
    transfer.setAlignment(1);
    transfer.setRowLength(static_cast<int>(uploadImage->bytesPerLine() / 4));
    _texture->setData(0, QOpenGLTexture::RGBA, QOpenGLTexture::UInt8, uploadImage->constBits(), &transfer);

    _shownFrame = uploadImage;
    ++_frameSerial;
    _uploadedSerial = sourceImage == &_latchedFrame ? _shownInfo.serial : 0;
}

namespace
{
/// A tight RGBA8 image into a nearest-sampled texture, (re)allocated for its size
void UploadRgba(QOpenGLTexture*& texture, int width, int height, const uint8_t* pixels)
{
    if (!texture)
        texture = new QOpenGLTexture(QOpenGLTexture::Target2D);
    if (!texture->isCreated() || texture->width() != width || texture->height() != height)
    {
        if (texture->isCreated())
            texture->destroy();
        texture->create();
        texture->setSize(width, height);
        texture->setFormat(QOpenGLTexture::RGBA8_UNorm);
        texture->setMipLevels(1);
        texture->allocateStorage(QOpenGLTexture::RGBA, QOpenGLTexture::UInt8);
        texture->setMinificationFilter(QOpenGLTexture::Nearest);
        texture->setMagnificationFilter(QOpenGLTexture::Nearest);
        texture->setWrapMode(QOpenGLTexture::ClampToEdge);
    }
    QOpenGLPixelTransferOptions transfer;
    transfer.setAlignment(1);
    texture->setData(0, QOpenGLTexture::RGBA, QOpenGLTexture::UInt8, pixels, &transfer);
}

}  // namespace

#ifdef UNREAL_TEST_HOOKS
void DeviceScreenGLWindow::hangGL()
{
    // A vertex shader that never ends (vertices run whatever is drawn): the system resets the GPU after its timeout
    // (TDR), the context is lost
    qWarning() << "DeviceScreenGLWindow: UNREAL_TEST_GPU_HANG: the window's GL work hangs now";
    QOpenGLShaderProgram program;
    if (!program.addShaderFromSourceCode(QOpenGLShader::Vertex, R"(#version 330 core
layout(location = 0) in vec2 corner;
uniform float stepBy;
uniform float limit;
void main() {
    // stepBy is 0: never ends (a loop the compiler cannot prove endless is kept)
    float x = 0.0;
    while (x < limit) x += stepBy;
    gl_Position = vec4(corner * 2.0 - 1.0, x * 1e-30, 1.0);
}
)") || !program.addShaderFromSourceCode(QOpenGLShader::Fragment, R"(#version 330 core
out vec4 color;
void main() { color = vec4(1.0); }
)") || !program.link())
    {
        qWarning() << "DeviceScreenGLWindow: the hang shader failed:" << program.log();
        return;
    }
    program.bind();
    program.setUniformValue("stepBy", 0.0f);
    program.setUniformValue("limit", 1.0f);
    _quadVao.bind();
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    _quadVao.release();
    program.release();
    glFlush();
}
#else
void DeviceScreenGLWindow::hangGL()
{
}
#endif

void DeviceScreenGLWindow::paintGL()
{
    if (_glHangAtNs != 0 && SteadyNowNs() >= _glHangAtNs)
    {
        _glHangAtNs = 0;
        hangGL();
    }
    const qreal dpr = devicePixelRatio();
    const int surfaceWidth = _renderPicture.isEmpty() ? qRound(width() * dpr) : _renderPicture.width();
    const int surfaceHeight = _renderPicture.isEmpty() ? qRound(height() * dpr) : _renderPicture.height();
    glViewport(0, 0, surfaceWidth, surfaceHeight);
    glDisable(GL_DITHER);  // the composition writes exact values
    // The HUD's QPainter of the previous frame may leave its state behind
    glDisable(GL_BLEND);
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_STENCIL_TEST);
    glDisable(GL_DEPTH_TEST);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);  // a preset's framebuffers clear to other colours
    glClear(GL_COLOR_BUFFER_BIT);

    _gpuTimes.Collect();
    const QRect picture = pictureDeviceRect();
    if (_texture && _texture->isCreated() && !picture.isEmpty())
    {
        _gpuTimes.Begin(GpuPassTimes::Picture);
        // The viewport is the picture, to the device pixel: the quad covers it, the CRT composition reads its tables
        // per pixel. GL counts rows from the bottom
        const int glY = surfaceHeight - picture.y() - picture.height();
        glViewport(picture.x(), glY, picture.width(), picture.height());

        // With or without CRT effects the composition draws (CRTProfile::None: the plain nearest scale), the software
        // screen's picture bit for bit
        if (_crtEnabled && _slangRunner)
            drawSlangPreset(picture, glY);
        else if (_crtEnabled && _customShader && _customShader->isLinked())
            drawCustomCrt(picture);
        else if (_crtEnabled && CRTBeamModel::isModel(_crtParams.profile) && _modelGpu.isCreated() && _shownFrame)
            drawModelCrt(picture);
        else if (_composeShader && _composeShader->isLinked() && _shownFrame)
            drawComposedCrt(picture);
        glViewport(0, 0, surfaceWidth, surfaceHeight);
        _gpuTimes.End(GpuPassTimes::Picture);
    }

    if (_hudOverlay && _hudOverlay->isVisible() && !_renderingImage)
    {
        // Into the bound framebuffer: the window's surface or the native way's IOSurface
        QOpenGLPaintDevice device(QSize(surfaceWidth, surfaceHeight));
        device.setDevicePixelRatio(dpr);
        QPainter painter(&device);
        _hudOverlay->renderHUD(painter, width(), height(), dpr);
    }
}

void DeviceScreenGLWindow::drawQuad(QOpenGLShaderProgram* program, float srcX1, float srcY1, float srcX2, float srcY2)
{
    program->setUniformValue("texRect", QVector4D(srcX1, srcY1, srcX2, srcY2));
    _quadVao.bind();
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    _quadVao.release();
}

void DeviceScreenGLWindow::drawComposedCrt(const QRect& picture)
{
    CRTRenderer::Layout layout;
    layout.outWidth = picture.width();
    layout.outHeight = picture.height();
    layout.frameWidth = _shownFrame->width();
    layout.frameHeight = _shownFrame->height();
    layout.source = displaySourceRect().toRect().intersected(_shownFrame->rect());
    if (layout.source.isEmpty())
        return;

    // The tables again only for a new size or profile, the grading once per frame
    static const CRTProfileParams kPlain = CRTProfileParams::None();
    const CRTProfileParams& params = _crtEnabled ? _crtParams : kPlain;
    const bool rebuilt = _crtRenderer.setLayout(layout, params);
    if (rebuilt || !_indexTexture || !_factorTexture)
    {
        UploadRgba(_indexTexture, layout.outWidth, layout.outHeight, _crtRenderer.indexTable().data());
        UploadRgba(_factorTexture, layout.outWidth, layout.outHeight, _crtRenderer.factorTable().data());
    }
    if (rebuilt || !_gradedTexture || _gradedSerial != _frameSerial)
    {
        _crtRenderer.grade(_shownFrame->constBits(), static_cast<int>(_shownFrame->bytesPerLine()), params);
        UploadRgba(_gradedTexture, layout.frameWidth, layout.frameHeight, _crtRenderer.graded().data());
        _gradedSerial = _frameSerial;
    }

    _composeShader->bind();
    _gradedTexture->bind(0);
    _indexTexture->bind(1);
    _factorTexture->bind(2);
    _composeShader->setUniformValue("frame", 0);
    _composeShader->setUniformValue("indexTable", 1);
    _composeShader->setUniformValue("factorTable", 2);
    _composeShader->setUniformValue("frameSize", QVector2D(static_cast<float>(layout.frameWidth), static_cast<float>(layout.frameHeight)));
    drawQuad(_composeShader, 0.0f, 0.0f, 1.0f, 1.0f);
    _factorTexture->release(2);
    _indexTexture->release(1);
    _gradedTexture->release(0);
    glActiveTexture(GL_TEXTURE0);
    _composeShader->release();
}

void DeviceScreenGLWindow::drawModelCrt(const QRect& picture)
{
    CRTBeamModel::Layout layout;
    layout.outWidth = picture.width();
    layout.outHeight = picture.height();
    layout.frameWidth = _shownFrame->width();
    layout.frameHeight = _shownFrame->height();
    layout.source = displaySourceRect().toRect().intersected(_shownFrame->rect());
    if (layout.source.isEmpty())
        return;

    // The tables again only for a new size or profile, the frame once per upload
    const bool rebuilt = _crtModel.setLayout(layout, _crtParams.profile);
    if (rebuilt || !_modelTablesUploaded)
    {
        _modelGpu.uploadTables(_crtModel);
        _modelTablesUploaded = true;
    }
    if (rebuilt || _modelSerial != _frameSerial)
    {
        // Not the glow's columns: the shader sums them from the frame itself
        _crtModel.prepare(_shownFrame->constBits(), static_cast<int>(_shownFrame->bytesPerLine()), false);
        _modelGpu.uploadFrame(_crtModel);
        _modelSerial = _frameSerial;
    }
    _modelGpu.draw(_crtModel);
    glActiveTexture(GL_TEXTURE0);
}

void DeviceScreenGLWindow::drawCustomCrt(const QRect& picture)
{
    const QRectF source = displaySourceRect();
    const float texW = static_cast<float>(_texture->width());
    const float texH = static_cast<float>(_texture->height());

    _customShader->bind();
    _texture->bind(0);
    _customShader->setUniformValue("tex", 0);
    _customShader->setUniformValue("texSize", QVector2D(texW, texH));
    _customShader->setUniformValue("outputSize", QVector2D(static_cast<float>(picture.width()), static_cast<float>(picture.height())));
    // The profiles have no phosphor mask: the mask uniforms of the documented interface stay, as "none"
    _customShader->setUniformValue("maskType", 0);
    _customShader->setUniformValue("curvature", _crtParams.curvature);
    _customShader->setUniformValue("cornerRadius", _crtParams.cornerRadius);
    _customShader->setUniformValue("scanlineWeight", _crtParams.scanlineWeight);
    _customShader->setUniformValue("maskStrength", 0.0f);
    _customShader->setUniformValue("maskDotPitch", 0.0f);
    _customShader->setUniformValue("bloomStrength", _crtParams.bloomStrength);
    _customShader->setUniformValue("saturation", _crtParams.saturation);
    _customShader->setUniformValue("brightness", _crtParams.brightness);
    _customShader->setUniformValue("contrast", _crtParams.contrast);
    _customShader->setUniformValue("gamma", _crtParams.gamma);
    drawQuad(_customShader, static_cast<float>(source.left()) / texW, static_cast<float>(source.top()) / texH,
             static_cast<float>(source.right()) / texW, static_cast<float>(source.bottom()) / texH);
    _texture->release(0);
    _customShader->release();
}

void DeviceScreenGLWindow::drawSlangPreset(const QRect& picture, int glY)
{
    // The passes render into their own framebuffers; the last one is copied 1:1 into the picture of the target (the
    // window or renderWindowImage's framebuffer object)
    GLint target = 0;
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &target);
    const QRect source = displaySourceRect().toRect().intersected(QRect(0, 0, _texture->width(), _texture->height()));
    if (source.isEmpty())
        return;
    _slangRunner->render(_texture->textureId(), source, picture.size());
    _slangRunner->drawTo(static_cast<GLuint>(target), QRect(picture.x(), glY, picture.width(), picture.height()));
}

void DeviceScreenGLWindow::keyPressEvent(QKeyEvent* event)
{
    // The full screen key (Cmd+F / Alt+Enter) bubbles up to the application shortcut
    if (KeyboardManager::isFullScreenKey(event))
    {
        event->ignore();
        return;
    }

    event->accept();

    // The mouse release key never reaches the machine
    if (_mouseCapture && _mouseCapture->handleKeyPress(event))
        return;

    // An automation session holds the machine: no keys from the GUI (releases still go: nothing stays held)
    if (RemoteControlGuard::InputBlocked(_emulator.get()))
        return;

    if (!event->isAutoRepeat())
    {
        // Two messages: the ZX key for the matrix, the physical key for a PS/2 machine
        const std::string targetId = _emulator ? _emulator->GetUUID().toString() : std::string();
        KeyboardManager::postHostKey(event, KEY_PRESSED, targetId);
    }
}

void DeviceScreenGLWindow::keyReleaseEvent(QKeyEvent* event)
{
    if (KeyboardManager::isFullScreenKey(event))
    {
        event->ignore();
        return;
    }

    event->accept();

    if (_mouseCapture && _mouseCapture->handleKeyRelease(event))
        return;

    if (!event->isAutoRepeat())
    {
        // Two messages: the ZX key for the matrix, the physical key for a PS/2 machine
        const std::string targetId = _emulator ? _emulator->GetUUID().toString() : std::string();
        KeyboardManager::postHostKey(event, KEY_RELEASED, targetId);
    }
}

void DeviceScreenGLWindow::mousePressEvent(QMouseEvent* event)
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
        QWindow::mousePressEvent(event);
}

void DeviceScreenGLWindow::mouseReleaseEvent(QMouseEvent* event)
{
    if (_mouseCapture && _mouseCapture->handleMouseRelease(event))
        event->accept();
    else
        QWindow::mouseReleaseEvent(event);
}

void DeviceScreenGLWindow::mouseMoveEvent(QMouseEvent* event)
{
    if (_mouseCapture && _mouseCapture->handleMouseMove(event))
        event->accept();
    else
        QWindow::mouseMoveEvent(event);
}

void DeviceScreenGLWindow::wheelEvent(QWheelEvent* event)
{
    if (_mouseCapture && _mouseCapture->handleWheel(event))
        event->accept();
    else
        QWindow::wheelEvent(event);
}

void DeviceScreenGLWindow::focusOutEvent(QFocusEvent* event)
{
    if (_mouseCapture)
        _mouseCapture->handleFocusOut();
    // Keys held while the focus left never send their release here: let a PS/2 machine see them go up
    KeyboardManager::postHeldKeyReleases(_emulator ? _emulator->GetUUID().toString() : std::string());
    QWindow::focusOutEvent(event);
}

QRectF DeviceScreenGLWindow::displaySourceRect() const
{
    if (!_hasViewport)
        return _devicePixelsRect;
    return QRectF(_displayViewport.cropLeft, _displayViewport.cropTop,
                  _devicePixelsRect.width() - _displayViewport.cropLeft - _displayViewport.cropRight,
                  _devicePixelsRect.height() - _displayViewport.cropTop - _displayViewport.cropBottom);
}

QSize DeviceScreenGLWindow::drawnPictureSize() const
{
    // The same fit paintGL draws: the native ratio, pillarboxed or letterboxed
    const double windowW = width();
    const double windowH = height();
    if (windowW <= 0 || windowH <= 0)
        return QSize();
    if (windowW / windowH > ratio)
        return QSize(static_cast<int>(windowH * ratio), static_cast<int>(windowH));
    return QSize(static_cast<int>(windowW), static_cast<int>(windowW / ratio));
}

QRect DeviceScreenGLWindow::pictureDeviceRect() const
{
    if (!_renderPicture.isEmpty())
        return QRect(QPoint(0, 0), _renderPicture);
    const QSize drawn = drawnPictureSize();
    if (drawn.isEmpty())
        return QRect();
    const qreal dpr = devicePixelRatio();
    const int x = (width() - drawn.width()) / 2;
    const int y = (height() - drawn.height()) / 2;
    return QRect(qRound(x * dpr), qRound(y * dpr), qRound(drawn.width() * dpr), qRound(drawn.height() * dpr));
}

bool DeviceScreenGLWindow::event(QEvent* event)
{
    if (event->type() == QEvent::ShortcutOverride)
    {
        auto* keyEvent = static_cast<QKeyEvent*>(event);
        if (KeyboardManager::isFullScreenKey(keyEvent))
        {
            event->ignore();
            return false;
        }
        // The machine's keys (KeyboardManager::machineOwnsKey): no shortcut, the key press comes here.
        // Leased: automation may be removing the instance on another thread
        const Emulator::ContextLease lease = _emulator ? _emulator->LeaseContext() : Emulator::ContextLease{};
        if (KeyboardManager::machineOwnsKey(keyEvent, lease ? lease->pKeyboard : nullptr))
        {
            event->accept();
            return true;
        }
    }

    switch (event->type())
    {
        case QEvent::DragEnter:
        {
            QDragEnterEvent* dragEvent = static_cast<QDragEnterEvent*>(event);
            if (dragEvent->mimeData()->hasUrls())
            {
                dragEvent->acceptProposedAction();
                const QList<QUrl> urls = dragEvent->mimeData()->urls();
                emit dragEntered(urls.isEmpty() ? QString() : urls.first().toLocalFile());
                return true;
            }
            break;
        }
        case QEvent::DragLeave:
            emit dragLeft();
            return true;

        case QEvent::Drop:
        {
            QDropEvent* dropEvent = static_cast<QDropEvent*>(event);
            const QMimeData* mimeData = dropEvent->mimeData();
            if (mimeData->hasUrls())
            {
                QStringList paths;
                for (const QUrl& url : mimeData->urls())
                {
                    if (url.isLocalFile())
                        paths << url.toLocalFile();
                }
                if (!paths.isEmpty())
                {
                    qDebug() << "DeviceScreenGLWindow: files dropped:" << paths;
                    emit filesDropped(paths);
                }
                emit dragLeft();
                return true;
            }
            break;
        }
        case QEvent::UpdateRequest:
            // Native way: the HUD's requests go with the next timed present, never ahead of a frame's own time
            if (_native)
                update();
            else
                renderGL();
            return true;
        case QEvent::Expose:
            if (isExposed())
            {
                if (_smoothAuto && !_syncTimer->isActive() && !_isShuttingDown)
                    _syncTimer->start();
                if (_native)
                    renderNative(true);
                else
                    renderGL();
            }
            break;
        case QEvent::Resize:
            if (_native && isExposed())
                renderNative(true);
            break;
        default:
            break;
    }
    return QWindow::event(event);
}

void DeviceScreenGLWindow::handleExternalKeyPress(QKeyEvent* event)
{
    keyPressEvent(event);
}

void DeviceScreenGLWindow::handleExternalKeyRelease(QKeyEvent* event)
{
    keyReleaseEvent(event);
}
