// The native presenter on Windows (nativepresenter.h; frame presenter design, sections 4.1, 6.2 and 6.3; POC 037
// measured the DXGI side).
//
// - GL draws into one of three D3D11 textures shared through WGL_NV_DX_interop2 (lock, draw, unlock: the unlock is the
//   GL -> D3D synchronisation).
// - A presenter thread copies the texture into the back buffer of a flip-discard swap chain (rows turned over), waits
//   with a high-resolution timer until the frame's audio time and presents: Present(0, ALLOW_TEARING) for VRR, a
//   G-Sync / FreeSync display then starts the refresh at the present; Present(0) without tearing on a fixed display:
//   the newest frame presented before a vblank shows there, a frame still waiting is replaced (b-vsync). Present(1)
//   would queue each frame for a refresh of its own: frames faster than the refresh pile up a refresh each.
// - The screen time is the first kernel vblank after the present (D3DKMTWaitForVerticalBlankEvent on a thread):
//   DXGI's frame statistics are all zeros for a composed window. Under VRR the vblank comes after the scan-out (1 / the
//   panel's top rate), which is taken off. The same vblanks tell VRR from a fixed display: a VRR panel's vblanks
//   follow the presents, a fixed one's stay on its grid; the presenter then turns to Present(0) without tearing.

#include "platform/nativepresenter.h"

#ifdef _WIN32

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <d3d11_4.h>
#include <d3dcompiler.h>
#include <dxgi1_5.h>
#include <windows.h>
#include <wrl/client.h>

#include <QDebug>
#include <QOpenGLContext>
#include <QOpenGLFunctions>
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
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
#include "platform/presentlead.h"
#include "widgets/framesource.h"

using Microsoft::WRL::ComPtr;

namespace
{
// WGL_NV_DX_interop / WGL_NV_DX_interop2 (wglext.h is not on every toolchain)
constexpr GLenum kWglAccessWriteDiscardNV = 0x0002;
using DXOpenDeviceFn = HANDLE(WINAPI*)(void* dxDevice);
using DXCloseDeviceFn = BOOL(WINAPI*)(HANDLE device);
using DXRegisterObjectFn = HANDLE(WINAPI*)(HANDLE device, void* dxObject, GLuint name, GLenum type, GLenum access);
using DXUnregisterObjectFn = BOOL(WINAPI*)(HANDLE device, HANDLE object);
using DXLockObjectsFn = BOOL(WINAPI*)(HANDLE device, GLint count, HANDLE* objects);
using GetExtensionsStringFn = const char*(WINAPI*)(HDC dc);

// d3dkmthk.h is a WDK header; the vblank wait needs only these
using D3dkmtHandle = UINT;
struct D3dkmtOpenAdapterFromHdc
{
    HDC hDc;
    D3dkmtHandle hAdapter;
    LUID AdapterLuid;
    UINT VidPnSourceId;
};
struct D3dkmtWaitForVerticalBlankEvent
{
    D3dkmtHandle hAdapter;
    D3dkmtHandle hDevice;
    UINT VidPnSourceId;
};
struct D3dkmtCloseAdapter
{
    D3dkmtHandle hAdapter;
};
using OpenAdapterFn = LONG(APIENTRY*)(D3dkmtOpenAdapterFromHdc*);
using WaitVBlankFn = LONG(APIENTRY*)(const D3dkmtWaitForVerticalBlankEvent*);
using CloseAdapterFn = LONG(APIENTRY*)(const D3dkmtCloseAdapter*);

/// The back buffer from the shared texture, rows turned over (GL counts them from the bottom). Sizes match: loaded,
/// not sampled, so the copy is exact
const char* kHlsl = R"(
Texture2D<float4> t : register(t0);
float4 vs(uint id : SV_VertexID) : SV_Position {
    float2 p = float2((id << 1) & 2, id & 2);
    return float4(p * float2(2, -2) + float2(-1, 1), 0, 1);
}
float4 ps(float4 pos : SV_Position) : SV_Target {
    uint w, h;
    t.GetDimensions(w, h);
    uint2 xy = uint2(pos.xy);
    if (xy.x >= w || xy.y >= h)
        return float4(0, 0, 0, 1);
    return float4(t.Load(int3(xy.x, h - 1 - xy.y, 0)).rgb, 1);
}
)";

/// GetProcAddress's FARPROC to the real type (a cast warns under gcc's -Wcast-function-type)
template <typename Fn>
Fn FromProc(FARPROC proc)
{
    Fn fn;
    static_assert(sizeof fn == sizeof proc);
    std::memcpy(&fn, &proc, sizeof fn);
    return fn;
}

/// Shared textures: one being drawn, one waiting for its time, one being copied
constexpr size_t kSlots = 3;
/// The Present call itself, started this much before the frame's time
constexpr int64_t kPresentCostNs = 300'000;
/// Vblank intervals kept for the VRR check
constexpr size_t kVBlankWindow = 120;
/// The vblank thread publishes their median for RefreshGrid every this many vblanks
constexpr int kVBlankPeriodEvery = 16;
/// The presenter's destructor waits this long for the vblank thread, then leaves it (blocked in the kernel's wait
/// while no vblank comes: a monitor asleep or gone); it ends at the next vblank
constexpr int kVBlankJoinMs = 200;
/// The adaptive lead's first margin (LeadNs): the wake's slack (~1.5 ms), the draw and the copy into the back buffer
constexpr int64_t kLeadStartNs = 8'000'000;

/// The refresh rate of the monitor the window is on (Hz, 0 = unknown)
double MonitorRefreshHz(HWND hwnd)
{
    MONITORINFOEXW mi{};
    mi.cbSize = sizeof mi;
    if (!GetMonitorInfoW(MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST), &mi))
        return 0.0;
    DEVMODEW dm{};
    dm.dmSize = sizeof dm;
    if (!EnumDisplaySettingsW(mi.szDevice, ENUM_CURRENT_SETTINGS, &dm) || dm.dmDisplayFrequency <= 1)
        return 0.0;
    return static_cast<double>(dm.dmDisplayFrequency);
}

/// A present's HRESULT that means the device is gone: only a new device presents again
bool DeviceGone(HRESULT hr)
{
    return hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET || hr == DXGI_ERROR_DEVICE_HUNG ||
           hr == DXGI_ERROR_DRIVER_INTERNAL_ERROR;
}

