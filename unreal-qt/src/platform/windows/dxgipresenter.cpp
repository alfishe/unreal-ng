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

int64_t SteadyNs()
{
    return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

/// Statistics window: the last presents
constexpr size_t kStatsWindow = 600;
/// Shared textures: one being drawn, one waiting for its time, one being copied
constexpr size_t kSlots = 3;
/// The Present call itself, started this much before the frame's time
constexpr int64_t kPresentCostNs = 300'000;
/// Vblank intervals kept for the VRR check
constexpr size_t kVBlankWindow = 120;

double Percentile(std::vector<double> v, double q)
{
    if (v.empty())
        return 0.0;
    std::sort(v.begin(), v.end());
    const size_t i = static_cast<size_t>(q * static_cast<double>(v.size() - 1) + 0.5);
    return v[std::min(i, v.size() - 1)];
}

void AddSample(std::vector<double>& ring, size_t& next, double value)
{
    if (ring.size() < kStatsWindow)
        ring.push_back(value);
    else
        ring[next] = value;
    next = (next + 1) % kStatsWindow;
}

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

class DxgiPresenter final : public NativePresenter
{
public:
    enum class Policy
    {
        Auto,   // VRR while the vblanks follow the presents, else vsync
        Vrr,    // Present(0, ALLOW_TEARING) at the time, always
        Vsync,  // Present(0) without tearing at the time, always
    };

    explicit DxgiPresenter(HWND hwnd) : _hwnd(hwnd) {}

    ~DxgiPresenter() override
    {
        _stop = true;
        _queueCv.notify_all();
        _wake.Notify();
        if (_presentThread.joinable())
            _presentThread.join();
        _vblankStop = true;
        if (_vblankThread.joinable())
            _vblankThread.join();
        // GL objects and interop registrations in the drawing context (current: the window's destructor)
        for (Slot& s : _slots)
            ReleaseSlot(s);
        if (_interopDevice && _closeDevice)
            _closeDevice(_interopDevice);
        if (_trace)
            fclose(_trace);
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

        const QByteArray policy = qgetenv("UNREAL_PRESENT_POLICY").toLower();
        _policy = policy == "vrr" ? Policy::Vrr : policy == "vsync" ? Policy::Vsync : Policy::Auto;
        _vrr = _tearing && _policy != Policy::Vsync;
        _panelFollows = _tearing;
        _refreshHz = MonitorRefreshHz(_hwnd);
        if (const QByteArray path = qgetenv("UNREAL_PRESENT_TRACE"); !path.isEmpty())
            _trace = std::fopen(path.constData(), "w");

        _presentThread = std::thread([this] { RunPresent(); });
        _vblankThread = std::thread([this] { RunVBlanks(); });
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
            if (!_freeCv.wait_for(lock, std::chrono::milliseconds(100), found))
                return 0;
        }
        Slot& slot = _slots[index];
        if (slot.width != width || slot.height != height || !slot.framebuffer)
        {
            if (!AllocateSlot(slot, width, height, f))
            {
                ReleaseSlot(slot);
                return 0;
            }
        }
        if (!_lockObjects(_interopDevice, 1, &slot.interop))
            return 0;
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
        Slot& slot = _slots[_current];
        // The unlock flushes GL and hands the texture to D3D
        const int64_t unlock0 = SteadyNs();
        _unlockObjects(_interopDevice, 1, &slot.interop);
        const double unlockMs = static_cast<double>(SteadyNs() - unlock0) / 1e6;
        {
            std::lock_guard<std::mutex> lock(_queueMutex);
            slot.queued = true;
            _queue.push_back({_current, atNs, trace});
        }
        _queueCv.notify_one();
        _wake.Notify();
        std::lock_guard<std::mutex> lock(_statsMutex);
        _presented++;
        AddSample(_gpuWaits, _gpuWaitNext, unlockMs);
    }

    bool IsVisible() const override
    {
        if (IsIconic(GetAncestor(_hwnd, GA_ROOT)))
            return false;
        // DXGI said occluded: try again half a second later (a present tells)
        const int64_t occluded = _occludedNs.load(std::memory_order_relaxed);
        return occluded == 0 || SteadyNs() - occluded > 500'000'000;
    }

    /// The presenter thread presents at the time itself; the window draws a little before: the wake's slack (~1.5 ms),
    /// the draw and the copy into the back buffer
    int64_t LeadNs(int64_t /*refreshPeriodNs*/) const override
    {
        return 8'000'000;
    }

    Stats GetStats() const override
    {
        Stats s;
        std::vector<double> errors, waits, drawableWaits;
        {
            std::lock_guard<std::mutex> lock(_statsMutex);
            errors = _errors;
            waits = _gpuWaits;
            drawableWaits = _drawableWaits;
            s.presented = _presented;
            s.shown = _shown;
            s.dropped = _dropped;
        }
        s.window = static_cast<uint32_t>(errors.size());
        s.errorP50 = Percentile(errors, 0.50);
        s.errorP99 = Percentile(errors, 0.99);
        s.errorMax = errors.empty() ? 0.0 : *std::max_element(errors.begin(), errors.end());
        s.gpuWaitP50 = Percentile(waits, 0.50);
        s.gpuWaitMax = waits.empty() ? 0.0 : *std::max_element(waits.begin(), waits.end());
        s.drawableWaitP50 = Percentile(drawableWaits, 0.50);
        s.drawableWaitMax = drawableWaits.empty() ? 0.0 : *std::max_element(drawableWaits.begin(), drawableWaits.end());
        s.minRefreshHz = s.maxRefreshHz = _refreshHz;
        s.visible = IsVisible();
        return s;
    }

    std::string Describe() const override
    {
        const Stats s = GetStats();
        double vblankP50 = 0.0;
        std::string vrrNote, vblankError;
        {
            std::lock_guard<std::mutex> lock(_statsMutex);
            vblankP50 = Percentile(_vblankIntervals, 0.50);
            vrrNote = _vrrNote;
            vblankError = _vblankError;
        }
        const char* mode = _vrr ? "VRR, Present(0, ALLOW_TEARING) at the time" : "vsync, Present(0) at the time";
        char line[400];
        std::snprintf(
            line, sizeof line,
            "DXGI %s (policy %s, tearing %s%s): %llu presented, %llu shown, %llu dropped; error p50 %.2f p99 "
            "%.2f max %.2f ms; GL unlock p50 %.2f max %.2f ms; back buffer wait p50 %.2f max %.2f ms; display "
            "%.0f Hz, vblank interval p50 %.2f ms (%s)%s%s",
            mode,
            _policy == Policy::Auto  ? "auto"
            : _policy == Policy::Vrr ? "vrr"
                                     : "vsync",
            _tearing ? "supported" : "not supported", vrrNote.empty() ? "" : (", " + vrrNote).c_str(),
            static_cast<unsigned long long>(s.presented), static_cast<unsigned long long>(s.shown),
            static_cast<unsigned long long>(s.dropped), s.errorP50, s.errorP99, s.errorMax, s.gpuWaitP50, s.gpuWaitMax,
            s.drawableWaitP50, s.drawableWaitMax, _refreshHz.load(), vblankP50,
            _panelFollows ? "the panel follows the frames" : "a fixed grid",
            vblankError.empty() ? "" : ("; " + vblankError).c_str(), s.visible ? "" : "; occluded");
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
        TraceInfo trace;
    };
    struct Pending
    {
        int64_t submittedNs = 0;
        int64_t askedNs = 0;
        int64_t atNs = 0;
        int64_t drawableWaitNs = 0;
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

    bool AllocateSlot(Slot& s, int width, int height, QOpenGLFunctions* f)
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
            return false;
        f->glGenTextures(1, &s.glTexture);
        s.interop =
            _registerObject(_interopDevice, s.texture.Get(), s.glTexture, GL_TEXTURE_2D, kWglAccessWriteDiscardNV);
        if (!s.interop || !_lockObjects(_interopDevice, 1, &s.interop))
            return false;
        f->glGenFramebuffers(1, &s.framebuffer);
        f->glBindFramebuffer(GL_FRAMEBUFFER, s.framebuffer);
        f->glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, s.glTexture, 0);
        const bool complete = f->glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
        _unlockObjects(_interopDevice, 1, &s.interop);
        if (!complete)
            return false;
        s.width = width;
        s.height = height;
        return true;
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

    void RunPresent()
    {
        SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_HIGHEST);
        // The monitor must not sleep while frames are shown: no vblanks then, and DWM composes on a timer (POC 037)
        SetThreadExecutionState(ES_CONTINUOUS | ES_DISPLAY_REQUIRED);
        while (!_stop)
        {
            Item item;
            bool replaced = false;
            {
                std::unique_lock<std::mutex> lock(_queueMutex);
                _queueCv.wait(lock, [&] { return _stop || !_queue.empty(); });
                if (_stop)
                    break;
                item = _queue.front();
                _queue.pop_front();
                // A later frame already due: this one would be replaced before any refresh
                replaced = !_queue.empty() && _queue.front().atNs <= SteadyNs();
                if (replaced)
                    _slots[item.slot].queued = false;
            }
            if (replaced)
            {
                _freeCv.notify_all();
                std::lock_guard<std::mutex> lock(_statsMutex);
                _dropped++;
                continue;
            }

            // Max frame latency 1: a back buffer is free once the previous present was taken
            const int64_t wait0 = SteadyNs();
            WaitForSingleObjectEx(_waitable, 100, TRUE);
            const int64_t drawableWaitNs = SteadyNs() - wait0;
            const bool copied = CopyToBackBuffer(_slots[item.slot]);
            {
                std::lock_guard<std::mutex> lock(_queueMutex);
                _slots[item.slot].queued = false;
            }
            _freeCv.notify_all();
            if (!copied)
                continue;

            if (item.atNs > SteadyNs() + kPresentCostNs)
            {
                TimeHelper::WaitUntilPrecise(
                    std::chrono::steady_clock::time_point(std::chrono::nanoseconds(item.atNs - kPresentCostNs)),
                    [this] { return _stop.load(); }, &_wake);
            }
            const bool vrr = _vrr;
            const int64_t submitted = SteadyNs();
            const HRESULT hr = _swap->Present(0, vrr ? DXGI_PRESENT_ALLOW_TEARING : 0);
            if (hr == DXGI_STATUS_OCCLUDED)
                _occludedNs = SteadyNs();
            else
                _occludedNs = 0;
            {
                std::lock_guard<std::mutex> lock(_statsMutex);
                AddSample(_drawableWaits, _drawableWaitNext, static_cast<double>(drawableWaitNs) / 1e6);
                _pending.push_back({submitted, std::max(item.atNs, submitted), item.atNs, drawableWaitNs, item.trace});
                while (_pending.size() > 16)
                {
                    _pending.pop_front();
                    _dropped++;
                }
                if (item.atNs > 0 && _lastAtNs > 0)
                {
                    _presentIntervals.push_back(static_cast<double>(item.atNs - _lastAtNs) / 1e6);
                    if (_presentIntervals.size() > kVBlankWindow)
                        _presentIntervals.erase(_presentIntervals.begin());
                }
                if (item.atNs > 0)
                    _lastAtNs = item.atNs;
            }
            if (++_sinceCheck >= 100)
            {
                _sinceCheck = 0;
                CheckVrr();
            }
        }
        SetThreadExecutionState(ES_CONTINUOUS);
    }

    /// Presenter thread: do the vblanks follow the presents? On a fixed display they stay on the refresh grid whatever
    /// the presents do. A G-Sync panel follows without tearing allowed too (policy vsync), so the screen times take
    /// the scan-out off by what the panel does, not by the present mode. Policy auto on a fixed display: Present(0)
    /// without tearing from here on (tearing in an independent-flip window would show)
    void CheckVrr()
    {
        double vblank = 0.0, present = 0.0;
        {
            std::lock_guard<std::mutex> lock(_statsMutex);
            if (_vblankIntervals.size() < kVBlankWindow / 2 || _presentIntervals.size() < kVBlankWindow / 2)
                return;
            vblank = Percentile(_vblankIntervals, 0.50);
            present = Percentile(_presentIntervals, 0.50);
        }
        const double refreshHz = MonitorRefreshHz(_hwnd);
        _refreshHz = refreshHz;
        if (refreshHz <= 0.0)
            return;
        const double refresh = 1000.0 / refreshHz;
        const bool onGrid = std::abs(vblank - refresh) < refresh * 0.03;
        const bool presentsOffGrid = std::abs(present - refresh) > refresh * 0.10;
        if (!onGrid)
            _panelFollows = true;
        if (!onGrid || !presentsOffGrid)
            return;
        _panelFollows = false;
        if (_policy == Policy::Auto && _vrr)
        {
            _vrr = false;
            char note[160];
            std::snprintf(note, sizeof note, "no VRR: vblanks every %.2f ms while frames come every %.2f ms", vblank,
                          present);
            {
                std::lock_guard<std::mutex> lock(_statsMutex);
                _vrrNote = note;
            }
            qInfo().noquote() << "DxgiPresenter:" << note << "- Present(0) without tearing from now on";
        }
    }

    void SetVBlankError(const char* error)
    {
        std::lock_guard<std::mutex> lock(_statsMutex);
        _vblankError = error;
    }

    /// Vblank thread: the first vblank after each present is its screen time
    void RunVBlanks()
    {
        // The wake after the vblank event is the measurement's error: the highest priority there is
        SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);
        HMODULE gdi = GetModuleHandleW(L"gdi32.dll");
        auto open = FromProc<OpenAdapterFn>(GetProcAddress(gdi, "D3DKMTOpenAdapterFromHdc"));
        auto wait = FromProc<WaitVBlankFn>(GetProcAddress(gdi, "D3DKMTWaitForVerticalBlankEvent"));
        auto close = FromProc<CloseAdapterFn>(GetProcAddress(gdi, "D3DKMTCloseAdapter"));
        if (!open || !wait || !close)
        {
            SetVBlankError("no D3DKMT vblank wait");
            return;
        }
        // The adapter of the window's monitor, opened again when the window moves to another monitor or the wait fails
        // (a display mode change, a session moved between RDP and the console)
        D3dkmtWaitForVerticalBlankEvent w{};
        HMONITOR monitor = nullptr;
        int64_t checkedNs = 0;
        int64_t last = 0;
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
            SetVBlankError(text);
            closeAdapter();
            const auto retry = std::chrono::steady_clock::now() + std::chrono::milliseconds(200);
            while (!_vblankStop && std::chrono::steady_clock::now() < retry)
                std::this_thread::sleep_for(std::chrono::milliseconds(20));
        };
        while (!_vblankStop)
        {
            const int64_t now = SteadyNs();
            if (!w.hAdapter || now - checkedNs > 1'000'000'000)
            {
                checkedNs = now;
                HMONITOR current = MonitorFromWindow(_hwnd, MONITOR_DEFAULTTONEAREST);
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
                    _refreshHz = MonitorRefreshHz(_hwnd);
                    SetVBlankError("");
                }
            }
            if (const LONG status = wait(&w); status != 0)
            {
                fail("D3DKMTWaitForVerticalBlankEvent", status);
                continue;
            }
            const int64_t v = SteadyNs();
            std::lock_guard<std::mutex> lock(_statsMutex);
            if (last != 0)
            {
                _vblankIntervals.push_back(static_cast<double>(v - last) / 1e6);
                if (_vblankIntervals.size() > kVBlankWindow)
                    _vblankIntervals.erase(_vblankIntervals.begin());
            }
            last = v;
            // The presents before this vblank: the newest is on screen from it, the others were replaced
            size_t before = 0;
            while (before < _pending.size() && _pending[before].submittedNs < v)
                before++;
            if (before == 0)
                continue;
            // A panel following the presents (VRR) has the vblank at the end of the scan-out the present started
            const double refreshHz = _refreshHz;
            const double topHz = refreshHz > 0.0 ? refreshHz : 60.0;
            const int64_t screen = _panelFollows ? v - static_cast<int64_t>(1e9 / topHz) : v;
            for (size_t i = 0; i < before; i++)
            {
                const Pending& p = _pending[i];
                const bool shown = i + 1 == before;
                if (shown)
                {
                    _shown++;
                    AddSample(_errors, _errorNext, static_cast<double>(screen - p.askedNs) / 1e6);
                }
                else
                {
                    _dropped++;
                }
                if (_trace)
                {
                    std::fprintf(_trace, "%llu %lld %lld %lld %lld %lld %lld %lld\n",
                                 static_cast<unsigned long long>(p.trace.serial), static_cast<long long>(p.atNs),
                                 static_cast<long long>(p.submittedNs), static_cast<long long>(shown ? screen : 0),
                                 static_cast<long long>(p.trace.latchNs), static_cast<long long>(p.trace.plannedNs),
                                 static_cast<long long>(p.trace.wokeNs), static_cast<long long>(p.drawableWaitNs));
                }
            }
            _pending.erase(_pending.begin(), _pending.begin() + static_cast<std::ptrdiff_t>(before));
        }
        closeAdapter();
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
    UINT _swapFlags = 0;
    UINT _backWidth = 0;
    UINT _backHeight = 0;
    bool _tearing = false;
    Policy _policy = Policy::Auto;
    std::atomic<bool> _vrr{false};
    std::atomic<bool> _panelFollows{false};  // the vblanks follow the presents (CheckVrr; at first: tearing supported)
    std::string _vrrNote;                    // _statsMutex
    std::atomic<double> _refreshHz{0.0};
    int _sinceCheck = 0;

    std::array<Slot, kSlots> _slots;
    size_t _current = 0;
    bool _drawing = false;

    std::mutex _queueMutex;
    std::condition_variable _queueCv;
    std::condition_variable _freeCv;
    std::deque<Item> _queue;
    WakeSignal _wake;
    std::atomic<bool> _stop{false};
    std::thread _presentThread;
    std::atomic<bool> _vblankStop{false};
    std::thread _vblankThread;
    std::string _vblankError;  // _statsMutex
    std::atomic<int64_t> _occludedNs{0};

    mutable std::mutex _statsMutex;
    std::deque<Pending> _pending;
    std::vector<double> _errors;
    size_t _errorNext = 0;
    std::vector<double> _gpuWaits;
    size_t _gpuWaitNext = 0;
    std::vector<double> _drawableWaits;
    size_t _drawableWaitNext = 0;
    std::vector<double> _vblankIntervals;
    std::vector<double> _presentIntervals;
    int64_t _lastAtNs = 0;
    uint64_t _presented = 0;
    uint64_t _shown = 0;
    uint64_t _dropped = 0;
    FILE* _trace = nullptr;
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
