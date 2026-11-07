#include "platform/nativepresenter.h"

#ifdef __linux__

#include <QDebug>
#include <QGuiApplication>
#include <QOpenGLContext>
#include <QOpenGLExtraFunctions>
#include <QOpenGLFunctions>
#include <QScreen>
#include <QWindow>

#include <dlfcn.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "common/timehelper.h"
#include "emulator/video/screenbeat.h"
#include "platform/linux/x11display.h"
#include "platform/linuxpresent.h"
#include "platform/presentlead.h"
#include "widgets/framesource.h"

namespace
{
// GLX / EGL entry points, resolved at run time from the libraries Qt's platform plugin loaded (no link dependency)
using GlxGetCurrentContextFn = void* (*)();
using GlxGetCurrentDisplayFn = void* (*)();
using GlxGetCurrentDrawableFn = unsigned long (*)();
using GlxQueryExtensionsStringFn = const char* (*)(void*, int);
using XDefaultScreenFn = int (*)(void*);
using GlxGetSyncValuesFn = int (*)(void*, unsigned long, int64_t*, int64_t*, int64_t*);
using GlxWaitForSbcFn = int (*)(void*, unsigned long, int64_t, int64_t*, int64_t*, int64_t*);
using GlxSwapIntervalExtFn = void (*)(void*, unsigned long, int);
using GlxSwapIntervalMesaFn = int (*)(unsigned int);
using EglGetCurrentDisplayFn = void* (*)();
using EglSwapIntervalFn = unsigned (*)(void*, int);

/// A pointer from dlsym / getProcAddress to the function's type (a cast warns under gcc's -Wcast-function-type)
template <typename Fn, typename Ptr>
Fn As(Ptr pointer)
{
    Fn fn;
    static_assert(sizeof fn == sizeof pointer);
    std::memcpy(&fn, &pointer, sizeof fn);
    return fn;
}

/// A function of an already loaded library (RTLD_NOLOAD: never loads one), nullptr when it is not there
template <typename Fn>
Fn Loaded(const char* library, const char* name)
{
    void* handle = dlopen(library, RTLD_NOW | RTLD_NOLOAD);
    if (!handle)
        return nullptr;
    Fn fn = As<Fn>(dlsym(handle, name));
    dlclose(handle);  // still loaded: Qt's plugin holds it
    return fn;
}

/// NativePresenter::PresentFeedback: the first presenter's answer
std::atomic<int> gPresentFeedback{-1};

/// Textures: one being drawn, one waiting for its time, one being copied
constexpr size_t kSlots = 3;
/// The blit and the swap call, started this much before the frame's time
constexpr int64_t kSwapCostNs = 300'000;
/// The adaptive lead's first margin: the wake's slack, the draw, the blit (the present thread swaps at the time)
constexpr int64_t kLeadStartNs = 8'000'000;
/// Present intervals kept for the VRR check
constexpr size_t kIntervalWindow = 120;
/// The window system asked again (VRR capable, fullscreen) at most this often
constexpr int64_t kDecideEveryNs = 1'000'000'000;

/// The GL draw context's view of a slot (GUI thread) and the present context's (its own framebuffer: framebuffers are
/// not shared between contexts, the texture is)
struct Slot
{
    GLuint texture = 0;
    GLuint framebuffer = 0;
    int width = 0;
    int height = 0;
    uint32_t generation = 0;   ///< counts reallocations: the present thread attaches the texture again
    bool queued = false;       ///< handed to the present thread, not copied yet (_queueMutex)
    GLsync released = nullptr;  ///< the present thread's copy done (_queueMutex); BeginFrame waits for it
    GLuint presentFramebuffer = 0;  ///< present thread
    uint32_t presentGeneration = 0;  ///< present thread
};

struct Item
{
    size_t slot = 0;
    int64_t atNs = 0;
    int64_t handedNs = 0;
    uint64_t number = 0;
    GLsync drawn = nullptr;  ///< the picture's draw done (the GUI thread's fence)
    NativePresenter::TraceInfo trace;
};

/// The timed GL swap (frame presenter design, sections 4.1 and 6.2, Linux): GL draws each picture into a texture, a
/// present thread with a context of its own on the window (shared with the drawing one) waits until the frame's time,
/// copies it into the back buffer and swaps: swap interval 0 under VRR (the display starts a refresh when the frame
/// comes), interval 1 on a fixed display (the first refresh after the time). GLX_OML_sync_control (Mesa on X11)
/// gives each swap's screen time (glXWaitForSbcOML's UST) and the vblank grid; without it (Wayland, EGL) nothing is
/// measured and the window uses this presenter only for policy vrr (linuxpresent::UseTimedSwap)
class GlTimedPresenter final : public NativePresenter
{
public:
    explicit GlTimedPresenter(QWindow* window) : _window(window) {}