/// A present handed to the presenter thread, then waiting for its vblank
struct Pending
{
    int64_t submittedNs = 0;
    int64_t askedNs = 0;
    int64_t atNs = 0;
    int64_t handedNs = 0;  // PresentFrame handed it over (the lead's "ahead")
    uint64_t number = 0;   // its count from 1 (PresentLead)
    int64_t drawableWaitNs = 0;
    NativePresenter::TraceInfo trace;
};

/// What the vblank thread touches, shared with it: the kernel's vblank wait has no timeout, so a presenter going while
/// no vblank comes (a monitor asleep or gone) leaves the thread blocked there and goes; the thread ends at the next
/// vblank. The presenter's statistics live here too, under statsMutex
struct Feedback
{
    explicit Feedback(HWND window) : hwnd(window) {}

    const HWND hwnd;
    std::atomic<bool> stop{false};
    std::mutex doneMutex;
    std::condition_variable doneCv;
    bool done = false;  // the vblank thread ended (doneMutex)

    std::atomic<double> refreshHz{0.0};
    std::atomic<bool> panelFollows{false};   // the vblanks follow the presents (CheckVrr; at first: tearing supported)
    std::atomic<int64_t> lastVBlankNs{0};    // the newest vblank (RefreshGrid)
    std::atomic<int64_t> vblankPeriodNs{0};  // the vblank intervals' median (RefreshGrid; 0: too few yet)
    PresentLead lead{kLeadStartNs};

    mutable std::mutex statsMutex;
    std::deque<Pending> pending;
    presentstats::Ring errors;
    presentstats::Ring gpuWaits;
    presentstats::Ring drawableWaits;
    ScreenBeat beat;
    std::vector<double> vblankIntervals;
    std::vector<double> presentIntervals;
    std::string vblankError;
    std::string vrrNote;
    std::string failure;  // the last BeginFrame failure
    uint64_t presented = 0;
    uint64_t shown = 0;
    uint64_t dropped = 0;
    presentstats::Trace trace;  // UNREAL_PRESENT_TRACE
};

void SetVBlankError(Feedback& fb, const char* error)
{
    std::lock_guard<std::mutex> lock(fb.statsMutex);
    fb.vblankError = error;
}

/// Vblank thread: the first vblank after each present is its screen time
void RunVBlanks(const std::shared_ptr<Feedback>& shared)
{
    Feedback& fb = *shared;
    // The wake after the vblank event is the measurement's error: the highest priority there is
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);
    HMODULE gdi = GetModuleHandleW(L"gdi32.dll");
    auto open = FromProc<OpenAdapterFn>(GetProcAddress(gdi, "D3DKMTOpenAdapterFromHdc"));
    auto wait = FromProc<WaitVBlankFn>(GetProcAddress(gdi, "D3DKMTWaitForVerticalBlankEvent"));
    auto close = FromProc<CloseAdapterFn>(GetProcAddress(gdi, "D3DKMTCloseAdapter"));
    if (!open || !wait || !close)
    {
        SetVBlankError(fb, "no D3DKMT vblank wait");
        return;
    }
    // The adapter of the window's monitor, opened again when the window moves to another monitor or the wait fails
    // (a display mode change, a session moved between RDP and the console)
    D3dkmtWaitForVerticalBlankEvent w{};
    HMONITOR monitor = nullptr;
    int64_t checkedNs = 0;
    int64_t last = 0;
    int sincePeriod = 0;
    const auto closeAdapter = [&] {
        if (w.hAdapter)
        {
            D3dkmtCloseAdapter ca{w.hAdapter};
            close(&ca);
        }
        w = {};
        monitor = nullptr;
        last = 0;
    };
    const auto fail = [&](const char* what, LONG status) {
        char text[96];
        std::snprintf(text, sizeof text, "%s 0x%08lX", what, static_cast<unsigned long>(status));
        SetVBlankError(fb, text);
        closeAdapter();
        std::unique_lock<std::mutex> lock(fb.doneMutex);
        fb.doneCv.wait_for(lock, std::chrono::milliseconds(200), [&] { return fb.stop.load(); });
    };
    while (!fb.stop)
    {
        const int64_t now = SteadyNowNs();
        if (!w.hAdapter || now - checkedNs > 1'000'000'000)
        {
            checkedNs = now;
            HMONITOR current = MonitorFromWindow(fb.hwnd, MONITOR_DEFAULTTONEAREST);
            if (current != monitor || !w.hAdapter)
            {
                closeAdapter();
                MONITORINFOEXW mi{};
                mi.cbSize = sizeof mi;
                GetMonitorInfoW(current, &mi);
                HDC dc = CreateDCW(nullptr, mi.szDevice, nullptr, nullptr);
                D3dkmtOpenAdapterFromHdc oa{};
                oa.hDc = dc;
                const LONG status = dc ? open(&oa) : -1;
                if (dc)
                    DeleteDC(dc);
                if (status != 0)
                {
                    fail("D3DKMTOpenAdapterFromHdc", status);
                    continue;
                }
                w.hAdapter = oa.hAdapter;
                w.VidPnSourceId = oa.VidPnSourceId;
                monitor = current;
                fb.refreshHz = MonitorRefreshHz(fb.hwnd);
                SetVBlankError(fb, "");
            }
        }
        if (const LONG status = wait(&w); status != 0)
        {
            fail("D3DKMTWaitForVerticalBlankEvent", status);
            continue;
        }
        if (fb.stop)
            break;
        const int64_t v = SteadyNowNs();
        std::lock_guard<std::mutex> lock(fb.statsMutex);
        if (last != 0)
        {
            fb.vblankIntervals.push_back(static_cast<double>(v - last) / 1e6);
            if (fb.vblankIntervals.size() > kVBlankWindow)
                fb.vblankIntervals.erase(fb.vblankIntervals.begin());
            if (++sincePeriod >= kVBlankPeriodEvery && fb.vblankIntervals.size() >= kVBlankWindow / 2)
            {
                sincePeriod = 0;
                fb.vblankPeriodNs.store(
                    static_cast<int64_t>(presentstats::Percentile(fb.vblankIntervals, 0.50) * 1e6),
                    std::memory_order_relaxed);
            }
        }
        last = v;
        fb.lastVBlankNs.store(v, std::memory_order_relaxed);
        // The presents before this vblank: the newest is on screen from it, the others were replaced
        size_t before = 0;
        while (before < fb.pending.size() && fb.pending[before].submittedNs < v)
            before++;
        if (before == 0)
            continue;
        // A panel following the presents (VRR) has the vblank at the end of the scan-out the present started
        const double refreshHz = fb.refreshHz;
        const double topHz = refreshHz > 0.0 ? refreshHz : 60.0;
        const int64_t screen = fb.panelFollows ? v - static_cast<int64_t>(1e9 / topHz) : v;
        for (size_t i = 0; i < before; i++)
        {
            const Pending& p = fb.pending[i];
            const bool shown = i + 1 == before;
            if (shown)
            {
                fb.shown++;
                fb.errors.Add(static_cast<double>(screen - p.askedNs) / 1e6);
                // The lead from the frame's own time: a window that handed it over late was late too. Handed over
                // a refresh or more before its time and still late: the compositor held it, more lead cannot help
                const int64_t asked = p.atNs > 0 ? p.atNs : p.askedNs;
                fb.lead.Shown(screen - asked, p.number, std::max(asked, p.handedNs) - p.handedNs);
                fb.beat.Shown(p.trace.picture, p.trace.targetNs, screen, static_cast<int64_t>(1e9 / topHz));
            }
            else
            {
                fb.dropped++;
            }
            fb.trace.Write(p.trace, p.atNs, p.submittedNs, shown ? screen : 0, p.drawableWaitNs);
        }
        fb.pending.erase(fb.pending.begin(), fb.pending.begin() + static_cast<std::ptrdiff_t>(before));
    }
    closeAdapter();
}

