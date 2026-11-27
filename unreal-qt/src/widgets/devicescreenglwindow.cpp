#include "devicescreenglwindow.h"
#include "media/droppaths.h"
#include "emulator/remotecontrolguard.h"
#include "emulator/mousecapturecontroller.h"
#include "hud/qt/hudoverlay.h"

#include <QDebug>
#include <algorithm>
#include <utility>
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
#include <QScreen>
#include <QSurfaceFormat>
#include <QTimer>
#include <QUrl>
#include <QVector2D>
#include <QVector4D>

#include <QOpenGLExtraFunctions>
#include <QOpenGLShaderProgram>

#include "gpu/gldevice.h"
#include "gpu/glwindowbuffer.h"
#ifdef UNREAL_VULKAN
#include "gpu/vulkandevice.h"
#include "gpu/vulkanpresenter.h"
#include "gpu/vulkansurface.h"
#endif
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

/// The renderer asked for (View > Renderer, the automation's renderer; UNREAL_RENDERER at start): "gl" / "vulkan".
/// The GUI thread
std::string& WantedRenderer()
{
    static std::string wanted =
        qEnvironmentVariable("UNREAL_RENDERER").compare(QStringLiteral("vulkan"), Qt::CaseInsensitive) == 0 ? "vulkan"
                                                                                                           : "gl";
    return wanted;
}

// The Vulkan probe (the GUI thread): -1 none yet, 0 no hardware Vulkan 1.3 GPU, 1 one; its name
int gVulkanProbe = -1;
QString gVulkanName;

/// A hardware Vulkan 1.3 GPU (llvmpipe, SwiftShader only with UNREAL_GPU_ALLOW_SOFTWARE=1), asked once until
/// reprobeGPUAcceleration: a device made takes ~50 ms
bool VulkanAvailable()
{
#ifdef UNREAL_VULKAN
    if (gVulkanProbe < 0)
    {
        gVulkanProbe = 0;
        gVulkanName.clear();
        std::string error;
        auto device = gpu::VulkanDevice::Create(gpu::VulkanDevice::Setup{}, error);
        if (device)
        {
            const std::string name = device->Describe();
            gVulkanName = QString::fromStdString(name);
            qInfo() << "DeviceScreenGLWindow: Vulkan:" << gVulkanName;
            const bool software =
                name.find("llvmpipe") != std::string::npos || name.find("SwiftShader") != std::string::npos;
            if (!software || qEnvironmentVariableIntValue("UNREAL_GPU_ALLOW_SOFTWARE") == 1)
                gVulkanProbe = 1;
            else
                qInfo() << "DeviceScreenGLWindow: the Vulkan GPU is a software one: no Vulkan renderer";
        }
        else
        {
            qInfo() << "DeviceScreenGLWindow: no Vulkan renderer:" << QString::fromStdString(error);
        }
    }
    return gVulkanProbe == 1;
#else
    return false;
#endif
}

/// The Vulkan renderer (docs/inprogress/2026-11-19-vulkan-renderer): asked for, and a GPU for it
bool UseVulkanRenderer()
{
    return WantedRenderer() == "vulkan" && VulkanAvailable();
}

/// Vulkan: the HUD image over the picture, its rows turned over into GL's
const char* const kHudFragment = R"(
#version 330
uniform sampler2D hud;
uniform int height;
out vec4 color;
void main() {
    color = texelFetch(hud, ivec2(int(gl_FragCoord.x), height - 1 - int(gl_FragCoord.y)), 0);
}
)";

/// The window recording's picture copied as it is: into the encoder's frame (macOS: composeRecordingFrame), into its
/// blocks before a smooth scale (packRecordingNv12 too)
const char* const kRecordCopyFragment = R"(
#version 330
uniform sampler2D source;
in vec2 texCoord;
out vec4 color;
void main() {
    color = texture(source, texCoord);
}
)";

/// The window recording's picture as NV12 bytes (packRecordingNv12): the target's texel (col, row) holds the Y of
/// pixels 4 col .. 4 col + 3 of row `row` (row < height), else U,V of chroma columns 2 col and 2 col + 1 of chroma
/// row `row - height`. FrameScaler::PackedToNv12's integer arithmetic (BT.709, limited range, the chroma of a 2x2
/// block's average): a picture sampled texel for texel gives its bytes
const char* const kRecordNv12Fragment = R"(
#version 330
uniform sampler2D source;
uniform vec2 origin;      // the texture coordinate of the place's top left corner
uniform vec2 stepPx;      // per pixel of the place
uniform ivec4 inside;     // the picture in the place: x0, y0, x1, y1 (pixels, rows from the top)
uniform ivec3 background; // 0..255
uniform int height;       // the place's rows
out vec4 color;
ivec3 Pixel(int x, int y) {
    if (x < inside.x || y < inside.y || x >= inside.z || y >= inside.w)
        return background;
    return ivec3(texture(source, origin + (vec2(x, y) + 0.5) * stepPx).rgb * 255.0 + 0.5);
}
float Luma(ivec3 p) {
    return float(clamp(((47 * p.r + 157 * p.g + 16 * p.b + 128) >> 8) + 16, 0, 255)) / 255.0;
}
vec2 Chroma(int cx, int cy) {
    ivec3 s = (Pixel(cx, cy) + Pixel(cx + 1, cy) + Pixel(cx, cy + 1) + Pixel(cx + 1, cy + 1)) >> 2;
    int u = ((112 * s.b - 26 * s.r - 86 * s.g + 128) >> 8) + 128;
    int v = ((112 * s.r - 102 * s.g - 10 * s.b + 128) >> 8) + 128;
    return vec2(float(clamp(u, 0, 255)), float(clamp(v, 0, 255))) / 255.0;
}
void main() {
    int col = int(gl_FragCoord.x);
    int row = int(gl_FragCoord.y);
    if (row < height) {
        int x = col * 4;
        color = vec4(Luma(Pixel(x, row)), Luma(Pixel(x + 1, row)), Luma(Pixel(x + 2, row)), Luma(Pixel(x + 3, row)));
    } else {
        int cy = (row - height) * 2;
        color = vec4(Chroma(col * 4, cy), Chroma(col * 4 + 2, cy));
    }
}
)";

// The last probe (the GUI thread): -1 none yet, 0 no GPU, 1 a hardware one; the renderer it saw
std::atomic<int> gGpuProbe{-1};
QString gGpuRenderer;
}  // namespace

DeviceScreenGLWindow::DeviceScreenGLWindow(QWindow* parent)
    : QWindow(parent), _vulkan(UseVulkanRenderer()), _native(_vulkan || UseNativePresenter())
{
#ifdef __APPLE__
    setSurfaceType(_native ? QSurface::MetalSurface : QSurface::OpenGLSurface);
#else
    // Windows: the swap chain takes the HWND as it is; a GL surface keeps the fallback to GL open (no interop)
    setSurfaceType(QSurface::OpenGLSurface);
#endif
    setupVSync();
    if (_native)
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
    else
    {
        _glTimer = new QTimer(this);
        _glTimer->setSingleShot(true);
        _glTimer->setTimerType(Qt::PreciseTimer);
        connect(_glTimer, &QTimer::timeout, this, [this]() { renderGL(); });
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
        watchScreenRate();
        updateDisplayRate();
        updateVideoSync();
    });
    watchScreenRate();
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
    dropPresenter();
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
    for (CrtTables& tables : _crtTables)
        tables.Invalidate();
    return true;
}

void DeviceScreenGLWindow::releaseGLObjects()
{
    _texture.reset();
    for (CrtTables& tables : _crtTables)
        tables.Release();
    _composeShader.reset();
    _smooth.SetDevice(nullptr);
    _smoothGpu.Destroy();
    _smoothGpuFailed = false;  // another context or device: the GPU passes are tried again
    _customShader.reset();
    if (_slangRunner)
        _slangRunner->release();
    _slangRunner.reset();
    _gpuTimes.Release(false);
    _hudTexture.reset();
    _hudProgram.reset();
    _recordCopyProgram.reset();
    _recordNv12Program.reset();
    _frameTarget = nullptr;
    dropPresenter();  // its GL objects in this context; Vulkan: its swapchain, before the device and the surface
    _gpu.reset();
#ifdef UNREAL_VULKAN
    if (_vkSurface)
        vkDestroySurfaceKHR(_vkInstance, _vkSurface, nullptr);
    _vkSurface = VK_NULL_HANDLE;
    if (_vkInstance)
        vkDestroyInstance(_vkInstance, nullptr);
    _vkInstance = VK_NULL_HANDLE;
#endif
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
    _glPaintAsked = true;  // the HUD, an overlay, a setting: drawn even with no new frame
    requestUpdate();
}