    ~GlTimedPresenter() override
    {
        {
            std::lock_guard<std::mutex> lock(_queueMutex);
            _stop = true;
        }
        _queueCv.notify_all();
        _wake.Notify();
        if (_thread.joinable())
            _thread.join();
        // The draw context's objects (current: the window's releaseGLObjects / renderNative)
        QOpenGLContext* gl = QOpenGLContext::currentContext();
        if (!gl)
            return;
        QOpenGLExtraFunctions* f = gl->extraFunctions();
        for (Slot& s : _slots)
        {
            if (s.released)
                f->glDeleteSync(s.released);
            if (s.framebuffer)
                f->glDeleteFramebuffers(1, &s.framebuffer);
            if (s.texture)
                f->glDeleteTextures(1, &s.texture);
        }
        for (Item& item : _queue)
            if (item.drawn)
                f->glDeleteSync(item.drawn);
    }

    /// GUI thread, the window's context current: the present thread made its context and is ready
    bool Init()
    {
        _drawContext = QOpenGLContext::currentContext();
        if (!_drawContext)
        {
            _error = "no GL context current";
            return false;
        }
        if (!QOpenGLContext::supportsThreadedOpenGL())
        {
            _error = "the platform has no threaded OpenGL";
            return false;
        }
        const char* vendor = reinterpret_cast<const char*>(_drawContext->functions()->glGetString(GL_VENDOR));
        _nvidia = vendor && std::strstr(vendor, "NVIDIA");
        Decide(SteadyNowNs());
        _thread = std::thread([this] { RunPresent(); });
        std::unique_lock<std::mutex> lock(_queueMutex);
        if (!_readyCv.wait_for(lock, std::chrono::seconds(2), [&] { return _ready || _failed; }) || _failed)
        {
            if (!_failed)
                _error = "the present thread did not start";
            lock.unlock();
            {
                std::lock_guard<std::mutex> stop(_queueMutex);
                _stop = true;
            }
            _queueCv.notify_all();
            _thread.join();
            return false;
        }
        return true;
    }

    const std::string& Error() const { return _error; }

    /// The present thread's screen times come from GLX_OML_sync_control
    bool Feedback() const { return _oml.load(); }