class DxgiPresenter final : public NativePresenter
{
public:
    explicit DxgiPresenter(HWND hwnd) : _hwnd(hwnd), _fb(std::make_shared<Feedback>(hwnd)) {}

    ~DxgiPresenter() override
    {
        _stop = true;
        _queueCv.notify_all();
        _wake.Notify();
        if (_presentThread.joinable())
            _presentThread.join();
        if (_vblankThread.joinable())
        {
            bool done = false;
            {
                std::unique_lock<std::mutex> lock(_fb->doneMutex);
                _fb->stop = true;
                _fb->doneCv.notify_all();
                done = _fb->doneCv.wait_for(lock, std::chrono::milliseconds(kVBlankJoinMs), [&] { return _fb->done; });
            }
            if (done)
                _vblankThread.join();
            else
                _vblankThread.detach();  // blocked in the vblank wait; it holds what it touches
        }
        // GL objects and interop registrations in the drawing context (current: the window's destructor)
        for (Slot& s : _slots)
            ReleaseSlot(s);
        if (_interopDevice && _closeDevice)
            _closeDevice(_interopDevice);
        // The swap chain lets the window go only when its last reference is gone on the GPU too: D3D11 destroys
        // late, and a new swap chain for the same window meanwhile gets E_ACCESSDENIED (seen on .101 after a GPU
        // reset, the window frozen). ClearState unbinds, Flush runs the deferred destruction now
        _rtv.Reset();
        if (_waitable)
            CloseHandle(_waitable);
        _waitable = nullptr;
        _swap.Reset();
        _vs.Reset();
        _ps.Reset();
        if (_context)
        {
            _context->ClearState();
            _context->Flush();
        }
    }

    bool Init()
    {
        QOpenGLContext* gl = QOpenGLContext::currentContext();
        if (!gl)
            return false;
        // The extension: NVIDIA and AMD have it; without it the window presents with GL
        auto getExtensions = reinterpret_cast<GetExtensionsStringFn>(gl->getProcAddress("wglGetExtensionsStringARB"));
        const char* extensions = getExtensions ? getExtensions(wglGetCurrentDC()) : nullptr;
        if (!extensions || !std::strstr(extensions, "WGL_NV_DX_interop2"))
        {
            _error = "no WGL_NV_DX_interop2";
            return false;
        }
        _openDevice = reinterpret_cast<DXOpenDeviceFn>(gl->getProcAddress("wglDXOpenDeviceNV"));
        _closeDevice = reinterpret_cast<DXCloseDeviceFn>(gl->getProcAddress("wglDXCloseDeviceNV"));
        _registerObject = reinterpret_cast<DXRegisterObjectFn>(gl->getProcAddress("wglDXRegisterObjectNV"));
        _unregisterObject = reinterpret_cast<DXUnregisterObjectFn>(gl->getProcAddress("wglDXUnregisterObjectNV"));
        _lockObjects = reinterpret_cast<DXLockObjectsFn>(gl->getProcAddress("wglDXLockObjectsNV"));
        _unlockObjects = reinterpret_cast<DXLockObjectsFn>(gl->getProcAddress("wglDXUnlockObjectsNV"));
        if (!_openDevice || !_closeDevice || !_registerObject || !_unregisterObject || !_lockObjects || !_unlockObjects)
        {
            _error = "WGL_NV_DX_interop2 entry points missing";
            return false;
        }

        if (FAILED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT,
                                     nullptr, 0, D3D11_SDK_VERSION, &_device, nullptr, &_context)))
        {
            _error = "D3D11CreateDevice failed";
            return false;
        }
        // The interop's lock / unlock on the GUI thread reach the device while the presenter thread draws
        ComPtr<ID3D11Multithread> multithread;
        if (SUCCEEDED(_context.As(&multithread)))
            multithread->SetMultithreadProtected(TRUE);

        ComPtr<IDXGIDevice> dxgiDevice;
        ComPtr<IDXGIAdapter> adapter;
        ComPtr<IDXGIFactory2> factory;
        if (FAILED(_device.As(&dxgiDevice)) || FAILED(dxgiDevice->GetAdapter(&adapter)) ||
            FAILED(adapter->GetParent(IID_PPV_ARGS(&factory))))
        {
            _error = "no DXGI factory";
            return false;
        }
        ComPtr<IDXGIFactory5> factory5;
        BOOL tearing = FALSE;
        if (SUCCEEDED(factory.As(&factory5)))
            factory5->CheckFeatureSupport(DXGI_FEATURE_PRESENT_ALLOW_TEARING, &tearing, sizeof tearing);
        _tearing = tearing != FALSE;

        DXGI_SWAP_CHAIN_DESC1 sd{};
        sd.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        sd.SampleDesc.Count = 1;
        sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        sd.BufferCount = 3;
        sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
        sd.Scaling = DXGI_SCALING_STRETCH;
        sd.AlphaMode = DXGI_ALPHA_MODE_IGNORE;
        sd.Flags =
            DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT | (_tearing ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0);
        _swapFlags = sd.Flags;
        ComPtr<IDXGISwapChain1> swap1;
        const HRESULT hr = factory->CreateSwapChainForHwnd(_device.Get(), _hwnd, &sd, nullptr, nullptr, &swap1);
        if (FAILED(hr) || FAILED(swap1.As(&_swap)))
        {
            char buf[64];
            std::snprintf(buf, sizeof buf, "CreateSwapChainForHwnd 0x%08lX", static_cast<unsigned long>(hr));
            _error = buf;
            return false;
        }
        factory->MakeWindowAssociation(_hwnd, DXGI_MWA_NO_ALT_ENTER | DXGI_MWA_NO_WINDOW_CHANGES);
        _swap->SetMaximumFrameLatency(1);
        _waitable = _swap->GetFrameLatencyWaitableObject();

        ComPtr<ID3DBlob> vsb, psb, err;
        if (FAILED(
                D3DCompile(kHlsl, std::strlen(kHlsl), nullptr, nullptr, nullptr, "vs", "vs_5_0", 0, 0, &vsb, &err)) ||
            FAILED(
                D3DCompile(kHlsl, std::strlen(kHlsl), nullptr, nullptr, nullptr, "ps", "ps_5_0", 0, 0, &psb, &err)) ||
            FAILED(_device->CreateVertexShader(vsb->GetBufferPointer(), vsb->GetBufferSize(), nullptr, &_vs)) ||
            FAILED(_device->CreatePixelShader(psb->GetBufferPointer(), psb->GetBufferSize(), nullptr, &_ps)))
        {
            _error = "shader compile failed";
            return false;
        }

        _interopDevice = _openDevice(_device.Get());
        if (!_interopDevice)
        {
            _error = "wglDXOpenDeviceNV failed";
            return false;
        }

        _vrr = _tearing && Policy() != PresentPolicy::Vsync;
        _fb->panelFollows = _tearing;
        _fb->refreshHz = MonitorRefreshHz(_hwnd);