void DeviceScreenGLWindow::renderGL()
{
    if (_isShuttingDown || !isExposed() || !prepareContext())
        return;
    if (_glTimer)
        _glTimer->stop();
    const QSize pixels = (QSizeF(size()) * devicePixelRatio()).toSize();
    glBindFramebuffer(GL_FRAMEBUFFER, _context->defaultFramebufferObject());
    gpu::RestoreWindowDrawBuffer(_context.get());  // NVIDIA EGL: GL_NONE, every draw dropped
    bool asked = std::exchange(_glPaintAsked, false) || !_glDrawn;
    if (pixels != _glSize)
    {
        _glSize = pixels;
        resizeGL(pixels.width(), pixels.height());
        asked = true;
    }
    const uint64_t uploads = _frameSerial;
    const uint64_t shownSerial = _uploadedSerial;
    updateTexture(refreshTargetNs());
    _glNextTargetNs = _shownInfo.nextTargetNs;
    // The window repaints on every refresh, paced by the swap that waits for the display. Where the swaps do not wait
    // (headless mutter under NVIDIA EGL, a remote desktop: 2026-11-25, ~250 repaints a second for 49 Hz frames, each a
    // buffer for the compositor and the encoder) it draws and swaps only for something new: another frame (the same
    // one uploaded again after a latch is not), smooth motion's next picture, a repaint asked; the next look is timed
    const bool newFrame = _smoothUsed ? _smoothChanged
                                      : _frameSerial != uploads && (_uploadedSerial == 0 || _uploadedSerial != shownSerial);
    if (_glSwapUnpaced && !newFrame && !asked)
    {
        scheduleGl();
        return;
    }
    paintGL();
    _context->swapBuffers(this);
    // Swaps that wait for the display come at most at its rate: half again as many in a second is a swap that waits
    // for nothing (what the window then does stays for its life)
    const int64_t now = SteadyNowNs();
    if (now - _glSwapSecondNs >= 1'000'000'000)
    {
        const QScreen* display = screen();
        const double hz = display ? display->refreshRate() : 60.0;
        if (!_glSwapUnpaced && _glSwapSecondNs != 0 && now - _glSwapSecondNs < 2'000'000'000 &&
            _glSwapsInSecond > std::max(90.0, hz * 1.5))
        {
            _glSwapUnpaced = true;
            qInfo().noquote() << "DeviceScreenGLWindow: GL swaps do not wait for the display (" << _glSwapsInSecond
                              << "a second at" << hz << "Hz): the window repaints only what is new";
        }
        _glSwapSecondNs = now;
        _glSwapsInSecond = 0;
    }
    _glSwapsInSecond++;
    _glDrawn = true;
    captureForRecording();  // after the swap: the frame shown, the GUI free until the next one
    onFrameSwapped();
}

void DeviceScreenGLWindow::scheduleGl()
{
    // While frames come: at the next frame's time less the refresh the look asks ahead for, else a refresh on; a latch
    // asks at once anyway (refresh)
    if (!_glTimer || _isShuttingDown || !isExposed() || _idleRefreshes > idleLimit())
        return;
    const int64_t period = refreshPeriodNs();
    int64_t waitNs = period;
    if (_glNextTargetNs > 0)
        waitNs = std::clamp<int64_t>(_glNextTargetNs - period - SteadyNowNs(), 1'000'000, period);
    _glTimer->start(static_cast<int>((waitNs + 999'999) / 1'000'000));
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
    if (_vulkan && _gpu && _gpu->Lost())
    {
        dropVulkan("lost its device (a GPU reset)");
        return;
    }
    if (!_presenter)
    {
#ifdef UNREAL_VULKAN
        if (_vulkan && _gpu && _gpu->GetBackend() == gpu::Device::Backend::Vulkan)
        {
            std::string error;
            _presenter = gpu::VulkanPresenter::Create(this, static_cast<gpu::VulkanDevice&>(*_gpu), _vkSurface, error);
            if (!_presenter)
            {
                dropVulkan("its presenter was not made: " + error);
                return;
            }
        }
        else
#endif
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
        if (!_vulkan && !linuxpresent::UseTimedSwap(_presentPolicy, NativePresenter::PresentFeedback() == 1))
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
        settleSmoothGuard();
    }

    // Occluded or minimized: the system would drop every present. Nothing is copied, interpolated or drawn (the GPU
    // rests); a slow poll notices the window shown again, a forced repaint waits for it. Latches keep it polling, a
    // paused machine stops it as an idle one (the window's Expose starts it again)
    if (!_presenter->IsVisible())
    {
        _presentPending = _presentPending || force;
        _presentHidden = true;
        _recovery.Hidden();
        settleSmoothGuard();
        _smoothRefreshNs = 0;
        _idleRefreshes++;
        scheduleNative();
        return;
    }
    if (_presentHidden)
        settleSmoothGuard();  // shown again: the lates of the first moments are the window's, not the pictures'
    _presentHidden = false;

    // The presenter paces (b-vsync on a display link): its tick gives the refresh to draw for, the timer only forced
    // repaints. A wake of the timer left over from before draws nothing. Ticks that stop (the system stops a display
    // link whose view it does not show: an occluded window) leave the timer's way in charge until they come again
    // Low-latency video: a latch's wake draws its frame now, not at the next tick (half a refresh later on average)
    const bool paced = linkPacing();
    const int64_t linkRefreshNs = _linkRefreshNs;
    _linkRefreshNs = 0;
    const bool latchWake = _latchWake;
    _latchWake = false;
    if (paced && linkRefreshNs == 0 && !force && !latchWake)
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
        _frameTarget = _vulkan ? _presenter->BeginTexture(pixels.width(), pixels.height()) : nullptr;
        if (!failHook && (_vulkan ? _frameTarget != nullptr : _presenter->BeginFrame(pixels.width(), pixels.height()) != 0))
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
            // Low-latency video: the newest frame at once, at the first refresh it can make (a refresh asked for a
            // lead ahead would hold it that long)
            if (newFrame && _shownInfo.atOnce)
                at = 0;
            if (newFrame && _smoothRefreshNs != 0)
            {
                at = _smoothRefreshNs - _smoothPeriodNs / 2;
                trace.picture = ++_smoothPictures;
                trace.targetNs = _smoothRefreshNs;
            }
            _presenter->PresentFrame(at, trace);
            _frameTarget = nullptr;
            if (newFrame && !_presenter->PresentFailed())
                captureForRecording();  // after the present: the frame shown, the GUI free until the next one
            // Drawn and not handed on (Vulkan: no image to present into): the same way as no framebuffer
            if (_presenter->PresentFailed())
            {
                if (pictureNotPresented(now, false))
                    return;
            }
            else if (const int64_t failedNs = _recovery.Presented(now); failedNs > 0)
                qInfo().noquote() << "DeviceScreenGLWindow: presenting again after" << failedNs / 1'000'000 << "ms,"
                                  << _recovery.Failures() << "pictures not presented";
        }
        else if (pixels.isEmpty())
        {
            // A window of no size has nothing to present
        }
        else
        {
            if (pictureNotPresented(now, failHook))
                return;
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
    dropPresenter();
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

/// A picture was not presented (no framebuffer, or the present failed): told once, waited for, then a new presenter,
/// then GL. @return the presenter is gone (renderNative ends)
bool DeviceScreenGLWindow::pictureNotPresented(int64_t now, bool failHook)
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
            dropPresenter();
            update();  // the timer's present at once makes it
            return true;
        case PresenterRecovery::BeginFailure::FallBack:
            _presenterFallback = "stopped presenting " + std::to_string(PresenterRecovery::kMaxRestarts + 1) +
                                 " times within a minute (" + failure + ")";
            if (_vulkan)
            {
                // The Vulkan renderer goes with its presenter (kept, GL's paint would draw nothing: an empty
                // window for good); GL draws and tries its own presenter
                const std::string why = _presenterFallback;
                _presenterFallback.clear();
                _recovery = PresenterRecovery();
                dropVulkan(why);
            }
            else
                fallBackToGl(_presenterFallback);
            return true;
    }
    return false;
}

void DeviceScreenGLWindow::fallBackToGl(const std::string& why)
{
    qWarning().noquote() << "DeviceScreenGLWindow: the native presenter" << QString::fromStdString(why)
                         << "- presenting with GL from now on";
    dropPresenter();  // the context current (renderNative)
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
        if (_vulkan)
            out.presenter = "vulkan";  // the Vulkan renderer's swapchain (gpu::VulkanPresenter) on every platform
        const NativePresenter::Stats s = _presenter->GetStats();
        out.timedAt = "screen";
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
    FillSmoothStep(_smoothSteppedDown, _smoothTooSlow, out);
    out.smoothBehind = _smoothBehind;
    out.smoothStarved = _smoothStarved;
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
    out.lowLatency = _shownInfo.atOnce;
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
    gVulkanProbe = -1;  // asked again with the rest
    QString renderer;
    const int available = probeGPUAccelerationOnce(renderer) ? 1 : 0;
    const bool changed = available != gGpuProbe.load() || renderer != gGpuRenderer;
    gGpuRenderer = renderer;
    gGpuProbe.store(available);
    return changed;
}

std::vector<std::string> DeviceScreenGLWindow::availableRenderers()
{
    std::vector<std::string> renderers = {"gl"};
    if (VulkanAvailable())
        renderers.emplace_back("vulkan");
    return renderers;
}

bool DeviceScreenGLWindow::vulkanBuilt()
{
#ifdef UNREAL_VULKAN
    return true;
#else
    return false;
#endif
}

std::string DeviceScreenGLWindow::wantedRenderer()
{
    return WantedRenderer();
}

bool DeviceScreenGLWindow::setWantedRenderer(const std::string& name)
{
    if (name != "gl" && name != "vulkan")
        return false;
    if (name == WantedRenderer())
        return true;
    WantedRenderer() = name;
    gGpuProbe.store(-1);  // the GPU probe asks the renderer's own GPU: again at the next ask
    return true;
}

bool DeviceScreenGLWindow::probeGPUAccelerationOnce(QString& rendererName)
{
    // The Vulkan renderer draws on a Vulkan GPU, whatever GL the display has (Xvfb's llvmpipe beside an NVIDIA): that
    // GPU decides. None: the GL probe below (the window falls back to GL)
    if (UseVulkanRenderer())
    {
        rendererName = gVulkanName;
        return true;
    }
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
    _baseRatio = ratio;
    _frameGeometry = PictureGeometry{};

    _devicePixelsRect = QRectF(0.0, 0.0, width, height);
    _devicePixels = new QImage(static_cast<const unsigned char*>(buffer), width, height, QImage::Format_RGBA8888);

    _latchedFrame = QImage(width, height, QImage::Format_RGBA8888);
    _latchedFrame.fill(Qt::black);
    _latchedContent = 0;

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
    _latchedContent = 0;
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
        _latchedContent = 0;  // another source: what _latchedFrame holds is no version of its frames
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
    // Settings set again (another device for the effects, another mode): the interpolation gets its chance again
    _smoothTooSlow = false;
    _smoothSteppedDown = false;
    _smooth.SetLite(false);
    _smoothGuard.StartOver(_presenter ? _presenter->LatePresents() : 0);
    _uploadedSerial = 0;
    updateDisplayRate();
    updateVideoSync();
    if (syncWatched())
        _syncTimer->start();
    else
        _syncTimer->stop();
}

void DeviceScreenGLWindow::updateVideoSync()
{
    bool vrr = false;
    bool vrrPending = false;
    double displayHz = 0.0;
    double presenterMinHz = 0.0;
    if (_presenter)
    {
        const NativePresenter::Stats s = _presenter->GetStats();
        vrr = s.vrr;
        vrrPending = s.vrrPending;
        displayHz = s.maxRefreshHz;
        presenterMinHz = s.minRefreshHz;
    }
    // The display rate step follows what the presenter sees now, whatever smooth motion is set to: a VRR that
    // engaged or left, a VRR check that came to its answer (the mode switch waited for it)
    if ((vrr != _syncVrr || vrrPending != _syncVrrPending) && _displayRate == DisplayRate::Match)
    {
        _syncVrr = vrr;
        _syncVrrPending = vrrPending;
        updateDisplayRate();
    }
    if (!_smoothAuto)
    {
        _sync = VideoSync::Off;
        _syncReason.clear();
        // Blend or interpolate set by hand: only on a display that beats with the machine
        double hz = _displayState == "matched" && _displayHz > 0.0 ? _displayHz : displayHz;
        if (hz <= 0.0 && screen())
            hz = screen()->refreshRate();
        const bool helps = SmoothMotionHelps(vrr, hz, _frameRate.Hz());
        if (helps != _smoothHelps)
        {
            qInfo() << "DeviceScreenGLWindow: smooth motion" << (helps ? "draws" : "not drawn") << "(display" << hz
                    << "Hz, machine" << _frameRate.Hz() << "Hz" << (vrr ? ", VRR)" : ")");
            _smoothHelps = helps;
        }
        return;
    }
    _smoothHelps = true;
    if (_displayState == "matched" && _displayHz > 0.0)
        displayHz = _displayHz;
    if (displayHz <= 0.0 && screen())
        displayHz = screen()->refreshRate();
    const double machineHz = _frameRate.Hz();
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
    // Smooth motion on or off: the interpolation starts from the frames that come from now on, its guard as well
    _smoothGuard.Reset(_presenter ? _presenter->LatePresents() : 0);
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
    const NativePresenter::Stats presenterStats = _presenter ? _presenter->GetStats() : NativePresenter::Stats{};
    if (presenterStats.vrr)
        return restoreDisplayRate("the display shows each frame at its own time (VRR)");
    if (presenterStats.vrrPending)
    {
        // Not known yet whether the display follows the frames: a mode switched now would be switched back a second
        // later on a VRR display (two blankings). updateVideoSync comes again when the presenter knows
        if (!_displaySwitch || !_displaySwitch->Switched())
        {
            _displayState = "unchanged";
            _displayReason = "the display's VRR check is not done yet";
        }
        return;
    }
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
    _smoothGuard.Reset(_presenter ? _presenter->LatePresents() : 0);  // the switch's blanking is no late picture
    _displayHz = pick.hz;
    _displayState = "matched";
    _displayReason.clear();
}

void DeviceScreenGLWindow::watchScreenRate()
{
    // The rate of the screen under the window: the system or the user changes it (a mode set in the display
    // settings, back from sleep on another mode), the screen stays the same one. The display rate step and the sync
    // step are taken again against it. Queued: the window's own mode switch gets here too, from inside its Set
    disconnect(_screenRateChanged);
    if (QScreen* display = screen())
        _screenRateChanged = connect(
            display, &QScreen::refreshRateChanged, this,
            [this](qreal hz) {
                if (_isShuttingDown)
                    return;
                qInfo() << "DeviceScreenGLWindow: the display's rate is" << hz << "Hz now";
                // The presents late across the switch (the display blanks) are not the interpolation's
                _smoothGuard.Reset(_presenter ? _presenter->LatePresents() : 0);
                updateDisplayRate();
                updateVideoSync();
            },
            Qt::QueuedConnection);
}

void DeviceScreenGLWindow::settleSmoothGuard()
{
    _smoothGuard.Reset(_presenter ? _presenter->LatePresents() : 0);
    _smoothGuardFromNs = SteadyNowNs() + kSmoothGuardSettleNs;
}

void DeviceScreenGLWindow::noteInterpolated(int64_t drawNs)
{
    if (SteadyNowNs() < _smoothGuardFromNs)
    {
        _smoothGuard.Reset(_presenter ? _presenter->LatePresents() : 0);
        return;
    }
    // An interpolated picture drawn. Do they reach the screen in time? Told by what the presenter saw, whatever the
    // device and the picture's size: of the last kSmoothGuardWindow of them, kSmoothGuardLates or more presents a
    // refresh late is an interpolation this machine cannot draw within a refresh (the Steam Deck: 18.9 ms a picture
    // on its GPU at 16.7 ms a refresh; 13 pictures a second reached the screen, and the lead those lates widened to
    // its bound halved the blend's too). Then it steps down for a while (SmoothGuard::SteppedDown, retrySmoothStep):
    // first to the cheap interpolation (LiteFlow, about 1 / 30 of the time on one thread), and if that is late as
    // well (or was the mode chosen), to the blend. Not in the first seconds of a window shown (settleSmoothGuard)
    // Without a presenter (the GL swap alone) the drawing's own time tells
    if (_smoothGuard.Note(_presenter ? _presenter->LatePresents() : 0, drawNs, refreshPeriodNs()))
    {
        if (!_smooth.Lite())
        {
            _smoothSteppedDown = true;
            _smooth.SetLite(true);
            _smoothShown = {};
        }
        else
            _smoothTooSlow = true;
        _smoothGuard.SteppedDown(SteadyNowNs());
        qWarning() << "DeviceScreenGLWindow: smooth motion's interpolation is too slow here (of its last"
                   << SmoothGuard::kWindow << "pictures" << _smoothGuard.ClosedLates() << "a refresh late,"
                   << _smoothGuard.ClosedSlow() << "drawn in most of a refresh) -"
                   << (_smoothTooSlow ? "blended" : "the cheap interpolation") << "for a while";
    }
}

void DeviceScreenGLWindow::retrySmoothStep()
{
    // A step down tried again after its wait (SmoothGuard::SteppedDown): one level up, the guard counting afresh
    if (!(_smoothTooSlow || _smoothSteppedDown) || !_smoothGuard.RetryDue(SteadyNowNs()))
        return;
    if (_smoothTooSlow)
        _smoothTooSlow = false;  // the cheap interpolation again (or the full one, if it was the mode chosen)
    else
    {
        _smoothSteppedDown = false;
        _smooth.SetLite(false);
        _smoothShown = {};
    }
    _smoothGuard.Reset(_presenter ? _presenter->LatePresents() : 0);
    if (_smoothSteppedDown)
        _smoothGuard.RetryLater(SteadyNowNs());  // the full interpolation is tried next, after the same wait
    qInfo() << "DeviceScreenGLWindow: smooth motion tries" << (_smooth.Lite() ? "the cheap interpolation" : "the interpolation")
            << "again";
}

void DeviceScreenGLWindow::dropPresenter()
{
    _presenter.reset();
    _smoothDrained = 0;
    _smoothLag = 0;
}

bool DeviceScreenGLWindow::smoothActive() const
{
    // Low-latency video shows each frame at once: smooth motion would wait for the next one
    // Turbo, stepping (the latch clock): frames not paced to real time have no picture between them, and feeding
    // them (copies under the frame source's mutex, a flicker search each) only held the emulation up
    return _smooth.GetMode() != smoothmotion::Mode::Off && _nextFrameSource && !_temporalEnabled &&
           !_shownInfo.atOnce && _shownInfo.source != FrameClockSource::Latch &&
           (_smoothAuto ? _sync == VideoSync::Interpolate : _smoothHelps);
}

void DeviceScreenGLWindow::feedSmooth()
{
    // Each frame once, in order. The screen holds a few: a backlog past them is gone anyway. Only the copies here,
    // under the mutex; the engine's work on them (its flicker search) in pushSmooth
    uint64_t last = _smoothFed > 0 ? _smoothFeedInfo[_smoothFed - 1].serial : _smooth.LastSerial();
    // Started over (Reset: settings, the sync step, a new source): from a few frames before the one due, not from the
    // oldest the screen holds (a feed takes as many as it has room for, and the newest fed was then three frames
    // behind the one due: shown for a refresh, a step back)
    if (last == 0 && _shownInfo.serial > kSmoothLookBehind)
        last = _shownInfo.serial - kSmoothLookBehind;
    while (_smoothFed < _smoothFeed.size())
    {
        QImage& frame = _smoothFeed[_smoothFed];
        if (frame.size() != _latchedFrame.size())
            frame = QImage(_latchedFrame.size(), QImage::Format_RGBA8888);
        PresentFrameInfo next;
        if (!_nextFrameSource(last, frame.bits(), static_cast<size_t>(frame.sizeInBytes()), &next) || next.serial == 0)
            break;
        last = next.serial;
        // Of another size than the window's frames (a video mode switch, the window's new size still queued): its
        // pixels are not rows of this image. Left out; the engine starts over from the next one it gets
        if (next.geometry.width != 0 &&
            (next.geometry.width != frame.width() || next.geometry.height != frame.height()))
            continue;
        _smoothFeedInfo[_smoothFed++] = next;
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
        _smoothPushed[_smoothNextPushed] = {info.serial, info.content};
        _smoothNextPushed = (_smoothNextPushed + 1) % _smoothPushed.size();
        _smoothTrace.Frame(info.serial, info.targetNs, reinterpret_cast<const uint32_t*>(frame.constBits()),
                           frame.width(), frame.height());
    }
    _smoothFed = 0;
}

uint64_t DeviceScreenGLWindow::pushedContent(uint64_t serial) const
{
    for (const PushedContent& pushed : _smoothPushed)
        if (pushed.serial == serial)
            return pushed.content;
    return 0;
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
    if (syncWatched() && !_syncTimer->isActive() && isExposed())
        _syncTimer->start();
    if (_native)
    {
        // The display link runs while frames come; started again after an idle stop (the timer's way draws until it
        // ticks)
        if (_presenter && _presenter->PacesRefreshes())
        {
            _presenter->WantRefreshes(true);
            if (linkPacing())
            {
                if (_shownInfo.atOnce)
                {
                    _latchWake = true;
                    _presentTimer->start(0);
                }
                return;
            }
        }
        // The timer runs while frames come; started again after an idle stop. Low-latency video: a new frame is
        // drawn and presented now, not at the wake planned for the next refresh
        if (!_presentTimer->isActive() || _shownInfo.atOnce)
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

void DeviceScreenGLWindow::setPictureView(PictureView::Mode mode)
{
    if (mode == _pictureView)
        return;
    _pictureView = mode;
    _frameHistory.clear();
    applyPictureViewRatio();
    _textureNeedsUpdate = true;
    update();
}

void DeviceScreenGLWindow::applyPictureViewRatio()
{
    // The shape of the cut: the window draws the picture in it, fitted by height with the aspect kept
    if (_pictureView == PictureView::Mode::Full || _devicePixelsRect.isEmpty())
    {
        ratio = _baseRatio;
        return;
    }
    const PictureView::Cut cut = PictureView::CutOf(_frameGeometry, static_cast<uint16_t>(_devicePixelsRect.width()),
                                                    static_cast<uint16_t>(_devicePixelsRect.height()), _pictureView);
    ratio = cut.aspect > 0.0 ? static_cast<float>(cut.aspect) : _baseRatio;
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

#ifdef UNREAL_VULKAN
    if (_vulkan && !createVulkanDevice())
        _vulkan = false;
#endif
    if (!_gpu)
        _gpu = gpu::GlDevice::Create();
    if (!_gpu)
    {
        qWarning() << "DeviceScreenGLWindow: no GPU device on the context";
        return;
    }
    std::string log;
    _composeShader = _gpu->CreateProgram(CRT_VERTEX_SHADER, CRTRenderer::kCompositionShader, log);
    if (!_composeShader)
        qWarning() << "CRT composition shader failed:" << QString::fromStdString(log);
    for (CrtTables& tables : _crtTables)
        if (!tables.modelGpu.create(_gpu.get()))
            qWarning() << "CRT model shader failed:" << tables.modelGpu.log();

    // A custom shader or preset chosen before the context existed
    if (!_customShaderSource.isEmpty())
        compileCustomShader(_customShaderSource);
    if (!_slangPresetName.isEmpty() && !applySlangPreset(_slangPresetName))
        _slangPresetName.clear();
    if (!_gpuTimes.Init(_gpu.get()))
        qInfo() << "DeviceScreenGLWindow: no GPU timestamps, the GPU pass times stay 0";
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
    if (!_gpu)
        return false;
    auto runner = std::make_unique<SlangPresetRunner>();
    if (!runner->load(SlangPresetRunner::presetDirectory(name), _gpu.get()))
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
    _customShader.reset();
    if (source.isEmpty() || !_gpu)
        return source.isEmpty();
    std::string log;
    _customShader =
        _gpu->CreateProgram(CRT_VERTEX_SHADER, CRTRenderer::coreFragmentSource(source).toStdString(), log);
    if (_customShader)
        return true;
    qWarning() << "Custom CRT shader failed:" << QString::fromStdString(log);
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
    if (_gpu && _gpu->GetBackend() == gpu::Device::Backend::Vulkan)
    {
        // Into a texture of its own, read back (GL rows: turned over)
        auto target = _gpu->CreateTexture(pixels.width(), pixels.height(), gpu::Format::RGBA8);
        if (target)
        {
            _renderingImage = true;
            updateTexture(refreshTargetNs());
            const uint32_t windowCount = _slangRunner ? _slangRunner->frameCount() : 0;
            if (_slangRunner)
                _slangRunner->setFrameCount(_imageFrameCount++);
            _frameTarget = target.get();
            paintGL();
            _frameTarget = nullptr;
            if (_slangRunner)
                _slangRunner->setFrameCount(windowCount);
            _renderingImage = false;
            std::vector<uint8_t> rows(static_cast<size_t>(pixels.width()) * pixels.height() * 4);
            if (_gpu->Read(*target, {0, 0, pixels.width(), pixels.height()}, rows.data()))
            {
                QImage read(pixels, QImage::Format_RGBA8888);
                const size_t row = static_cast<size_t>(pixels.width()) * 4;
                for (int y = 0; y < pixels.height(); ++y)
                    std::memcpy(read.scanLine(y), &rows[static_cast<size_t>(pixels.height() - 1 - y) * row], row);
                image = read.convertToFormat(QImage::Format_ARGB32_Premultiplied);
            }
        }
    }
    else
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
    if (hud && _overlayPainter)
    {
        QPainter painter(&image);
        _overlayPainter(painter, width(), height());
    }
    return image;
}

void DeviceScreenGLWindow::setRecordingCapture(bool enabled, int pictureHeight)
{
    _recordCapture = enabled;
    _recordHeight = pictureHeight;
    _recordSerial = 0;
    if (!enabled)
        _recordFrame = {};
    const bool release = !enabled && (_recordTarget
#ifdef __APPLE__
                                      || _recordSurfaces
#endif
                                     );
    if ((!_recordPending.empty() || release) && isValid() && makeCurrent())
    {
        for (PendingCapture& capture : _recordPending)
            dropCapture(capture);
        if (!enabled)
        {
            _recordTarget.reset();  // a GL texture goes with its context current
            _recordScaled.reset();
            _recordNv12.reset();
            _recordSource.reset();
#ifdef __APPLE__
            if (_recordSurfaces)
                _recordSurfaces->Release();
            _recordSurfaces.reset();
#endif
        }
        doneCurrent();
    }
    _recordPending.clear();
}

void DeviceScreenGLWindow::setRecordingFrame(const RecordingFrameLayout& layout)
{
    _recordFrame = layout;
}

void DeviceScreenGLWindow::dropCapture(PendingCapture& capture)
{
#ifdef __APPLE__
    if (_recordSurfaces && capture.composed.pixelBuffer)
        _recordSurfaces->Drop(capture.composed);
#endif
    (void)capture;  // a read in a slot is simply started again over
}

bool DeviceScreenGLWindow::composeRecordingFrame(const gpu::Rect& rect, const QSize& kept, int slot)
{
#ifdef __APPLE__
    if (_recordFrame.size.isEmpty() || _recordFrame.nv12 || !_gpu || _gpu->GetBackend() != gpu::Device::Backend::GL ||
        !_recordTarget)
        return false;
    if (!_recordSurfaces)
        _recordSurfaces = std::make_unique<RecordingSurfaces>();
    if (!_recordSurfaces->Configure(_recordFrame.size.width(), _recordFrame.size.height()))
        return false;
    // The picture where the CPU way puts it (WindowRecorder::Writer::composeInto): in scale x scale blocks; a few
    // pixels off its place, centered as it is (the extra edge cut); else scaled into the place's corner, its aspect
    // kept
    const int scale = std::clamp(_recordFrame.scale, 1, 4);
    const QSize picture = kept * scale;
    const QRect place = _recordFrame.screenRect;
    QRect from(QPoint(0, 0), picture);  // the picture's pixels, rows from the top
    QRect to;
    bool linear = false;
    if (std::abs(picture.width() - place.width()) <= 4 && std::abs(picture.height() - place.height()) <= 4)
    {
        const int left = place.x() + (place.width() - picture.width()) / 2;
        const int top = place.y() + (place.height() - picture.height()) / 2;
        to = QRect(left, top, picture.width(), picture.height()).intersected(QRect(QPoint(0, 0), _recordFrame.size));
        from = QRect(to.x() - left, to.y() - top, to.width(), to.height());
    }
    else
    {
        to = QRect(place.topLeft(), picture.scaled(place.size(), Qt::KeepAspectRatio));
        linear = true;
    }
    if (to.isEmpty() || from.isEmpty())
        return false;
    if (!_recordCopyProgram)
    {
        std::string log;
        _recordCopyProgram = _gpu->CreateProgram(CRT_VERTEX_SHADER, kRecordCopyFragment, log);
        if (!_recordCopyProgram)
        {
            qWarning() << "DeviceScreenGLWindow: the recording copy program failed:" << QString::fromStdString(log);
            return false;
        }
    }
    auto& gl = static_cast<gpu::GlDevice&>(*_gpu);
    // The source: the drawn picture, a texel per `scale` pixels (a nearest draw makes the blocks); scaled smooth too,
    // the blocks are made first (the CPU way's order: the CRT mask stays sharp, then the whole is scaled)
    gpu::Texture* source = _recordTarget.get();
    gpu::Rect sourceRect = rect;
    int sourceScale = scale;
    if (linear && scale > 1)
    {
        if (!_recordScaled || _recordScaled->Width() != picture.width() || _recordScaled->Height() != picture.height())
            _recordScaled = _gpu->CreateTexture(picture.width(), picture.height(), gpu::Format::RGBA8);
        if (!_recordScaled)
            return false;
        const float w = static_cast<float>(_recordTarget->Width());
        const float h = static_cast<float>(_recordTarget->Height());
        _recordCopyProgram->SetTexture("source", _recordTarget.get(), {});
        // Texture to texture, GL rows both: the quad's top corners (texRect.xy) take the source's top rows
        _recordCopyProgram->SetFloat4("texRect", static_cast<float>(rect.x) / w,
                                      static_cast<float>(rect.y + rect.height) / h,
                                      static_cast<float>(rect.x + rect.width) / w, static_cast<float>(rect.y) / h);
        gpu::DrawState state;
        state.viewport = {0, 0, picture.width(), picture.height()};
        gl.Draw(*_recordCopyProgram, *_recordScaled, state);
        _recordCopyProgram->SetTexture("source", nullptr);
        source = _recordScaled.get();
        sourceRect = {0, 0, picture.width(), picture.height()};
        sourceScale = 1;
    }
    // `from` in the source's GL rows (from the bottom), as texture coordinates. The frame's GL row r is its row r
    // from the top: the quad's top corners (texRect.xy) land on the frame's lower rows, so they take the picture's
    // lower rows; the picture comes out the right way up
    const float sourceW = static_cast<float>(source->Width() * sourceScale);
    const float sourceH = static_cast<float>(source->Height() * sourceScale);
    const float originX = static_cast<float>(sourceRect.x * sourceScale);
    const float originY = static_cast<float>(sourceRect.y * sourceScale);
    const float x0 = (originX + static_cast<float>(from.x())) / sourceW;
    const float x1 = (originX + static_cast<float>(from.x() + from.width())) / sourceW;
    const float y0 = (originY + static_cast<float>(picture.height() - from.y() - from.height())) / sourceH;
    const float y1 = (originY + static_cast<float>(picture.height() - from.y())) / sourceH;
    const QColor color = _recordFrame.background;
    const float background[3] = {static_cast<float>(color.redF()), static_cast<float>(color.greenF()),
                                 static_cast<float>(color.blueF())};
    const int frameW = _recordFrame.size.width();
    const int frameH = _recordFrame.size.height();
    const auto draw = [&](unsigned framebuffer) {
        std::unique_ptr<gpu::Texture> frame = gl.WrapFramebuffer(framebuffer, frameW, frameH);
        if (!frame)
            return false;
        gpu::Sampler sampler;
        sampler.filter = linear ? gpu::Filter::Linear : gpu::Filter::Nearest;
        _recordCopyProgram->SetTexture("source", source, sampler);
        _recordCopyProgram->SetFloat4("texRect", x0, y0, x1, y1);
        gpu::DrawState state;
        state.viewport = {to.x(), to.y(), to.width(), to.height()};
        gl.Draw(*_recordCopyProgram, *frame, state);
        _recordCopyProgram->SetTexture("source", nullptr);
        return true;
    };
    PendingCapture capture;
    if (!_recordSurfaces->Compose(background, draw, capture.composed))
        return false;
    capture.slot = slot;
    capture.size = kept;
    capture.frame = shownFrameNumber();
    _recordPending.push_back(std::move(capture));
    return true;
#else
    (void)rect;
    (void)kept;
    (void)slot;
    return false;
#endif
}

bool DeviceScreenGLWindow::packRecordingNv12(const gpu::Rect& rect, const QSize& kept, QSize& image)
{
    if (!_recordFrame.nv12 || _recordFrame.size.isEmpty() || !_gpu || !_recordTarget || kept.isEmpty())
        return false;
    const QRect place = _recordFrame.screenRect;
    if (place.isEmpty() || ((place.width() | place.height()) & 1) != 0)
        return false;
    // The picture where the CPU way puts it in its place (composeNv12 / composeInto): in scale x scale blocks; a few
    // pixels off, centered as it is (the extra edge cut); else scaled into the place's corner, its aspect kept (the
    // blocks made first, then the whole scaled smooth: the CPU way's order, the CRT mask stays sharp)
    const int scale = std::clamp(_recordFrame.scale, 1, 4);
    const QSize picture = kept * scale;
    const bool linear =
        std::abs(picture.width() - place.width()) > 4 || std::abs(picture.height() - place.height()) > 4;
    QRect inside;
    if (linear)
        inside = QRect(QPoint(0, 0), picture.scaled(place.size(), Qt::KeepAspectRatio));
    else
        inside = QRect(QPoint((place.width() - picture.width()) / 2, (place.height() - picture.height()) / 2), picture)
                     .intersected(QRect(QPoint(0, 0), place.size()));
    if (inside.isEmpty())
        return false;
    // The source: the drawn picture, a texel per `scale` pixels; scaled smooth, the blocks drawn first (nearest)
    gpu::Texture* source = _recordTarget.get();
    gpu::Rect from = rect;
    int texelPixels = scale;
    if (linear && scale > 1)
    {
        if (!_recordCopyProgram)
        {
            std::string log;
            _recordCopyProgram = _gpu->CreateProgram(CRT_VERTEX_SHADER, kRecordCopyFragment, log);
            if (!_recordCopyProgram)
            {
                qWarning() << "DeviceScreenGLWindow: the recording copy program failed:" << QString::fromStdString(log);
                return false;
            }
        }
        if (!_recordScaled || _recordScaled->Width() != picture.width() || _recordScaled->Height() != picture.height())
            _recordScaled = _gpu->CreateTexture(picture.width(), picture.height(), gpu::Format::RGBA8);
        if (!_recordScaled)
            return false;
        const float tw = static_cast<float>(_recordTarget->Width());
        const float th = static_cast<float>(_recordTarget->Height());
        _recordCopyProgram->SetTexture("source", _recordTarget.get(), {});
        // Texture to texture, GL rows both: the quad's top corners (texRect.xy) take the source's top rows
        _recordCopyProgram->SetFloat4("texRect", static_cast<float>(rect.x) / tw,
                                      static_cast<float>(rect.y + rect.height) / th,
                                      static_cast<float>(rect.x + rect.width) / tw, static_cast<float>(rect.y) / th);
        gpu::DrawState blocks;
        blocks.viewport = {0, 0, picture.width(), picture.height()};
        _gpu->Draw(*_recordCopyProgram, *_recordScaled, blocks);
        _recordCopyProgram->SetTexture("source", nullptr);
        source = _recordScaled.get();
        from = {0, 0, picture.width(), picture.height()};
        texelPixels = 1;
    }
    const float w = static_cast<float>(source->Width());
    const float h = static_cast<float>(source->Height());
    // A pixel of the picture (from its top left) is (from.x + px / n, from.y + from.h - py / n) in the source's GL
    // texels (n: pixels per texel): its texture coordinate goes from the picture's top left corner, then per pixel of
    // the place (scaled: the picture's pixels per the place's)
    float stepX = 1.0f / (w * static_cast<float>(texelPixels));
    float stepY = -1.0f / (h * static_cast<float>(texelPixels));
    float originX = static_cast<float>(from.x) / w;
    float originY = static_cast<float>(from.y + from.height) / h;
    if (linear)
    {
        stepX *= static_cast<float>(picture.width()) / static_cast<float>(inside.width());
        stepY *= static_cast<float>(picture.height()) / static_cast<float>(inside.height());
    }
    else
    {
        originX -= static_cast<float>((place.width() - picture.width()) / 2) * stepX;
        originY -= static_cast<float>((place.height() - picture.height()) / 2) * stepY;
    }
    if (!_recordNv12Program)
    {
        std::string log;
        _recordNv12Program = _gpu->CreateProgram(CRT_VERTEX_SHADER, kRecordNv12Fragment, log);
        if (!_recordNv12Program)
        {
            qWarning() << "DeviceScreenGLWindow: the recording NV12 program failed:" << QString::fromStdString(log);
            _recordFrame.nv12 = false;  // the pictures as they are from now on
            return false;
        }
    }
    const int texels = (place.width() + 3) / 4;
    const int rows = place.height() * 3 / 2;
    if (!_recordNv12 || _recordNv12->Width() != texels || _recordNv12->Height() != rows)
        _recordNv12 = _gpu->CreateTexture(texels, rows, gpu::Format::RGBA8);
    if (!_recordNv12)
        return false;
    gpu::Sampler sampler;
    sampler.filter = linear ? gpu::Filter::Linear : gpu::Filter::Nearest;
    const QColor color = _recordFrame.background;
    const int32_t background[3] = {color.red(), color.green(), color.blue()};
    gpu::Program& p = *_recordNv12Program;
    p.SetTexture("source", source, sampler);
    p.SetFloat2("origin", originX, originY);
    p.SetFloat2("stepPx", stepX, stepY);
    p.SetInt4("inside", inside.left(), inside.top(), inside.left() + inside.width(), inside.top() + inside.height());
    p.SetInts("background", background, 3);
    p.SetInt("height", place.height());
    p.SetFloat4("texRect", 0.0f, 0.0f, 1.0f, 1.0f);
    gpu::DrawState state;
    state.viewport = {0, 0, texels, rows};
    _gpu->Draw(p, *_recordNv12, state);
    p.SetTexture("source", nullptr);
    // The bytes: the place's width (the texels' last few may pass it), its rows and half as many again
    image = QSize(place.width(), rows);
    return true;
}

bool DeviceScreenGLWindow::drawRecordingPicture(gpu::Rect& rect, QSize& kept)
{
    const QSize window = (QSizeF(size()) * devicePixelRatio()).toSize();
    if (_recordHeight > 0)
        _renderPicture = QSize(static_cast<int>(_recordHeight * static_cast<double>(ratio)), _recordHeight);
    const QSize pixels = _renderPicture.isEmpty() ? window : _renderPicture;
    const QRect picture = pictureDeviceRect().intersected(QRect(QPoint(0, 0), pixels));
    if (pixels.isEmpty() || picture.isEmpty())
    {
        _renderPicture = {};
        return false;
    }
    if (!_recordTarget || _recordTarget->Width() != pixels.width() || _recordTarget->Height() != pixels.height())
        _recordTarget = _gpu->CreateTexture(pixels.width(), pixels.height(), gpu::Format::RGBA8);
    if (!_recordTarget)
    {
        _renderPicture = {};
        return false;
    }
    // Smooth motion's picture is the display's, between two frames: the recording draws the frame due itself (the
    // latched one, whose number the picture carries) in the texture's place, and as the CPU-side frame the CRT
    // composition grades
    const bool frameItself = _shownFrame == &_smoothFrame && !_latchedFrame.isNull() &&
                             _latchedFrame.format() == QImage::Format_RGBA8888;
    if (frameItself)
    {
        const int w = _latchedFrame.width();
        const int h = _latchedFrame.height();
        if (!_recordSource || _recordSource->Width() != w || _recordSource->Height() != h)
            _recordSource = _gpu->CreateTexture(w, h, gpu::Format::RGBA8);
        if (!_recordSource)
        {
            _renderPicture = {};
            return false;
        }
        _gpu->Upload(*_recordSource, {0, 0, w, h}, _latchedFrame.constBits(),
                     static_cast<int>(_latchedFrame.bytesPerLine() / 4));
        std::swap(_texture, _recordSource);
        _shownFrame = &_latchedFrame;  // the CRT composition reads its pixels on the CPU (grade, prepare)
    }
    _renderingImage = true;
    const uint32_t windowCount = _slangRunner ? _slangRunner->frameCount() : 0;
    if (_slangRunner)
        _slangRunner->setFrameCount(_imageFrameCount++);  // the pictures count their own (interlace presets)
    if (_gpu->GetBackend() == gpu::Device::Backend::Vulkan)
    {
        gpu::Texture* windowTarget = _frameTarget;
        _frameTarget = _recordTarget.get();
        paintGL();
        _frameTarget = windowTarget;
    }
    else
    {
        GLint previous = 0;
        glGetIntegerv(GL_FRAMEBUFFER_BINDING, &previous);
        glBindFramebuffer(GL_FRAMEBUFFER, static_cast<gpu::GlDevice&>(*_gpu).FramebufferId(*_recordTarget));
        paintGL();
        glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(previous));
    }
    if (_slangRunner)
        _slangRunner->setFrameCount(windowCount);
    _renderingImage = false;
    if (frameItself)
    {
        std::swap(_texture, _recordSource);
        _shownFrame = &_smoothFrame;
    }
    _renderPicture = {};
    // GL rows: the picture's rectangle counted from the bottom
    rect = {picture.x(), pixels.height() - picture.y() - picture.height(), picture.width(), picture.height()};
    kept = picture.size();
    return true;
}

void DeviceScreenGLWindow::captureForRecording()
{
    if (!_recordCapture || !_gpu || !_texture)
        return;
    // One picture per frame: smooth motion presents every refresh, a forced repaint the same frame again
    const uint64_t serial = _shownInfo.serial != 0 ? _shownInfo.serial : _frameSerial;
    if (serial == _recordSerial)
        return;
    _recordSerial = serial;
    // The texture holds the frame just presented: drawn as it is (no updateTexture: that would take the next frame
    // out of the window's queue and feed smooth motion a second time)
    gpu::Rect rect;
    QSize kept;
    if (!drawRecordingPicture(rect, kept))
        return;
    if (_recordPending.size() >= static_cast<size_t>(gpu::Device::kReadSlots))
    {
        dropCapture(_recordPending.front());  // not taken (the GUI busy elsewhere): its slot is the one started now
        _recordPending.pop_front();
    }
    QSize nv12;
    if (composeRecordingFrame(rect, kept, _recordSlot))
        _recordSlot = (_recordSlot + 1) % gpu::Device::kReadSlots;
    else if (packRecordingNv12(rect, kept, nv12)
                 ? _gpu->BeginRead(_recordSlot, *_recordNv12, {0, 0, _recordNv12->Width(), _recordNv12->Height()})
                 : _gpu->BeginRead(_recordSlot, *_recordTarget, rect, true))
    {
        PendingCapture capture;
        capture.slot = _recordSlot;
        capture.nv12 = !nv12.isEmpty();
        capture.size = capture.nv12 ? nv12 : kept;
        capture.frame = shownFrameNumber();
        _recordPending.push_back(std::move(capture));
        _recordSlot = (_recordSlot + 1) % gpu::Device::kReadSlots;
    }
}

uint64_t DeviceScreenGLWindow::shownFrameNumber() const
{
    return _shownInfo.serial != 0 ? _shownInfo.geometry.frameNumber : 0;
}

bool DeviceScreenGLWindow::grabRecordingPicture(RecordingPicture& out)
{
    if (!_recordCapture || !_gpu || !isValid() || !makeCurrent())
        return false;
    // The window presents nothing (hidden, minimized): the frame due now, uploaded here, read at once
    updateTexture(refreshTargetNs());
    gpu::Rect rect;
    QSize kept;
    bool read = false;
    if (_texture && drawRecordingPicture(rect, kept))
    {
        // A slot no capture waits in: the pending ones are taken before a grab
        const int slot = _recordPending.empty() ? _recordSlot : -1;
        if (slot >= 0 && composeRecordingFrame(rect, kept, slot))
        {
#ifdef __APPLE__
            // The whole frame composed in the encoder's buffer, as a capture is, waited for at once
            PendingCapture capture = std::move(_recordPending.back());
            _recordPending.pop_back();
            out.frame = _recordSurfaces->Finish(capture.composed);
            out.number = capture.frame;
            read = out.frame != nullptr;
#endif
        }
        else if (QSize nv12; slot >= 0 && packRecordingNv12(rect, kept, nv12))
        {
            QImage image(nv12, QImage::Format_Grayscale8);
            read = !image.isNull() &&
                   _gpu->BeginRead(slot, *_recordNv12, {0, 0, _recordNv12->Width(), _recordNv12->Height()}) &&
                   _gpu->FinishRead(slot, image.bits(), static_cast<size_t>(image.bytesPerLine()), false);
            if (read)
            {
                out.image = std::move(image);
                out.number = shownFrameNumber();
            }
        }
        else if (slot >= 0)
        {
            QImage image(kept, QImage::Format_ARGB32);
            read = !image.isNull() && _gpu->BeginRead(slot, *_recordTarget, rect, true) &&
                   _gpu->FinishRead(slot, image.bits(), static_cast<size_t>(image.bytesPerLine()), true);
            if (read)
            {
                out.image = std::move(image);
                out.number = shownFrameNumber();
            }
        }
    }
    doneCurrent();
    return read;
}

void DeviceScreenGLWindow::takeRecordingPictures(std::vector<RecordingPicture>& out)
{
    if (_recordPending.empty() || !_gpu || !makeCurrent())
        return;
    for (PendingCapture& capture : _recordPending)
    {
#ifdef __APPLE__
        if (_recordSurfaces && capture.composed.pixelBuffer)
        {
            RecordingPicture picture;
            picture.frame = _recordSurfaces->Finish(capture.composed);
            picture.number = capture.frame;
            if (picture.frame)
                out.push_back(std::move(picture));
            continue;
        }
#endif
        // B,G,R,A bytes on a little-endian host; NV12's rows as the texels hold them (GL's row 0: the top one)
        QImage image(capture.size, capture.nv12 ? QImage::Format_Grayscale8 : QImage::Format_ARGB32);
        if (!image.isNull() && _gpu->FinishRead(capture.slot, image.bits(), static_cast<size_t>(image.bytesPerLine()),
                                                !capture.nv12))
        {
            RecordingPicture picture;
            picture.image = std::move(image);
            picture.number = capture.frame;
            out.push_back(std::move(picture));
        }
    }
    _recordPending.clear();
    doneCurrent();
}

void DeviceScreenGLWindow::updateTexture(int64_t hostNs, bool atRefresh)
{
    // The source is asked on every refresh (the frame due changes with the time), the legacy buffer only when a frame
    // came. Selection under the frame-source mutex (see clearFrameSource): the legacy fallback deep-copies so
    // post-lock texture upload only touches owned memory.
    const bool forced = _textureNeedsUpdate.exchange(false, std::memory_order_relaxed);
    QImage* sourceImage = nullptr;
    bool newFrame = forced;
    PictureGeometry geometry;
    {
        std::lock_guard<std::mutex> lock(_frameSourceMutex);
        PresentFrameInfo info;
        info.content = _latchedContent;  // the version held: not copied again (a refresh between two frames)
        bool got = _frameSource && !_latchedFrame.isNull() &&
                   _frameSource(hostNs, _latchedFrame.bits(), static_cast<size_t>(_latchedFrame.sizeInBytes()), &info);
        // Of another size than _latchedFrame (a video mode switch, the window's new size still queued): its pixels
        // are not rows of the image, not a picture to show
        if (got && info.geometry.width != 0 &&
            (info.geometry.width != _latchedFrame.width() || info.geometry.height != _latchedFrame.height()))
        {
            got = false;
            _latchedContent = 0;
        }
        if (got)
        {
            _latchedContent = info.content;
            sourceImage = &_latchedFrame;
            geometry = info.geometry;  // the geometry latched with the frame: the picture view cuts by it
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
            const bool smoothNow = smoothActive();
            // Off (turbo, low latency, the temporal blend, another sync step): the presents late meanwhile are not
            // the interpolation's, on again it starts counting from now
            _smoothGuard.Track(smoothNow, _presenter ? _presenter->LatePresents() : 0);
            if (smoothNow)
                retrySmoothStep();
            if (smoothNow && !_smoothWasActive)
            {
                // On again (low latency or temporal blending switched off, turbo ended): from the frames that come
                // now, not from the ones it held when it stopped
                _smooth.Reset();
                _smoothShown = {};
                _smoothFed = 0;
            }
            _smoothWasActive = smoothNow;
            if (smoothNow)
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
            if (!forced && _texture)
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

    _frameGeometry = geometry;
    applyPictureViewRatio();

    // Smooth motion: the picture for the time, drawn again only when it is another one
    _smoothChanged = false;
    _smoothUsed = false;
    QImage* smoothImage = nullptr;
    if (sourceImage == &_latchedFrame && smoothActive() && _smooth.Width() == sourceImage->width() &&
        _smooth.Height() == sourceImage->height())
    {
        // The GPU passes on the window's device (GL: its context current, updateTexture draws), made on the first use
        if (_smoothOnGpu && _gpu && !_smoothGpu.IsCreated() && !_smoothGpuFailed && !_smoothGpu.Create(_gpu.get()))
        {
            _smoothGpuFailed = true;
            qWarning() << "DeviceScreenGLWindow: smooth motion on the CPU, the GPU passes failed:" << _smoothGpu.Log();
        }
        _smoothGpu.SetPassTimes(&_gpuTimes);
        _smooth.SetDevice(_smoothOnGpu && _smoothGpu.IsCreated() ? &_smoothGpu : nullptr);
        const int64_t planNs = atRefresh ? hostNs : _shownInfo.targetNs;
        smoothmotion::SmoothMotion::Result plan = _smooth.Plan(planNs);
        if (plan.kind != smoothmotion::SmoothMotion::Kind::None)
        {
            // Its newest frame is behind the one due (a stall longer than a feed takes up): the frame due as it is,
            // never an older picture. One behind is its own choice (a held flicker, the frame before the time)
            const uint64_t newest = std::max(plan.serial, plan.to);
            if (_shownInfo.serial != 0 && newest + 2 <= _shownInfo.serial)
            {
                _smoothBehind++;
                plan = {};
            }
            // The frame due is the newest one fed: no next frame to go towards, the picture repeats (the lead, the
            // lip-sync trim or the core's delay took the look-ahead)
            else if (plan.kind == smoothmotion::SmoothMotion::Kind::Frame && plan.serial == _smooth.LastSerial() &&
                     plan.serial != _smoothStarvedSerial)
            {
                _smoothStarvedSerial = plan.serial;
                _smoothStarved++;
            }
        }
        // The interpolation's pictures came a refresh late (SmoothGuard below): the blend of the same two frames,
        // which costs nothing; a game's cadence picture: the frame
        const bool interpolates = plan.kind == smoothmotion::SmoothMotion::Kind::Interpolate ||
                                  plan.kind == smoothmotion::SmoothMotion::Kind::Cadence ||
                                  plan.kind == smoothmotion::SmoothMotion::Kind::Carry;
        if (_smoothTooSlow && plan.kind == smoothmotion::SmoothMotion::Kind::Interpolate)
            plan.kind = smoothmotion::SmoothMotion::Kind::Blend;
        else if (_smoothTooSlow && interpolates)
            plan = {smoothmotion::SmoothMotion::Kind::Frame, plan.serial, plan.serial, plan.serial, 0};
        bool kept = false;
        if (plan.kind != smoothmotion::SmoothMotion::Kind::None)
        {
            _smoothUsed = true;
            kept = plan == _smoothShown && !forced && _texture;
            if (!kept)
            {
                if (_smoothFrame.size() != sourceImage->size())
                {
                    _smoothFrame = QImage(sourceImage->size(), QImage::Format_RGBA8888);
                    _smoothShown = {};  // a new image holds no picture yet
                }
                // The frame due as it is, and _latchedFrame holds it in the version the engine has (the same bits):
                // that image goes up, no copy into _smoothFrame. Not while tracing (the trace reads _smoothFrame)
                const bool latched = plan.kind == smoothmotion::SmoothMotion::Kind::Frame &&
                                     plan.serial == _shownInfo.serial && _shownInfo.content != 0 &&
                                     pushedContent(plan.serial) == _shownInfo.content && !_smoothTrace.Active();
                if (latched)
                {
                    _smoothChanged = !(plan == _smoothShown);
                    _smoothShown = plan;
                    _smoothShownLatched = true;
                    smoothImage = sourceImage;
                }
            }
            if (!kept && smoothImage == nullptr)
            {
                // The GPU's passes alone (the device's segments) and the refresh on the CPU's clock. After a frame that
                // went up from _latchedFrame, _smoothFrame holds no picture: drawn whatever the plan
                const bool draws = !(plan == _smoothShown) || _smoothShownLatched;
                const bool timed = draws && _smooth.GetDevice() != nullptr;
                if (timed)
                    _gpuTimes.Open(GpuPassTimes::Smooth);
                const auto drawStart = std::chrono::steady_clock::now();
                // GL: the layer's passes leave framebuffer 0 bound; the one paintGL draws into comes back
                const bool gl = _gpu && _gpu->GetBackend() == gpu::Device::Backend::GL;
                GLint framebuffer = 0;
                GLint viewport[4] = {};
                if (gl && draws)
                {
                    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &framebuffer);
                    glGetIntegerv(GL_VIEWPORT, viewport);
                }
                const bool drawn = !draws || _smooth.Draw(plan, reinterpret_cast<uint32_t*>(_smoothFrame.bits()));
                if (gl && draws)
                {
                    glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(framebuffer));
                    glViewport(viewport[0], viewport[1], viewport[2], viewport[3]);
                }
                const auto drawTime = std::chrono::steady_clock::now() - drawStart;
                if (draws && interpolates && !_smoothTooSlow)
                    noteInterpolated(std::chrono::duration_cast<std::chrono::nanoseconds>(drawTime).count());
                if (draws)
                    _gpuTimes.Record(GpuPassTimes::SmoothCpu,
                                     std::chrono::duration<double, std::milli>(drawTime).count());
                if (timed)
                    _gpuTimes.Close(GpuPassTimes::Smooth);
                if (drawn)
                {
                    _smoothChanged = !(plan == _smoothShown);
                    _smoothShown = plan;
                    _smoothShownLatched = false;
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
    if (!smoothImage && !newFrame && !forced && _texture)
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
    if (!_gpu)
        return;
    if (!_texture || _texture->Width() != w || _texture->Height() != h)
        _texture = _gpu->CreateTexture(w, h, gpu::Format::RGBA8);
    if (!_texture)
        return;
    _gpu->Upload(*_texture, {0, 0, w, h}, uploadImage->constBits(), static_cast<int>(uploadImage->bytesPerLine() / 4));

    _shownFrame = uploadImage;
    ++_frameSerial;
    _uploadedSerial = sourceImage == &_latchedFrame ? _shownInfo.serial : 0;
}

namespace
{
/// A tight RGBA8 image into a texture, (re)allocated for its size
void UploadRgba(gpu::Device& device, std::unique_ptr<gpu::Texture>& texture, int width, int height,
                const uint8_t* pixels)
{
    if (!texture || texture->Width() != width || texture->Height() != height)
        texture = device.CreateTexture(width, height, gpu::Format::RGBA8);
    if (texture)
        device.Upload(*texture, {0, 0, width, height}, pixels);
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
    GLuint vao = 0;
    auto* gl = QOpenGLContext::currentContext()->extraFunctions();
    gl->glGenVertexArrays(1, &vao);
    gl->glBindVertexArray(vao);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    gl->glBindVertexArray(0);
    gl->glDeleteVertexArrays(1, &vao);
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
    // Vulkan: the presenter's texture (or renderWindowImage's); GL: the bound framebuffer
    const bool vulkan = _gpu && _gpu->GetBackend() == gpu::Device::Backend::Vulkan;
    std::unique_ptr<gpu::Texture> wrapped;
    GLint bound = 0;
    if (vulkan)
    {
        if (!_frameTarget)
            return;
        _gpu->Clear(*_frameTarget, 0.0f, 0.0f, 0.0f, 1.0f);
    }
    else
    {
        glViewport(0, 0, surfaceWidth, surfaceHeight);
        glDisable(GL_DITHER);  // the composition writes exact values
        // The HUD's QPainter of the previous frame may leave its state behind
        glDisable(GL_BLEND);
        glDisable(GL_SCISSOR_TEST);
        glDisable(GL_STENCIL_TEST);
        glDisable(GL_DEPTH_TEST);
        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);  // a preset's framebuffers clear to other colours
        glClear(GL_COLOR_BUFFER_BIT);
        // The bound framebuffer (the window's, the presenter's or renderWindowImage's) as the layer's target
        glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &bound);
        if (_gpu)
            wrapped = static_cast<gpu::GlDevice&>(*_gpu).WrapFramebuffer(static_cast<GLuint>(bound), surfaceWidth,
                                                                          surfaceHeight);
    }
    gpu::Texture* target = vulkan ? _frameTarget : wrapped.get();

    _gpuTimes.Collect();
    const QRect picture = pictureDeviceRect();
    if (target && _texture && !picture.isEmpty())
    {
        _gpuTimes.Begin(GpuPassTimes::Picture);
        // The viewport is the picture, to the device pixel: the quad covers it, the CRT composition reads its tables
        // per pixel. GL counts rows from the bottom (Vulkan's images keep GL's rows, gpudevice.h)
        const int glY = surfaceHeight - picture.y() - picture.height();
        const gpu::Rect viewport{picture.x(), glY, picture.width(), picture.height()};

        // With or without CRT effects the composition draws (CRTProfile::None: the plain nearest scale), the software
        // screen's picture bit for bit
        if (_crtEnabled && _slangRunner)
            drawSlangPreset(picture, *target, viewport);
        else if (_crtEnabled && _customShader)
            drawCustomCrt(picture, *target, viewport);
        else if (_crtEnabled && CRTBeamModel::isModel(_crtParams.profile) && crtTables().modelGpu.isCreated() && _shownFrame)
            drawModelCrt(picture, *target, viewport);
        else if (_composeShader && _shownFrame)
            drawComposedCrt(picture, *target, viewport);
        _gpuTimes.End(GpuPassTimes::Picture);
        if (!vulkan)
        {
            glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(bound));  // the layer leaves 0 bound: the HUD's
            glViewport(0, 0, surfaceWidth, surfaceHeight);
        }
    }

    const bool hudShown = _hudOverlay && _hudOverlay->isVisible();
    if ((hudShown || _overlayPainter) && !_renderingImage)
    {
        if (vulkan)
        {
            drawHudOverTarget(*target, surfaceWidth, surfaceHeight, dpr);
            return;
        }
        // QPainter's glyph cache uploads assume GL's default pixel store and texture unit; a picture pass that left
        // others (alignment 1, a row length) skews every newly cached glyph into diagonal stripes
        glActiveTexture(GL_TEXTURE0);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
        glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
        glPixelStorei(GL_UNPACK_SKIP_ROWS, 0);
        glPixelStorei(GL_UNPACK_SKIP_PIXELS, 0);
        // Into the bound framebuffer: the window's surface or the native way's IOSurface
        QOpenGLPaintDevice device(QSize(surfaceWidth, surfaceHeight));
        device.setDevicePixelRatio(dpr);
        QPainter painter(&device);
        if (hudShown)
            _hudOverlay->renderHUD(painter, width(), height(), dpr);
        if (_overlayPainter)
            _overlayPainter(painter, width(), height());
    }
}

void DeviceScreenGLWindow::drawHudOverTarget(gpu::Texture& target, int surfaceWidth, int surfaceHeight, qreal dpr)
{
    if (!_hudProgram)
    {
        std::string log;
        _hudProgram = _gpu->CreateProgram(CRT_VERTEX_SHADER, kHudFragment, log);
        if (!_hudProgram)
        {
            qWarning() << "DeviceScreenGLWindow: the HUD program failed:" << QString::fromStdString(log);
            return;
        }
    }
    if (_hudImage.width() != surfaceWidth || _hudImage.height() != surfaceHeight)
        _hudImage = QImage(surfaceWidth, surfaceHeight, QImage::Format_RGBA8888_Premultiplied);
    _hudImage.fill(Qt::transparent);
    _hudImage.setDevicePixelRatio(dpr);
    {
        QPainter painter(&_hudImage);
        if (_hudOverlay && _hudOverlay->isVisible())
            _hudOverlay->renderHUD(painter, width(), height(), dpr);
        if (_overlayPainter)
            _overlayPainter(painter, width(), height());
    }
    if (!_hudTexture || _hudTexture->Width() != surfaceWidth || _hudTexture->Height() != surfaceHeight)
        _hudTexture = _gpu->CreateTexture(surfaceWidth, surfaceHeight, gpu::Format::RGBA8);
    if (!_hudTexture)
        return;
    _gpu->Upload(*_hudTexture, {0, 0, surfaceWidth, surfaceHeight}, _hudImage.constBits(),
                 static_cast<int>(_hudImage.bytesPerLine() / 4));
    _hudProgram->SetTexture("hud", _hudTexture.get());
    _hudProgram->SetInt("height", surfaceHeight);
    _hudProgram->SetFloat4("texRect", 0.0f, 0.0f, 1.0f, 1.0f);
    gpu::DrawState state;
    state.viewport = {0, 0, surfaceWidth, surfaceHeight};
    state.blend = gpu::Blend::Premultiplied;
    _gpu->Draw(*_hudProgram, target, state);
}

#ifdef UNREAL_VULKAN
bool DeviceScreenGLWindow::createVulkanDevice()
{
    std::string error;
    _vkInstance = gpu::VulkanDevice::CreateInstance(true, error);
    if (_vkInstance)
        _vkSurface = gpu::CreateWindowSurface(_vkInstance, this, error);
    std::unique_ptr<gpu::VulkanDevice> device;
    if (_vkSurface)
    {
        gpu::VulkanDevice::Setup setup;
        setup.instance = _vkInstance;
        setup.surface = _vkSurface;
        setup.optionalExtensions = {VK_KHR_PRESENT_ID_EXTENSION_NAME, VK_KHR_PRESENT_WAIT_EXTENSION_NAME,
                                    VK_GOOGLE_DISPLAY_TIMING_EXTENSION_NAME};
#ifdef VK_EXT_present_timing
        for (const char* name : {VK_EXT_PRESENT_TIMING_EXTENSION_NAME, VK_KHR_PRESENT_ID_2_EXTENSION_NAME,
                                 VK_KHR_CALIBRATED_TIMESTAMPS_EXTENSION_NAME})
            setup.optionalExtensions.push_back(name);
#endif
        device = gpu::VulkanDevice::Create(setup, error);
    }
    if (!device)
    {
        const char* step = !_vkInstance ? "instance" : !_vkSurface ? "window surface" : "device";
        qWarning().noquote() << "DeviceScreenGLWindow: the Vulkan renderer is not available (" << step << "):"
                             << QString::fromStdString(error.empty() ? std::string("no reason given") : error)
                             << "- GL draws";
        if (_vkSurface)
            vkDestroySurfaceKHR(_vkInstance, _vkSurface, nullptr);
        _vkSurface = VK_NULL_HANDLE;
        if (_vkInstance)
            vkDestroyInstance(_vkInstance, nullptr);
        _vkInstance = VK_NULL_HANDLE;
        return false;
    }
    qInfo().noquote() << "DeviceScreenGLWindow: the Vulkan renderer," << QString::fromStdString(device->Describe());
    _gpu = std::move(device);
    return true;
}
#else
bool DeviceScreenGLWindow::createVulkanDevice()
{
    return false;
}
#endif

void DeviceScreenGLWindow::dropVulkan(const std::string& why)
{
    qWarning().noquote() << "DeviceScreenGLWindow: the Vulkan renderer" << QString::fromStdString(why)
                         << "- GL draws from now on";
    makeCurrent();  // the GL objects (smooth motion, timers) are in the context
    releaseGLObjects();
    _vulkan = false;
    _glInitialized = false;
    _glSize = QSize();
    _smoothShown = {};
    _uploadedSerial = 0;
    for (CrtTables& tables : _crtTables)
        tables.Invalidate();
    update();
}

void DeviceScreenGLWindow::drawQuad(gpu::Program& program, gpu::Texture& target, gpu::Rect viewport, float srcX1,
                                    float srcY1, float srcX2, float srcY2)
{
    program.SetFloat4("texRect", srcX1, srcY1, srcX2, srcY2);
    gpu::DrawState state;
    state.viewport = viewport;
    _gpu->Draw(program, target, state);
}

void DeviceScreenGLWindow::drawComposedCrt(const QRect& picture, gpu::Texture& target, gpu::Rect viewport)
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
    CrtTables& t = crtTables();
    static const CRTProfileParams kPlain = CRTProfileParams::None();
    const CRTProfileParams& params = _crtEnabled ? _crtParams : kPlain;
    const bool rebuilt = t.renderer.setLayout(layout, params);
    if (rebuilt || !t.indexTexture || !t.factorTexture)
    {
        UploadRgba(*_gpu, t.indexTexture, layout.outWidth, layout.outHeight, t.renderer.indexTable().data());
        UploadRgba(*_gpu, t.factorTexture, layout.outWidth, layout.outHeight, t.renderer.factorTable().data());
    }
    if (rebuilt || !t.gradedTexture || t.gradedSerial != _frameSerial)
    {
        t.renderer.grade(_shownFrame->constBits(), static_cast<int>(_shownFrame->bytesPerLine()), params);
        UploadRgba(*_gpu, t.gradedTexture, layout.frameWidth, layout.frameHeight, t.renderer.graded().data());
        t.gradedSerial = _frameSerial;
    }

    if (!t.gradedTexture || !t.indexTexture || !t.factorTexture)
        return;
    gpu::Program& p = *_composeShader;
    p.SetTexture("frame", t.gradedTexture.get());
    p.SetTexture("indexTable", t.indexTexture.get());
    p.SetTexture("factorTable", t.factorTexture.get());
    p.SetFloat2("frameSize", static_cast<float>(layout.frameWidth), static_cast<float>(layout.frameHeight));
    drawQuad(p, target, viewport, 0.0f, 0.0f, 1.0f, 1.0f);
}

void DeviceScreenGLWindow::drawModelCrt(const QRect& picture, gpu::Texture& target, gpu::Rect viewport)
{
    CrtTables& t = crtTables();
    CRTBeamModel::Layout layout;
    layout.outWidth = picture.width();
    layout.outHeight = picture.height();
    layout.frameWidth = _shownFrame->width();
    layout.frameHeight = _shownFrame->height();
    layout.source = displaySourceRect().toRect().intersected(_shownFrame->rect());
    if (layout.source.isEmpty())
        return;

    // The tables again only for a new size or profile, the frame once per upload
    const bool rebuilt = t.model.setLayout(layout, _crtParams.profile);
    if (rebuilt || !t.modelTablesUploaded)
    {
        t.modelGpu.uploadTables(t.model);
        t.modelTablesUploaded = true;
    }
    if (rebuilt || t.modelSerial != _frameSerial)
    {
        // Not the glow's columns: the shader sums them from the frame itself
        t.model.prepare(_shownFrame->constBits(), static_cast<int>(_shownFrame->bytesPerLine()), false);
        t.modelGpu.uploadFrame(t.model);
        t.modelSerial = _frameSerial;
    }
    t.modelGpu.draw(t.model, target, viewport);
}

void DeviceScreenGLWindow::drawCustomCrt(const QRect& picture, gpu::Texture& target, gpu::Rect viewport)
{
    const QRectF source = displaySourceRect();
    const float texW = static_cast<float>(_texture->Width());
    const float texH = static_cast<float>(_texture->Height());

    gpu::Program& p = *_customShader;
    p.SetTexture("tex", _texture.get());
    p.SetFloat2("texSize", texW, texH);
    p.SetFloat2("outputSize", static_cast<float>(picture.width()), static_cast<float>(picture.height()));
    // The profiles have no phosphor mask: the mask uniforms of the documented interface stay, as "none"
    p.SetInt("maskType", 0);
    p.SetFloat("curvature", _crtParams.curvature);
    p.SetFloat("cornerRadius", _crtParams.cornerRadius);
    p.SetFloat("scanlineWeight", _crtParams.scanlineWeight);
    p.SetFloat("maskStrength", 0.0f);
    p.SetFloat("maskDotPitch", 0.0f);
    p.SetFloat("bloomStrength", _crtParams.bloomStrength);
    p.SetFloat("saturation", _crtParams.saturation);
    p.SetFloat("brightness", _crtParams.brightness);
    p.SetFloat("contrast", _crtParams.contrast);
    p.SetFloat("gamma", _crtParams.gamma);
    drawQuad(p, target, viewport, static_cast<float>(source.left()) / texW, static_cast<float>(source.top()) / texH,
             static_cast<float>(source.right()) / texW, static_cast<float>(source.bottom()) / texH);
}

void DeviceScreenGLWindow::drawSlangPreset(const QRect& picture, gpu::Texture& target, gpu::Rect viewport)
{
    // The passes render into their own textures; the last one is copied 1:1 into the picture of the target (the
    // window, the presenter's frame or renderWindowImage's framebuffer object)
    const QRect source = displaySourceRect().toRect().intersected(QRect(0, 0, _texture->Width(), _texture->Height()));
    if (source.isEmpty())
        return;
    _slangRunner->render(*_texture, source, picture.size());
    _slangRunner->drawTo(target, QRect(viewport.x, viewport.y, viewport.width, viewport.height));
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

bool DeviceScreenGLWindow::overlayTakesPointer(QMouseEvent* event)
{
    const bool taken = _overlayPointer && _overlayPointer(event->type(), event->position(), width(), height());
    if (taken && !_overlayHidCursor)
    {
        setCursor(Qt::BlankCursor);
        _overlayHidCursor = true;
    }
    else if (!taken && _overlayHidCursor)
    {
        unsetCursor();
        _overlayHidCursor = false;
    }
    return taken;
}

void DeviceScreenGLWindow::mousePressEvent(QMouseEvent* event)
{
    if (overlayTakesPointer(event))
    {
        event->accept();
        return;
    }
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
    if (overlayTakesPointer(event))
    {
        event->accept();
        return;
    }
    if (_mouseCapture && _mouseCapture->handleMouseRelease(event))
        event->accept();
    else
        QWindow::mouseReleaseEvent(event);
}

void DeviceScreenGLWindow::mouseMoveEvent(QMouseEvent* event)
{
    if (overlayTakesPointer(event))
    {
        event->accept();
        return;
    }
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
    if (_pictureView != PictureView::Mode::Full && !_devicePixelsRect.isEmpty())
    {
        // The frame in hand is the texture's: _devicePixelsRect is its size (init), the geometry came with its pixels
        const QSize frame = _shownFrame ? _shownFrame->size() : _devicePixelsRect.size().toSize();
        const PictureView::Cut cut = PictureView::CutOf(_frameGeometry, static_cast<uint16_t>(frame.width()),
                                                        static_cast<uint16_t>(frame.height()), _pictureView);
        return QRectF(cut.source.x, cut.source.y, cut.source.width, cut.source.height);
    }
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
                const DropPaths::Result dragged = DropPaths::FromMime(dragEvent->mimeData());
                emit dragEntered(dragged.paths.isEmpty() ? QString() : dragged.paths.first(), dragged.refusal);
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
                const DropPaths::Result dropped = DropPaths::FromMime(mimeData);
                if (!dropped.paths.isEmpty() || !dropped.refusal.isEmpty())
                {
                    qDebug() << "DeviceScreenGLWindow: files dropped:" << dropped.paths << dropped.refusal;
                    emit filesDropped(dropped.paths, dropped.refusal);
                }
                // A refusal stays up a moment after the drop (MainWindow); the rest of the drag's marks go
                if (dropped.refusal.isEmpty())
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
            settleSmoothGuard();
            if (isExposed())
            {
                if (syncWatched() && !_syncTimer->isActive() && !_isShuttingDown)
                    _syncTimer->start();
                if (_native)
                    renderNative(true);
                else
                {
                    _glPaintAsked = true;  // the system may have dropped what the window showed
                    renderGL();
                }
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

#include "moc_devicescreenglwindow.cpp"