    unsigned BeginFrame(int width, int height) override
    {
        QOpenGLContext* gl = QOpenGLContext::currentContext();
        if (width <= 0 || height <= 0 || !gl)
            return 0;
        if (Lost())
            return Failed("the present thread stopped: " + _error);
        const int64_t now = SteadyNowNs();
        if (now - _decidedNs > kDecideEveryNs)
            Decide(now);
        QOpenGLExtraFunctions* f = gl->extraFunctions();

        size_t index = (_current + 1) % kSlots;
        GLsync released = nullptr;
        {
            std::unique_lock<std::mutex> lock(_queueMutex);
            const auto found = [&] {
                for (size_t i = 0; i < kSlots; i++)
                    if (!_slots[(_current + 1 + i) % kSlots].queued)
                    {
                        index = (_current + 1 + i) % kSlots;
                        return true;
                    }
                return false;
            };
            if (!_freeCv.wait_for(lock, std::chrono::milliseconds(100), [&] { return found() || _failed; }) || _failed)
                return Failed("no free slot (" + std::to_string(_queue.size()) + " queued for the present thread)");
            released = _slots[index].released;
            _slots[index].released = nullptr;
        }
        Slot& slot = _slots[index];
        // The present thread's copy of what the slot held before is done on the GPU before anything draws over it
        if (released)
        {
            f->glWaitSync(released, 0, GL_TIMEOUT_IGNORED);
            f->glDeleteSync(released);
        }
        if (!slot.texture || slot.width != width || slot.height != height)
        {
            if (!slot.texture)
                f->glGenTextures(1, &slot.texture);
            f->glBindTexture(GL_TEXTURE_2D, slot.texture);
            f->glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
            f->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
            f->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
            f->glBindTexture(GL_TEXTURE_2D, 0);
            if (!slot.framebuffer)
                f->glGenFramebuffers(1, &slot.framebuffer);
            f->glBindFramebuffer(GL_FRAMEBUFFER, slot.framebuffer);
            f->glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, slot.texture, 0);
            if (f->glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
                return Failed("the slot's framebuffer incomplete (" + std::to_string(width) + "x" +
                              std::to_string(height) + ")");
            slot.width = width;
            slot.height = height;
            slot.generation++;
        }
        f->glBindFramebuffer(GL_FRAMEBUFFER, slot.framebuffer);
        _current = index;
        _drawing = true;
        return slot.framebuffer;
    }

    void PresentFrame(int64_t atNs, const TraceInfo& trace) override
    {
        if (!_drawing)
            return;
        _drawing = false;
        QOpenGLExtraFunctions* f = QOpenGLContext::currentContext()->extraFunctions();
        GLsync drawn = f->glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
        f->glFlush();  // the fence reaches the GPU: the present thread's wait sees it
        uint64_t number = 0;
        {
            std::lock_guard<std::mutex> lock(_statsMutex);
            number = ++_presented;
        }
        {
            std::lock_guard<std::mutex> lock(_queueMutex);
            _slots[_current].queued = true;
            _queue.push_back({_current, atNs, SteadyNowNs(), number, drawn, trace});
        }
        _queueCv.notify_one();
        _wake.Notify();
    }

    void SetPolicy(PresentPolicy policy) override
    {
        _policy.store(static_cast<uint8_t>(policy), std::memory_order_relaxed);
        std::lock_guard<std::mutex> lock(_statsMutex);
        _engaged = true;
        _vrrNote.clear();
        ApplyMode();
        _decidedNs = 0;  // _VARIABLE_REFRESH follows at the next frame
    }

    bool IsVisible() const override
    {
        const bool exposed = _window->isExposed();
        _exposed.store(exposed, std::memory_order_relaxed);
        return exposed;
    }

    int64_t LeadNs(int64_t refreshPeriodNs) const override { return _lead.Margin(refreshPeriodNs); }

    bool RefreshGrid(int64_t& refreshNs, int64_t& periodNs) const override
    {
        const int64_t last = _lastVBlankNs.load(std::memory_order_relaxed);
        if (last == 0 || _vrr.load(std::memory_order_relaxed))
            return false;
        int64_t period = _vblankPeriodNs.load(std::memory_order_relaxed);
        const double hz = _refreshHz.load(std::memory_order_relaxed);
        if (period <= 0 && hz > 0.0)
            period = static_cast<int64_t>(1e9 / hz);
        if (period <= 0)
            return false;
        refreshNs = last;
        periodNs = period;
        return true;
    }

    std::string LastFailure() const override
    {
        std::lock_guard<std::mutex> lock(_statsMutex);
        return _failure;
    }

    bool Lost() const override { return _failed.load(); }