#ifdef UNREAL_TEST_HOOKS
        // UNREAL_TEST_GPU_HANG=d3d:<s>: the recovery test, the first presenter's GPU work never ends that long from
        // now (once: the presenter made anew presents on)
        static std::atomic<bool> hangArmed{false};
        if (const QByteArray hang = qgetenv("UNREAL_TEST_GPU_HANG"); hang.startsWith("d3d:") && !hangArmed.exchange(true))
            _hangAtNs = SteadyNowNs() + static_cast<int64_t>(hang.mid(4).toInt()) * 1'000'000'000;
#endif

        _presentThread = std::thread([this] { RunPresent(); });
        _vblankThread = std::thread([fb = _fb] {
            RunVBlanks(fb);
            {
                std::lock_guard<std::mutex> lock(fb->doneMutex);
                fb->done = true;
            }
            fb->doneCv.notify_all();
        });
        return true;
    }

    const std::string& Error() const
    {
        return _error;
    }

    unsigned BeginFrame(int width, int height) override
    {
        QOpenGLContext* gl = QOpenGLContext::currentContext();
        if (width <= 0 || height <= 0 || !gl)
            return 0;
        // The device gone: a new presenter is needed, no waiting
        if (Lost())
            return Failed("the D3D11 device is gone (a GPU reset)");
        QOpenGLFunctions* f = gl->functions();

        // The next slot the presenter thread is not copying; all three queued: wait for one
        size_t index = (_current + 1) % kSlots;
        {
            std::unique_lock<std::mutex> lock(_queueMutex);
            const auto found = [&] {
                for (size_t i = 0; i < kSlots; i++)
                {
                    if (!_slots[(_current + 1 + i) % kSlots].queued)
                    {
                        index = (_current + 1 + i) % kSlots;
                        return true;
                    }
                }
                return false;
            };
            if (!_freeCv.wait_for(lock, std::chrono::milliseconds(100), [&] { return found() || Lost(); }) || Lost())
            {
                char why[96];
                std::snprintf(why, sizeof why, "no free slot (%u queued for the presenter thread)%s",
                              static_cast<unsigned>(_queue.size()), Lost() ? ", the D3D11 device is gone" : "");
                lock.unlock();
                return Failed(why);
            }
        }
        Slot& slot = _slots[index];
        if (slot.width != width || slot.height != height || !slot.framebuffer)
        {
            // A new size: every slot made anew while the presenter thread is idle. The NVIDIA interop's register
            // racing that thread's ResizeBuffers / draw / Present hung the GPU (a TDR in about 1 of 40 new
            // presenters on .101, the old code too); the device's lock around the interop's calls deadlocks in
            // the driver instead
            {
                std::unique_lock<std::mutex> lock(_queueMutex);
                if (!_freeCv.wait_for(lock, std::chrono::milliseconds(100),
                                      [&] { return (_queue.empty() && !_busy) || Lost(); }) ||
                    Lost())
                {
                    lock.unlock();
                    return Failed(Lost() ? "the D3D11 device is gone (a GPU reset)"
                                         : "the presenter thread still busy for a new slot size");
                }
            }
            for (Slot& s : _slots)
            {
                if (const char* why = AllocateSlot(s, width, height, f))
                {
                    ReleaseSlot(s);
                    return Failed(why, width, height);
                }
            }
        }
        if (!_lockObjects(_interopDevice, 1, &slot.interop))
            return Failed("wglDXLockObjectsNV failed");
        f->glBindFramebuffer(GL_FRAMEBUFFER, slot.framebuffer);
        _current = index;
        _drawing = true;
        return slot.framebuffer;
    }

    std::string LastFailure() const override
    {
        std::lock_guard<std::mutex> lock(_fb->statsMutex);
        return _fb->failure;
    }

    bool Lost() const override
    {
        return _removed.load(std::memory_order_acquire) != S_OK;
    }

    void PresentFrame(int64_t atNs, const TraceInfo& trace) override
    {
        if (!_drawing)
            return;
        _drawing = false;
        Slot& slot = _slots[_current];
        // The unlock flushes GL and hands the texture to D3D
        const int64_t unlock0 = SteadyNowNs();
        _unlockObjects(_interopDevice, 1, &slot.interop);
        const int64_t handed = SteadyNowNs();
        const double unlockMs = static_cast<double>(handed - unlock0) / 1e6;
        uint64_t number = 0;
        {
            std::lock_guard<std::mutex> lock(_fb->statsMutex);
            number = ++_fb->presented;
            _fb->gpuWaits.Add(unlockMs);
        }
        {
            std::lock_guard<std::mutex> lock(_queueMutex);
            slot.queued = true;
            _queue.push_back({_current, atNs, handed, number, trace});
        }
        _queueCv.notify_one();
        _wake.Notify();
    }

    bool IsVisible() const override
    {
        if (IsIconic(GetAncestor(_hwnd, GA_ROOT)))
            return false;
        // DXGI said occluded: try again half a second later (a present tells)
        const int64_t occluded = _occludedNs.load(std::memory_order_relaxed);
        return occluded == 0 || SteadyNowNs() - occluded > 500'000'000;
    }

    /// The presenter thread presents at the time itself; the window draws a margin before, adapted to the load
    /// (PresentLead: from 8 ms, 4 to 20 ms)
    int64_t LeadNs(int64_t refreshPeriodNs) const override
    {
        return _fb->lead.Margin(refreshPeriodNs);
    }

    /// auto: VRR while the vblanks follow the presents, else vsync (CheckVrr); vrr: Present(0, ALLOW_TEARING) at the
    /// time, always; vsync: Present(0) without tearing at the time, always. VRR needs tearing supported
    void SetPolicy(PresentPolicy policy) override
    {
        _policy.store(static_cast<uint8_t>(policy), std::memory_order_relaxed);
        _vrr = _tearing && policy != PresentPolicy::Vsync;
        std::lock_guard<std::mutex> lock(_fb->statsMutex);
        _fb->vrrNote.clear();
    }

    /// The vblank thread's last vblank and their interval (the monitor's rate before there are enough of them);
    /// false while presenting for VRR. Lock-free: the vblank thread publishes both
    bool RefreshGrid(int64_t& refreshNs, int64_t& periodNs) const override
    {
        const int64_t last = _fb->lastVBlankNs.load(std::memory_order_relaxed);
        if (last == 0 || _vrr)
            return false;
        int64_t period = _fb->vblankPeriodNs.load(std::memory_order_relaxed);
        const double hz = _fb->refreshHz;
        if (period <= 0 && hz > 0.0)
            period = static_cast<int64_t>(1e9 / hz);
        if (period <= 0)
            return false;
        refreshNs = last;
        periodNs = period;
        return true;
    }

    Stats GetStats() const override
    {
        Stats s;
        std::vector<double> errors, waits, drawableWaits;
        {
            std::lock_guard<std::mutex> lock(_fb->statsMutex);
            errors = _fb->errors.Values();
            waits = _fb->gpuWaits.Values();
            drawableWaits = _fb->drawableWaits.Values();
            s.presented = _fb->presented;
            s.shown = _fb->shown;
            s.dropped = _fb->dropped;
            const ScreenBeat::Stats beat = _fb->beat.GetStats();
            s.beatFrames = beat.frames;
            s.offBeat = beat.offBeat;
            s.soundOffsetP50 = beat.offsetP50Ms;
            s.soundOffsetP99 = beat.offsetP99Ms;
        }
        s.late = _fb->lead.Late();
        s.leadMs = static_cast<double>(_fb->lead.MarginNs()) / 1e6;
        presentstats::Fill(s, std::move(errors), std::move(waits), std::move(drawableWaits));
        // Windows reports no VRR range: while the panel follows the frames its low end is not known (0)
        s.maxRefreshHz = _fb->refreshHz;
        s.minRefreshHz = _vrr ? 0.0 : _fb->refreshHz.load();
        s.visible = IsVisible();
        s.vrr = _vrr;
        return s;
    }

    std::string Describe() const override
    {
        const Stats s = GetStats();
        double vblankP50 = 0.0;
        std::string vrrNote, vblankError;
        {
            std::lock_guard<std::mutex> lock(_fb->statsMutex);
            vblankP50 = presentstats::Percentile(_fb->vblankIntervals, 0.50);
            vrrNote = _fb->vrrNote;
            vblankError = _fb->vblankError;
        }
        const char* mode = _vrr ? "VRR, Present(0, ALLOW_TEARING) at the time" : "vsync, Present(0) at the time";
        char line[512];
        std::snprintf(
            line, sizeof line,
            "DXGI %s (policy %s, tearing %s%s): %llu presented, %llu shown, %llu dropped, %llu a refresh late (lead "
            "%.1f ms); error p50 %.2f p99 "
            "%.2f max %.2f ms; GL unlock p50 %.2f max %.2f ms; back buffer wait p50 %.2f max %.2f ms; display "
            "%.0f Hz, vblank interval p50 %.2f ms (%s)%s%s%s",
            mode, PresentPolicyName(Policy()), _tearing ? "supported" : "not supported",
            vrrNote.empty() ? "" : (", " + vrrNote).c_str(), static_cast<unsigned long long>(s.presented),
            static_cast<unsigned long long>(s.shown), static_cast<unsigned long long>(s.dropped),
            static_cast<unsigned long long>(s.late), s.leadMs, s.errorP50, s.errorP99, s.errorMax, s.gpuWaitP50,
            s.gpuWaitMax, s.drawableWaitP50, s.drawableWaitMax, _fb->refreshHz.load(), vblankP50,
            _fb->panelFollows ? "the panel follows the frames" : "a fixed grid",
            vblankError.empty() ? "" : ("; " + vblankError).c_str(), s.visible ? "" : "; occluded",
            Lost() ? "; the device is gone" : "");
        return line;
    }