    Stats GetStats() const override
    {
        Stats s;
        std::vector<double> errors, waits, drawableWaits;
        {
            std::lock_guard<std::mutex> lock(_statsMutex);
            errors = _errors.Values();
            waits = _gpuWaits.Values();
            drawableWaits = _swapTimes.Values();
            s.presented = _presented;
            s.shown = _shown;
            s.dropped = _dropped;
            const ScreenBeat::Stats beat = _beat.GetStats();
            s.beatFrames = beat.frames;
            s.offBeat = beat.offBeat;
            s.soundOffsetP50 = beat.offsetP50Ms;
            s.soundOffsetP99 = beat.offsetP99Ms;
            s.vrrSupport = linuxpresent::VrrSupportName(_decision.support);
            s.vrrReason = _vrrNote.empty() ? _decision.reason
                                           : (_decision.reason.empty() ? _vrrNote : _decision.reason + "; " + _vrrNote);
        }
        s.late = _lead.Late();
        s.leadMs = static_cast<double>(_lead.MarginNs()) / 1e6;
        presentstats::Fill(s, std::move(errors), std::move(waits), std::move(drawableWaits));
        const bool vrr = _vrr.load(std::memory_order_relaxed);
        s.maxRefreshHz = _refreshHz.load(std::memory_order_relaxed);
        s.minRefreshHz = vrr ? 0.0 : s.maxRefreshHz;
        s.visible = _exposed.load(std::memory_order_relaxed);
        s.vrr = vrr;
        return s;
    }

    std::string Describe() const override
    {
        const Stats s = GetStats();
        const bool vrr = s.vrr;
        char line[640];
        std::snprintf(line, sizeof line,
                      "GL timed swap %s (policy %s, VRR %s%s%s; %s; feedback %s): %llu presented, %llu shown, %llu "
                      "dropped, %llu a refresh late (lead %.1f ms); error p50 %.2f p99 %.2f max %.2f ms; GL draw wait "
                      "p50 %.2f max %.2f ms; swap p50 %.2f max %.2f ms; display %.2f Hz, vblank %.2f ms%s%s",
                      vrr ? "VRR, interval 0 at the time" : "vsync, interval 1 at the time",
                      PresentPolicyName(Policy()), s.vrrSupport.c_str(), s.vrrReason.empty() ? "" : ": ",
                      s.vrrReason.c_str(), _api.c_str(),
                      _oml ? "GLX_OML_sync_control" : ("none (" + _feedbackNote + ")").c_str(),
                      static_cast<unsigned long long>(s.presented), static_cast<unsigned long long>(s.shown),
                      static_cast<unsigned long long>(s.dropped), static_cast<unsigned long long>(s.late), s.leadMs,
                      s.errorP50, s.errorP99, s.errorMax, s.gpuWaitP50, s.gpuWaitMax, s.drawableWaitP50,
                      s.drawableWaitMax, s.maxRefreshHz,
                      static_cast<double>(_vblankPeriodNs.load(std::memory_order_relaxed)) / 1e6,
                      s.visible ? "" : "; occluded", Lost() ? "; stopped" : "");
        return line;
    }

private:
    PresentPolicy Policy() const { return static_cast<PresentPolicy>(_policy.load(std::memory_order_relaxed)); }

    /// _statsMutex held: the swap mode the policy and the decision give
    void ApplyMode()
    {
        const linuxpresent::SwapMode mode = linuxpresent::PickSwapMode(Policy(), _decision.support, _engaged);
        _vrr.store(mode.vrr, std::memory_order_relaxed);
        _interval.store(mode.interval, std::memory_order_relaxed);
    }

    /// GUI thread: what the window system says now (the display, VRR capable, fullscreen); _VARIABLE_REFRESH on the
    /// window and its top level as the policy wants
    void Decide(int64_t now)
    {
        _decidedNs = now;
        const QScreen* screen = _window->screen();
        _refreshHz.store(screen ? screen->refreshRate() : 0.0, std::memory_order_relaxed);
        // Mesa sets it on the GL window itself (driconf adaptive_sync); the top level is what a compositor and
        // xf86-video-amdgpu look at for a fullscreen client
        const bool optIn = Policy() != PresentPolicy::Vsync;
        if (optIn != _optedIn || !_optInSet)
        {
            _optedIn = optIn;
            _optInSet = true;
            x11::OptInVariableRefresh(_window, optIn);
        }
        const linuxpresent::VrrDecision decision = x11::DecideWindowVrr(_window, _nvidia);
        std::lock_guard<std::mutex> lock(_statsMutex);
        if (decision.support != _decision.support)
        {
            _engaged = true;
            _vrrNote.clear();
        }
        _decision = decision;
        ApplyMode();
    }

    unsigned Failed(const std::string& why)
    {
        std::lock_guard<std::mutex> lock(_statsMutex);
        _failure = why;
        return 0;
    }

    /// Present thread: the context on the window, GLX or EGL found, the feedback checked
    bool StartContext(QOpenGLContext& context)
    {
        QSurfaceFormat format = _window->format();
        format.setSwapInterval(0);  // Qt's own frame throttling (Wayland) off: the thread swaps at the time
        context.setFormat(format);
        context.setShareContext(_drawContext);
        if (!context.create())
        {
            _error = "the present context was not made";
            return false;
        }
        if (!context.makeCurrent(_window))
        {
            _error = "the present context is not current on the window";
            return false;
        }
        auto glxContext = Loaded<GlxGetCurrentContextFn>("libGL.so.1", "glXGetCurrentContext");
        if (!glxContext)
            glxContext = Loaded<GlxGetCurrentContextFn>("libGLX.so.0", "glXGetCurrentContext");
        if (glxContext && glxContext())
        {
            auto display = Loaded<GlxGetCurrentDisplayFn>("libGL.so.1", "glXGetCurrentDisplay");
            auto drawable = Loaded<GlxGetCurrentDrawableFn>("libGL.so.1", "glXGetCurrentDrawable");
            auto extensions = Loaded<GlxQueryExtensionsStringFn>("libGL.so.1", "glXQueryExtensionsString");
            auto defaultScreen = Loaded<XDefaultScreenFn>("libX11.so.6", "XDefaultScreen");
            _glxDisplay = display ? display() : nullptr;
            _glxDrawable = drawable ? drawable() : 0;
            const char* ext = _glxDisplay && extensions
                                  ? extensions(_glxDisplay, defaultScreen ? defaultScreen(_glxDisplay) : 0)
                                  : nullptr;
            const std::string list = ext ? ext : "";
            _api = "GLX";
            if (list.find("GLX_EXT_swap_control") != std::string::npos)
                _swapIntervalExt = As<GlxSwapIntervalExtFn>(context.getProcAddress("glXSwapIntervalEXT"));
            if (!_swapIntervalExt && list.find("GLX_MESA_swap_control") != std::string::npos)
                _swapIntervalMesa = As<GlxSwapIntervalMesaFn>(context.getProcAddress("glXSwapIntervalMESA"));
            if (list.find("GLX_OML_sync_control") != std::string::npos)
            {
                _getSyncValues = As<GlxGetSyncValuesFn>(context.getProcAddress("glXGetSyncValuesOML"));
                _waitForSbc = As<GlxWaitForSbcFn>(context.getProcAddress("glXWaitForSbcOML"));
            }
            int64_t ust = 0, msc = 0, sbc = 0;
            if (!_glxDisplay || !_glxDrawable)
                _feedbackNote = "no GLX drawable current";
            else if (!_getSyncValues || !_waitForSbc)
                _feedbackNote = "the GLX connection lacks GLX_OML_sync_control (Xvfb, NVIDIA, a remote display)";
            else if (!_getSyncValues(_glxDisplay, _glxDrawable, &ust, &msc, &sbc))
                _feedbackNote = "glXGetSyncValuesOML failed";
            else if (!linuxpresent::UstOnSteadyClock(ust * 1000, SteadyNowNs()))
                _feedbackNote = "the driver's UST is not CLOCK_MONOTONIC";
            else
                _oml = true;
        }
        else
        {
            _api = "EGL";
            _feedbackNote = "EGL has no presentation feedback here (Wayland: wp_presentation needs the Vulkan presenter)";
            _eglDisplay = Loaded<EglGetCurrentDisplayFn>("libEGL.so.1", "eglGetCurrentDisplay");
            _eglSwapInterval = Loaded<EglSwapIntervalFn>("libEGL.so.1", "eglSwapInterval");
        }
        _setInterval = -1;
        return true;
    }