private:
    struct Slot
    {
        ComPtr<ID3D11Texture2D> texture;
        ComPtr<ID3D11ShaderResourceView> srv;
        GLuint glTexture = 0;
        GLuint framebuffer = 0;
        HANDLE interop = nullptr;
        int width = 0;
        int height = 0;
        bool queued = false;  // handed to the presenter thread, not copied yet (_queueMutex)
    };
    struct Item
    {
        size_t slot = 0;
        int64_t atNs = 0;
        int64_t handedNs = 0;
        uint64_t number = 0;
        TraceInfo trace;
    };

    void ReleaseSlot(Slot& s)
    {
        QOpenGLContext* gl = QOpenGLContext::currentContext();
        if (s.interop && _unregisterObject)
            _unregisterObject(_interopDevice, s.interop);
        s.interop = nullptr;
        if (gl)
        {
            QOpenGLFunctions* f = gl->functions();
            if (s.framebuffer)
                f->glDeleteFramebuffers(1, &s.framebuffer);
            if (s.glTexture)
                f->glDeleteTextures(1, &s.glTexture);
        }
        s.framebuffer = 0;
        s.glTexture = 0;
        s.srv.Reset();
        s.texture.Reset();
        s.width = s.height = 0;
    }

    /// GUI thread: BeginFrame gives no framebuffer; `why` with the system's last error for the window's log
    unsigned Failed(const char* why, int width = 0, int height = 0)
    {
        const unsigned long error = GetLastError();
        const HRESULT removed = _device ? _device->GetDeviceRemovedReason() : S_OK;
        char line[256];
        int n = std::snprintf(line, sizeof line, "%s", why);
        if (width > 0 && n > 0 && n < static_cast<int>(sizeof line))
            n += std::snprintf(line + n, sizeof line - n, " (%dx%d)", width, height);
        if (n > 0 && n < static_cast<int>(sizeof line))
            std::snprintf(line + n, sizeof line - n, ", last error %lu, device %s 0x%08lX", error,
                          removed == S_OK ? "ok" : "removed", static_cast<unsigned long>(removed));
        std::lock_guard<std::mutex> lock(_fb->statsMutex);
        _fb->failure = line;
        return 0;
    }

    /// GUI thread: the slot's texture, its interop registration and framebuffer. @return nullptr, or what failed
    const char* AllocateSlot(Slot& s, int width, int height, QOpenGLFunctions* f)
    {
        ReleaseSlot(s);
        D3D11_TEXTURE2D_DESC td{};
        td.Width = static_cast<UINT>(width);
        td.Height = static_cast<UINT>(height);
        td.MipLevels = 1;
        td.ArraySize = 1;
        td.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        td.SampleDesc.Count = 1;
        td.Usage = D3D11_USAGE_DEFAULT;
        td.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
        if (FAILED(_device->CreateTexture2D(&td, nullptr, &s.texture)) ||
            FAILED(_device->CreateShaderResourceView(s.texture.Get(), nullptr, &s.srv)))
            return "the slot's D3D11 texture not made";
        f->glGenTextures(1, &s.glTexture);
        s.interop =
            _registerObject(_interopDevice, s.texture.Get(), s.glTexture, GL_TEXTURE_2D, kWglAccessWriteDiscardNV);
        if (!s.interop)
            return "wglDXRegisterObjectNV failed";
        if (!_lockObjects(_interopDevice, 1, &s.interop))
            return "wglDXLockObjectsNV failed on a new slot";
        f->glGenFramebuffers(1, &s.framebuffer);
        f->glBindFramebuffer(GL_FRAMEBUFFER, s.framebuffer);
        f->glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, s.glTexture, 0);
        const bool complete = f->glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
        _unlockObjects(_interopDevice, 1, &s.interop);
        if (!complete)
            return "the slot's framebuffer incomplete";
        s.width = width;
        s.height = height;
        return nullptr;
    }

    /// Presenter thread: the slot into the back buffer (resized to it first)
    bool CopyToBackBuffer(Slot& slot)
    {
        const UINT w = static_cast<UINT>(slot.width), h = static_cast<UINT>(slot.height);
        if (w != _backWidth || h != _backHeight || !_rtv)
        {
            _rtv.Reset();
            _context->OMSetRenderTargets(0, nullptr, nullptr);
            if (FAILED(_swap->ResizeBuffers(0, w, h, DXGI_FORMAT_UNKNOWN, _swapFlags)))
                return false;
            ComPtr<ID3D11Texture2D> back;
            if (FAILED(_swap->GetBuffer(0, IID_PPV_ARGS(&back))) ||
                FAILED(_device->CreateRenderTargetView(back.Get(), nullptr, &_rtv)))
                return false;
            _backWidth = w;
            _backHeight = h;
        }
        D3D11_VIEWPORT vp{0.0f, 0.0f, static_cast<float>(w), static_cast<float>(h), 0.0f, 1.0f};
        _context->OMSetRenderTargets(1, _rtv.GetAddressOf(), nullptr);
        _context->RSSetViewports(1, &vp);
        _context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        _context->VSSetShader(_vs.Get(), nullptr, 0);
        _context->PSSetShader(_ps.Get(), nullptr, 0);
        _context->PSSetShaderResources(0, 1, slot.srv.GetAddressOf());
        _context->Draw(3, 0);
        ID3D11ShaderResourceView* none = nullptr;
        _context->PSSetShaderResources(0, 1, &none);
        _context->Flush();
        return true;
    }

    /// Presenter thread: the device gone (`reason`, the first one kept): BeginFrame fails at once from now on, the window
    /// makes a new presenter without waiting
    void LatchRemoved(HRESULT reason)
    {
        HRESULT expected = S_OK;
        if (_removed.compare_exchange_strong(expected, FAILED(reason) ? reason : DXGI_ERROR_DEVICE_REMOVED))
            qWarning("DxgiPresenter: the D3D11 device is gone (0x%08lX): presenting stops",
                     static_cast<unsigned long>(reason));
        _freeCv.notify_all();
    }

    void RunPresent()
    {
        SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_HIGHEST);
        // The monitor must not sleep while frames are shown: no vblanks then, and DWM composes on a timer (POC 037)
        SetThreadExecutionState(ES_CONTINUOUS | ES_DISPLAY_REQUIRED);
        while (!_stop)
        {
            Item item;
            bool replaced = false;
            bool lost = false;
            {
                std::unique_lock<std::mutex> lock(_queueMutex);
                if (_busy)
                {
                    // The last item's copy and present are done: the GUI thread may (re)make slots now
                    _busy = false;
                    _freeCv.notify_all();
                }
                _queueCv.wait(lock, [&] { return _stop || !_queue.empty(); });
                if (_stop)
                    break;
                item = _queue.front();
                _queue.pop_front();
                _busy = true;
                // The device gone: nothing presents any more, the slot is free again
                lost = Lost();
                // A later frame already due: this one would be replaced before any refresh
                replaced = !lost && !_queue.empty() && _queue.front().atNs <= SteadyNowNs();
                if (replaced || lost)
                    _slots[item.slot].queued = false;
            }
            if (lost)
            {
                _freeCv.notify_all();
                continue;
            }
            if (replaced)
            {
                _freeCv.notify_all();
                std::lock_guard<std::mutex> lock(_fb->statsMutex);
                _fb->dropped++;
                continue;
            }

            // Max frame latency 1: a back buffer is free once the previous present was taken
            const int64_t wait0 = SteadyNowNs();
            WaitForSingleObjectEx(_waitable, 100, TRUE);
            const int64_t drawableWaitNs = SteadyNowNs() - wait0;
            if (GiveWayToTheNext(item))
            {
                _freeCv.notify_all();
                std::lock_guard<std::mutex> lock(_fb->statsMutex);
                _fb->dropped++;
            }
            const bool copied = CopyToBackBuffer(_slots[item.slot]);
            {
                std::lock_guard<std::mutex> lock(_queueMutex);
                _slots[item.slot].queued = false;
            }
            _freeCv.notify_all();
            if (!copied)
            {
                // ResizeBuffers / GetBuffer refused: a device the GPU reset removed says so here
                if (const HRESULT reason = _device->GetDeviceRemovedReason(); FAILED(reason))
                    LatchRemoved(reason);
                continue;
            }

            if (item.atNs > SteadyNowNs() + kPresentCostNs)
            {
                TimeHelper::WaitUntilPrecise(
                    std::chrono::steady_clock::time_point(std::chrono::nanoseconds(item.atNs - kPresentCostNs)),
                    [this] { return _stop.load(); }, &_wake);
            }
            const bool vrr = _vrr;
            const int64_t submitted = SteadyNowNs();
            const HRESULT hr = _swap->Present(0, vrr ? DXGI_PRESENT_ALLOW_TEARING : 0);
            if (DeviceGone(hr))
            {
                const HRESULT reason = _device->GetDeviceRemovedReason();
                LatchRemoved(FAILED(reason) ? reason : hr);
                continue;
            }
            if (hr == DXGI_STATUS_OCCLUDED)
                _occludedNs = SteadyNowNs();
            else
                _occludedNs = 0;
            {
                std::lock_guard<std::mutex> lock(_fb->statsMutex);
                _fb->drawableWaits.Add(static_cast<double>(drawableWaitNs) / 1e6);
                _fb->pending.push_back({submitted, std::max(item.atNs, submitted), item.atNs, item.handedNs,
                                        item.number, drawableWaitNs, item.trace});
                while (_fb->pending.size() > 16)
                {
                    _fb->pending.pop_front();
                    _fb->dropped++;
                }
                if (item.atNs > 0 && _lastAtNs > 0)
                {
                    _fb->presentIntervals.push_back(static_cast<double>(item.atNs - _lastAtNs) / 1e6);
                    if (_fb->presentIntervals.size() > kVBlankWindow)
                        _fb->presentIntervals.erase(_fb->presentIntervals.begin());
                }
                if (item.atNs > 0)
                    _lastAtNs = item.atNs;
            }
            if (++_sinceCheck >= 100)
            {
                _sinceCheck = 0;
                CheckVrr();
            }
#ifdef UNREAL_TEST_HOOKS
            if (_hangAtNs != 0 && SteadyNowNs() >= _hangAtNs)
            {
                _hangAtNs = 0;
                HangDevice();
            }
#endif
        }
        SetThreadExecutionState(ES_CONTINUOUS);
    }