    void SetInterval(int interval)
    {
        if (interval == _setInterval)
            return;
        _setInterval = interval;
        if (_swapIntervalExt)
            _swapIntervalExt(_glxDisplay, _glxDrawable, interval);
        else if (_swapIntervalMesa)
            _swapIntervalMesa(static_cast<unsigned>(interval));
        else if (_eglDisplay && _eglSwapInterval)
            _eglSwapInterval(_eglDisplay(), interval);
    }

    void RunPresent()
    {
        QOpenGLContext context;
        const bool started = StartContext(context);
        {
            std::lock_guard<std::mutex> lock(_queueMutex);
            _ready = started;
            if (!started)
                _failed = true;
        }
        _readyCv.notify_all();
        if (!started)
            return;
        QOpenGLExtraFunctions* f = context.extraFunctions();
        int64_t lastAtNs = 0;
        int sinceCheck = 0;
        while (true)
        {
            Item item;
            int64_t nextAtNs = 0;
            {
                std::unique_lock<std::mutex> lock(_queueMutex);
                _queueCv.wait(lock, [&] { return _stop || !_queue.empty(); });
                if (_stop)
                    break;
                item = _queue.front();
                _queue.pop_front();
                nextAtNs = _queue.empty() ? 0 : _queue.front().atNs;
            }
            int64_t wakeNs = 0;
            if (linuxpresent::PlanSwap(item.atNs, nextAtNs, SteadyNowNs(), kSwapCostNs, wakeNs) ==
                linuxpresent::SwapStep::Drop)
            {
                f->glDeleteSync(item.drawn);
                Release(item.slot, nullptr);
                std::lock_guard<std::mutex> lock(_statsMutex);
                _dropped++;
                continue;
            }
            // The picture drawn (the GL wait of the report), then its time
            const int64_t wait0 = SteadyNowNs();
            f->glClientWaitSync(item.drawn, GL_SYNC_FLUSH_COMMANDS_BIT, 100'000'000);
            f->glDeleteSync(item.drawn);
            const double drawWaitMs = static_cast<double>(SteadyNowNs() - wait0) / 1e6;
            if (linuxpresent::PlanSwap(item.atNs, 0, SteadyNowNs(), kSwapCostNs, wakeNs) ==
                linuxpresent::SwapStep::Wait)
                TimeHelper::WaitUntilPrecise(std::chrono::steady_clock::time_point(std::chrono::nanoseconds(wakeNs)),
                                             [this] { return _stop.load(); }, &_wake);
            if (_stop)
            {
                Release(item.slot, nullptr);
                break;
            }
            SetInterval(_interval.load(std::memory_order_relaxed));

            Slot& slot = _slots[item.slot];
            if (!slot.presentFramebuffer || slot.presentGeneration != slot.generation)
            {
                if (!slot.presentFramebuffer)
                    f->glGenFramebuffers(1, &slot.presentFramebuffer);
                f->glBindFramebuffer(GL_READ_FRAMEBUFFER, slot.presentFramebuffer);
                f->glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, slot.texture, 0);
                slot.presentGeneration = slot.generation;
            }
            f->glBindFramebuffer(GL_READ_FRAMEBUFFER, slot.presentFramebuffer);
            f->glBindFramebuffer(GL_DRAW_FRAMEBUFFER, context.defaultFramebufferObject());
            f->glBlitFramebuffer(0, 0, slot.width, slot.height, 0, 0, slot.width, slot.height, GL_COLOR_BUFFER_BIT,
                                 GL_NEAREST);
            GLsync copied = f->glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
            f->glFlush();
            Release(item.slot, copied);

            const int64_t submitted = SteadyNowNs();
            context.swapBuffers(_window);
            const int64_t swapped = SteadyNowNs();
            int64_t screenNs = 0;
            int64_t ust = 0, msc = 0, sbc = 0;
            // The swap's screen time: the UST of the vblank it was shown at (an occluded window's swaps may never
            // complete: not waited for)
            if (_oml && _exposed.load(std::memory_order_relaxed) &&
                _waitForSbc(_glxDisplay, _glxDrawable, 0, &ust, &msc, &sbc) && ust > 0)
            {
                screenNs = ust * 1000;
                _vblanks.Add(screenNs, msc);
                _lastVBlankNs.store(_vblanks.LastNs(), std::memory_order_relaxed);
                _vblankPeriodNs.store(_vblanks.PeriodNs(), std::memory_order_relaxed);
            }
            Record(item, submitted, swapped, screenNs, drawWaitMs);
            if (item.atNs > 0 && lastAtNs > 0 && item.atNs > lastAtNs)
            {
                _intervals.push_back(static_cast<double>(item.atNs - lastAtNs) / 1e6);
                if (_intervals.size() > kIntervalWindow)
                    _intervals.erase(_intervals.begin());
            }
            if (item.atNs > 0)
                lastAtNs = item.atNs;
            if (++sinceCheck >= 100)
            {
                sinceCheck = 0;
                CheckVrr();
            }
        }
        for (Slot& s : _slots)
            if (s.presentFramebuffer)
                f->glDeleteFramebuffers(1, &s.presentFramebuffer);
        context.doneCurrent();
    }

    /// Present thread: the slot free for the GUI thread again, after `copied` (nullptr: nothing was copied)
    void Release(size_t index, GLsync copied)
    {
        {
            std::lock_guard<std::mutex> lock(_queueMutex);
            _slots[index].queued = false;
            _slots[index].released = copied;
        }
        _freeCv.notify_all();
    }

    void Record(const Item& item, int64_t submitted, int64_t swapped, int64_t screenNs, double drawWaitMs)
    {
        std::lock_guard<std::mutex> lock(_statsMutex);
        _gpuWaits.Add(drawWaitMs);
        _swapTimes.Add(static_cast<double>(swapped - submitted) / 1e6);
        if (screenNs > 0)
        {
            _shown++;
            const int64_t asked = std::max(item.atNs, submitted);
            _errors.Add(static_cast<double>(screenNs - asked) / 1e6);
            const int64_t own = item.atNs > 0 ? item.atNs : asked;
            _lead.Shown(screenNs - own, item.number, std::max(own, item.handedNs) - item.handedNs);
            const double hz = _refreshHz.load(std::memory_order_relaxed);
            _beat.Shown(item.trace.picture, item.trace.targetNs, screenNs,
                        static_cast<int64_t>(1e9 / (hz > 0.0 ? hz : 60.0)));
        }
        _trace.Write(item.trace, item.atNs, submitted, screenNs, 0);
    }