#ifdef UNREAL_TEST_HOOKS
    /// Presenter thread (UNREAL_TEST_GPU_HANG=d3d:<s>): a compute shader that never ends on this device; the system
    /// resets the GPU after its timeout (TDR) and removes the device, as the hang on .101 did
    void HangDevice()
    {
        static const char* const kHang = R"(
RWStructuredBuffer<uint> data : register(u0);
[numthreads(64, 1, 1)]
void main(uint3 id : SV_DispatchThreadID) {
    [loop] while (data[0] != 12345u) { InterlockedAdd(data[1 + id.x % 63], 1u); }
}
)";
        qWarning() << "DxgiPresenter: UNREAL_TEST_GPU_HANG: this device's GPU work hangs now";
        ComPtr<ID3DBlob> cs, err;
        ComPtr<ID3D11ComputeShader> shader;
        D3D11_BUFFER_DESC bd{};
        bd.ByteWidth = 64 * 4;
        bd.Usage = D3D11_USAGE_DEFAULT;
        bd.BindFlags = D3D11_BIND_UNORDERED_ACCESS;
        bd.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
        bd.StructureByteStride = 4;
        ComPtr<ID3D11Buffer> buffer;
        ComPtr<ID3D11UnorderedAccessView> uav;
        if (FAILED(D3DCompile(kHang, std::strlen(kHang), nullptr, nullptr, nullptr, "main", "cs_5_0", 0, 0, &cs,
                              &err)) ||
            FAILED(_device->CreateComputeShader(cs->GetBufferPointer(), cs->GetBufferSize(), nullptr, &shader)) ||
            FAILED(_device->CreateBuffer(&bd, nullptr, &buffer)) ||
            FAILED(_device->CreateUnorderedAccessView(buffer.Get(), nullptr, &uav)))
            return;
        _context->CSSetShader(shader.Get(), nullptr, 0);
        _context->CSSetUnorderedAccessViews(0, 1, uav.GetAddressOf(), nullptr);
        _context->Dispatch(1, 1, 1);
        ID3D11UnorderedAccessView* none = nullptr;
        _context->CSSetUnorderedAccessViews(0, 1, &none, nullptr);
        _context->CSSetShader(nullptr, nullptr, 0);
        _context->Flush();
    }