    /// Present thread, policy auto with VRR on: the vblanks stayed on the mode's grid while the frames were off it,
    /// VRR did not engage (a composited window, the driver's VRR off): interval 1 from now on, until the decision
    /// changes (fullscreen, another display)
    void CheckVrr()
    {
        if (!_oml || Policy() != PresentPolicy::Auto || !_vrr.load(std::memory_order_relaxed) || _intervals.size() < 60)
            return;
        const double vblankMs = static_cast<double>(_vblankPeriodNs.load(std::memory_order_relaxed)) / 1e6;
        const double presentMs = presentstats::Percentile(_intervals, 0.50);
        const double hz = _refreshHz.load(std::memory_order_relaxed);
        bool known = false;
        if (linuxpresent::VrrEngaged(vblankMs, presentMs, hz > 0.0 ? 1000.0 / hz : 0.0, known) || !known)
            return;
        char note[160];
        std::snprintf(note, sizeof note, "VRR did not engage: vblanks every %.2f ms while frames come every %.2f ms",
                      vblankMs, presentMs);
        qInfo().noquote() << "GlTimedPresenter:" << note << "- interval 1 from now on";
        std::lock_guard<std::mutex> lock(_statsMutex);
        _engaged = false;
        _vrrNote = note;
        ApplyMode();
    }

    QWindow* _window;
    QOpenGLContext* _drawContext = nullptr;
    std::string _error;
    bool _nvidia = false;

    // GUI thread
    std::array<Slot, kSlots> _slots;
    size_t _current = 0;
    bool _drawing = false;
    int64_t _decidedNs = 0;
    bool _optedIn = false;
    bool _optInSet = false;

    // Present thread (set before _ready)
    std::string _api;
    std::string _feedbackNote;
    void* _glxDisplay = nullptr;
    unsigned long _glxDrawable = 0;
    GlxSwapIntervalExtFn _swapIntervalExt = nullptr;
    GlxSwapIntervalMesaFn _swapIntervalMesa = nullptr;
    GlxGetSyncValuesFn _getSyncValues = nullptr;
    GlxWaitForSbcFn _waitForSbc = nullptr;
    EglGetCurrentDisplayFn _eglDisplay = nullptr;
    EglSwapIntervalFn _eglSwapInterval = nullptr;
    std::atomic<bool> _oml{false};
    int _setInterval = -1;
    linuxpresent::VBlankClock _vblanks;
    std::vector<double> _intervals;

    std::atomic<uint8_t> _policy{0};
    std::atomic<bool> _vrr{false};
    std::atomic<int> _interval{1};
    std::atomic<double> _refreshHz{0.0};
    std::atomic<int64_t> _lastVBlankNs{0};
    std::atomic<int64_t> _vblankPeriodNs{0};
    mutable std::atomic<bool> _exposed{true};
    mutable PresentLead _lead{kLeadStartNs};

    std::mutex _queueMutex;
    std::condition_variable _queueCv;
    std::condition_variable _freeCv;
    std::condition_variable _readyCv;
    std::deque<Item> _queue;
    bool _ready = false;
    std::atomic<bool> _failed{false};
    std::atomic<bool> _stop{false};
    WakeSignal _wake;
    std::thread _thread;

    mutable std::mutex _statsMutex;
    linuxpresent::VrrDecision _decision;
    bool _engaged = true;
    std::string _vrrNote;
    std::string _failure;
    presentstats::Ring _errors;
    presentstats::Ring _gpuWaits;
    presentstats::Ring _swapTimes;
    ScreenBeat _beat;
    uint64_t _presented = 0;
    uint64_t _shown = 0;
    uint64_t _dropped = 0;
    presentstats::Trace _trace;
};
}  // namespace

std::unique_ptr<NativePresenter> NativePresenter::Create(void* windowHandle)
{
    auto presenter = std::make_unique<GlTimedPresenter>(static_cast<QWindow*>(windowHandle));
    if (!presenter->Init())
    {
        qWarning().noquote() << "GlTimedPresenter:" << QString::fromStdString(presenter->Error());
        return nullptr;
    }
    gPresentFeedback.store(presenter->Feedback() ? 1 : 0);
    return presenter;
}

int NativePresenter::PresentFeedback()
{
    return gPresentFeedback.load();
}

#endif  // __linux__