#endif

    /// Presenter thread, the back buffer held: a frame whose time is past by more than a third of its period (the
    /// wait for the back buffer stalled 14-30 ms while the system moved the window between direct flip and composition
    /// on a focus change, seen on .101) would be on screen for a moment, the next one right after it: a flash in the
    /// motion. It gives way to the next frame (queued, or handed over within the period), shown at its own time; the
    /// picture before holds once instead. @return the frame was replaced (its slot freed)
    bool GiveWayToTheNext(Item& item)
    {
        const int64_t lastAtNs = _lastAtNs;  // the presenter thread's own
        if (item.atNs <= 0 || lastAtNs <= 0 || item.atNs <= lastAtNs)
            return false;
        const int64_t periodNs = item.atNs - lastAtNs;
        std::unique_lock<std::mutex> lock(_queueMutex);
        if (SteadyNowNs() - item.atNs <= periodNs / 3)
            return false;
        _queueCv.wait_until(lock,
                            std::chrono::steady_clock::time_point(std::chrono::nanoseconds(item.atNs + periodNs)),
                            [&] { return _stop || !_queue.empty(); });
        if (_stop || _queue.empty() || _queue.front().atNs <= item.atNs)
            return false;
        _slots[item.slot].queued = false;
        item = _queue.front();
        _queue.pop_front();
        return true;
    }

    /// Presenter thread: do the vblanks follow the presents? On a fixed display they stay on the refresh grid whatever
    /// the presents do. A G-Sync panel follows without tearing allowed too (policy vsync), so the screen times take
    /// the scan-out off by what the panel does, not by the present mode. Policy auto on a fixed display: Present(0)
    /// without tearing (tearing in an independent-flip window would show) until the vblanks follow the presents again,
    /// then VRR again: the window started in a remote desktop session (a 32 Hz grid) or behind another one (windowed
    /// G-Sync is the foreground window's) and moved to the console or to the front gets it there, without a restart
    /// VRR is on while the vblanks follow the presents: off the display's grid and at the presents' interval. A steady
    /// grid of their own that the presents are not on is no VRR, whoever's grid it is: the display's (a fixed display,
    /// VRR off for this window), or another one's (a remote session's display reports its own rate while the vblank
    /// wait still hears the monitor the adapter drives). A mode change's mix of two grids is neither: nothing changes
    void CheckVrr()
    {
        double vblank = 0.0, vblankLo = 0.0, vblankHi = 0.0, present = 0.0;
        {
            std::lock_guard<std::mutex> lock(_fb->statsMutex);
            if (_fb->vblankIntervals.size() < kVBlankWindow / 2 || _fb->presentIntervals.size() < kVBlankWindow / 2)
                return;
            vblank = presentstats::Percentile(_fb->vblankIntervals, 0.50);
            vblankLo = presentstats::Percentile(_fb->vblankIntervals, 0.10);
            vblankHi = presentstats::Percentile(_fb->vblankIntervals, 0.90);
            present = presentstats::Percentile(_fb->presentIntervals, 0.50);
        }
        const double refreshHz = MonitorRefreshHz(_hwnd);
        _fb->refreshHz = refreshHz;
        if (refreshHz <= 0.0 || vblank <= 0.0 || present <= 0.0)
            return;
        const double refresh = 1000.0 / refreshHz;
        const bool onGrid = std::abs(vblank - refresh) < refresh * 0.03;
        const bool follows = std::abs(vblank - present) < present * 0.03;
        const bool steady = vblankHi - vblankLo < vblank * 0.03;
        const bool presentsOffGrid = std::abs(present - vblank) > vblank * 0.10;
        // Off the grid and with the presents: the panel follows them
        if (follows && !onGrid)
        {
            _fb->panelFollows = true;
            if (Policy() == PresentPolicy::Auto && _tearing && !_vrr)
            {
                _vrr = true;
                {
                    std::lock_guard<std::mutex> lock(_fb->statsMutex);
                    _fb->vrrNote.clear();
                }
                qInfo().noquote() << "DxgiPresenter: the vblanks follow the frames again (every" << vblank
                                  << "ms) - VRR, Present(0, ALLOW_TEARING) from now on";
            }
            return;
        }
        if (!(onGrid || steady) || !presentsOffGrid)
            return;
        _fb->panelFollows = false;
        if (Policy() == PresentPolicy::Auto && _vrr)
        {
            _vrr = false;
            char note[200];
            if (onGrid)
                std::snprintf(note, sizeof note, "no VRR: vblanks every %.2f ms while frames come every %.2f ms",
                              vblank, present);
            else
                std::snprintf(note, sizeof note,
                              "no VRR: vblanks every %.2f ms, not the display's %.2f ms (another display's: a remote "
                              "session), while frames come every %.2f ms",
                              vblank, refresh, present);
            {
                std::lock_guard<std::mutex> lock(_fb->statsMutex);
                _fb->vrrNote = note;
            }
            qInfo().noquote() << "DxgiPresenter:" << note << "- Present(0) without tearing from now on";
        }
    }

    HWND _hwnd;
    std::string _error;

    DXOpenDeviceFn _openDevice = nullptr;
    DXCloseDeviceFn _closeDevice = nullptr;
    DXRegisterObjectFn _registerObject = nullptr;
    DXUnregisterObjectFn _unregisterObject = nullptr;
    DXLockObjectsFn _lockObjects = nullptr;
    DXLockObjectsFn _unlockObjects = nullptr;
    HANDLE _interopDevice = nullptr;

    ComPtr<ID3D11Device> _device;
    ComPtr<ID3D11DeviceContext> _context;
    ComPtr<IDXGISwapChain2> _swap;
    ComPtr<ID3D11RenderTargetView> _rtv;
    ComPtr<ID3D11VertexShader> _vs;
    ComPtr<ID3D11PixelShader> _ps;
    HANDLE _waitable = nullptr;
#ifdef UNREAL_TEST_HOOKS
    int64_t _hangAtNs = 0;  // UNREAL_TEST_GPU_HANG=d3d: the presenter thread's
#endif
    UINT _swapFlags = 0;
    UINT _backWidth = 0;
    UINT _backHeight = 0;
    bool _tearing = false;
    PresentPolicy Policy() const
    {
        return static_cast<PresentPolicy>(_policy.load(std::memory_order_relaxed));
    }

    std::atomic<uint8_t> _policy{0};  // a PresentPolicy (SetPolicy from the GUI thread)
    std::atomic<bool> _vrr{false};
    std::atomic<HRESULT> _removed{S_OK};  // the device gone (LatchRemoved): its reason
    int _sinceCheck = 0;

    std::array<Slot, kSlots> _slots;
    size_t _current = 0;
    bool _drawing = false;

    std::mutex _queueMutex;
    bool _busy = false;  // the presenter thread copies or presents an item it took (_queueMutex)
    std::condition_variable _queueCv;
    std::condition_variable _freeCv;
    std::deque<Item> _queue;
    WakeSignal _wake;
    std::atomic<bool> _stop{false};
    std::thread _presentThread;
    std::thread _vblankThread;
    std::atomic<int64_t> _occludedNs{0};
    int64_t _lastAtNs = 0;  // the presenter thread's

    std::shared_ptr<Feedback> _fb;  // the statistics and what the vblank thread touches
};
}  // namespace

std::unique_ptr<NativePresenter> NativePresenter::Create(void* windowHandle)
{
    auto presenter = std::make_unique<DxgiPresenter>(static_cast<HWND>(windowHandle));
    if (!presenter->Init())
    {
        qWarning().noquote() << "DxgiPresenter:" << QString::fromStdString(presenter->Error());
        return nullptr;
    }
    return presenter;
}

#endif  // _WIN32
