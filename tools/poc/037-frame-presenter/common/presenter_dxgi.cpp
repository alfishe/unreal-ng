// POC 037: the DXGI presenter on an HWND (a Qt window's or an SDL window's). D3D11, flip-discard swap chain, a
// frame-latency waitable object, tearing allowed when the system supports it (the condition for VRR in a window).
//
//   b-vsync   wait on the swap chain's waitable object (one per refresh), present the newest frame, Present(1)
//   c-vrr     at: each frame once, at latch + one period + delay (own timer), Present(0, ALLOW_TEARING): a G-Sync
//             display starts the refresh at the present
//   d-locked  as b, the waitable object's return ticks the producer, the frame is presented when it is ready
//
// Feedback: a thread polls IDXGISwapChain::GetFrameStatistics every 0.5 ms; PresentCount -> SyncQPCTime is the time
// the present reached the screen (the refresh it was scanned out in). NowNs() is QPC, the same base.

#include "presenter.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <d3d11.h>
#include <d3dcompiler.h>
#include <dwmapi.h>
#include <dxgi1_5.h>
#include <windows.h>
#include <wrl/client.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>
#include <vector>

using Microsoft::WRL::ComPtr;

namespace poc037
{

static const char* kHlsl = R"(
Texture2D t : register(t0);
SamplerState s : register(s0);
struct V { float4 pos : SV_Position; float2 uv : TEXCOORD0; };
V vs(uint id : SV_VertexID) {
    float2 p = float2((id << 1) & 2, id & 2);
    V v; v.pos = float4(p * float2(2, -2) + float2(-1, 1), 0, 1); v.uv = p; return v;
}
float4 ps(V v) : SV_Target { return t.Sample(s, v.uv); }
)";

static int64_t QpcToNs(int64_t qpc)
{
    static const int64_t freq = [] {
        LARGE_INTEGER f;
        QueryPerformanceFrequency(&f);
        return static_cast<int64_t>(f.QuadPart);
    }();
    return static_cast<int64_t>(static_cast<double>(qpc) * 1e9 / static_cast<double>(freq));
}

class DxgiPresenter final : public NativePresenter
{
public:
    DxgiPresenter(HWND hwnd, FrameRing& ring, Producer& producer, PresentLog& log, const Options& o)
        : _hwnd(hwnd), _ring(ring), _producer(producer), _log(log), _o(o)
    {
    }
    ~DxgiPresenter() override { Stop(); }

    bool Start() override
    {
        UINT flags = 0;
        D3D_FEATURE_LEVEL level = D3D_FEATURE_LEVEL_11_0;
        if (FAILED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags, &level, 1, D3D11_SDK_VERSION, &_device,
                                     nullptr, &_context)))
            return false;
        ComPtr<IDXGIDevice> dxgiDevice;
        _device.As(&dxgiDevice);
        ComPtr<IDXGIAdapter> adapter;
        dxgiDevice->GetAdapter(&adapter);
        ComPtr<IDXGIFactory5> factory;
        adapter->GetParent(IID_PPV_ARGS(&factory));
        BOOL tearing = FALSE;
        if (factory)
            factory->CheckFeatureSupport(DXGI_FEATURE_PRESENT_ALLOW_TEARING, &tearing, sizeof tearing);
        // Tearing is a windowed-only flag; exclusive fullscreen flips at the present on a VRR display anyway
        _tearing = tearing != FALSE && !_o.exclusive;

        DXGI_SWAP_CHAIN_DESC1 sd{};
        sd.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        sd.SampleDesc.Count = 1;
        sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        sd.BufferCount = 3;
        sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
        sd.Scaling = DXGI_SCALING_NONE;
        sd.Flags = DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT | (_tearing ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0);
        _swapFlags = sd.Flags;
        ComPtr<IDXGISwapChain1> sc1;
        if (FAILED(factory->CreateSwapChainForHwnd(_device.Get(), _hwnd, &sd, nullptr, nullptr, &sc1)))
            return false;
        factory->MakeWindowAssociation(_hwnd, DXGI_MWA_NO_ALT_ENTER);
        sc1.As(&_swap);
        if (_o.exclusive)
        {
            // At the desktop's mode: no mode change
            _exclusive = SUCCEEDED(_swap->SetFullscreenState(TRUE, nullptr));
        }
        _swap->SetMaximumFrameLatency(1);
        _waitable = _swap->GetFrameLatencyWaitableObject();

        ComPtr<ID3DBlob> vsb, psb, err;
        if (FAILED(D3DCompile(kHlsl, strlen(kHlsl), nullptr, nullptr, nullptr, "vs", "vs_5_0", 0, 0, &vsb, &err)) ||
            FAILED(D3DCompile(kHlsl, strlen(kHlsl), nullptr, nullptr, nullptr, "ps", "ps_5_0", 0, 0, &psb, &err)))
            return false;
        _device->CreateVertexShader(vsb->GetBufferPointer(), vsb->GetBufferSize(), nullptr, &_vs);
        _device->CreatePixelShader(psb->GetBufferPointer(), psb->GetBufferSize(), nullptr, &_ps);
        D3D11_TEXTURE2D_DESC td{};
        td.Width = kWidth;
        td.Height = kHeight;
        td.MipLevels = 1;
        td.ArraySize = 1;
        td.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        td.SampleDesc.Count = 1;
        td.Usage = D3D11_USAGE_DEFAULT;
        td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        _device->CreateTexture2D(&td, nullptr, &_tex);
        _device->CreateShaderResourceView(_tex.Get(), nullptr, &_srv);
        D3D11_SAMPLER_DESC smp{};
        smp.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
        smp.AddressU = smp.AddressV = smp.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
        _device->CreateSamplerState(&smp, &_sampler);
        _frame.pixels.assign(static_cast<size_t>(kWidth) * kHeight, 0);

        if (_o.variant == "d")
            _producer.StartTicked();
        else
            _producer.StartClocked();
        _stop = false;
        _poller = std::thread([this] { Poll(); });
        _vblanks = std::make_unique<VBlankRecorder>(_hwnd);
        _thread = std::thread([this] { _o.variant == "c" ? RunTimed() : RunWaitable(); });
        return true;
    }

    void Stop() override
    {
        if (_stop.exchange(true))
            return;
        if (_thread.joinable())
            _thread.join();
        if (_exclusive)
            _swap->SetFullscreenState(FALSE, nullptr);
        _producer.Stop();
        SleepUntilNs(NowNs() + 100000000);
        _pollStop = true;
        if (_poller.joinable())
            _poller.join();
        // DXGI frame statistics are empty for a composed (DWM) swap chain: the display's vblanks give the screen time
        if (_vblanks)
            _vblanks->Stop(_log);
        char hr[16];
        std::snprintf(hr, sizeof hr, "0x%08lX", static_cast<unsigned long>(_statsHr));
        _log.Meta("dxgi_stats_ok", std::to_string(_statsOk));
        _log.Meta("dxgi_stats_last_error", hr);
        UINT last = 0;
        _swap->GetLastPresentCount(&last);
        _log.Meta("dxgi_stats_sample", "PresentCount " + std::to_string(_lastStats.PresentCount) + " PresentRefreshCount " +
                                           std::to_string(_lastStats.PresentRefreshCount) + " SyncRefreshCount " +
                                           std::to_string(_lastStats.SyncRefreshCount) + " SyncQPCTime " +
                                           std::to_string(_lastStats.SyncQPCTime.QuadPart) + " LastPresentCount " +
                                           std::to_string(last));
    }

    std::string Describe() const override
    {
        std::string s = std::string("D3D11 flip-discard, ") + (_exclusive ? "exclusive fullscreen, " : "") + "waitable, max latency 1, tearing ";
        s += _tearing ? "supported, " : "not supported, ";
        if (_o.variant == "b")
            s += "waitable object per refresh, Present(1), newest frame";
        else if (_o.variant == "d")
            s += "waitable object per refresh ticks the producer, Present(1)";
        else
            s += "each frame at latch + period + delay, Present(0, ALLOW_TEARING)";
        return s;
    }

private:
    void Upload()
    {
        if (_frame.number == _uploaded)
            return;
        _context->UpdateSubresource(_tex.Get(), 0, nullptr, _frame.pixels.data(), kWidth * 4, 0);
        _uploaded = _frame.number;
    }

    void Resize()
    {
        RECT rc;
        GetClientRect(_hwnd, &rc);
        const UINT w = std::max<LONG>(1, rc.right - rc.left), h = std::max<LONG>(1, rc.bottom - rc.top);
        if (w == _w && h == _h && _rtv)
            return;
        _rtv.Reset();
        _swap->ResizeBuffers(0, w, h, DXGI_FORMAT_UNKNOWN, _swapFlags);
        ComPtr<ID3D11Texture2D> back;
        _swap->GetBuffer(0, IID_PPV_ARGS(&back));
        _device->CreateRenderTargetView(back.Get(), nullptr, &_rtv);
        _w = w;
        _h = h;
    }

    void Draw()
    {
        Resize();
        const float black[4] = {0, 0, 0, 1};
        _context->OMSetRenderTargets(1, _rtv.GetAddressOf(), nullptr);
        _context->ClearRenderTargetView(_rtv.Get(), black);
        double s = std::min(static_cast<double>(_w) / kWidth, static_cast<double>(_h) / kHeight);
        if (s >= 1)
            s = std::floor(s);
        D3D11_VIEWPORT vp{static_cast<float>((_w - kWidth * s) / 2), static_cast<float>((_h - kHeight * s) / 2),
                          static_cast<float>(kWidth * s), static_cast<float>(kHeight * s), 0, 1};
        _context->RSSetViewports(1, &vp);
        _context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        _context->VSSetShader(_vs.Get(), nullptr, 0);
        _context->PSSetShader(_ps.Get(), nullptr, 0);
        _context->PSSetShaderResources(0, 1, _srv.GetAddressOf());
        _context->PSSetSamplers(0, 1, _sampler.GetAddressOf());
        _context->Draw(3, 0);
    }

    void PresentFrame(int64_t tick, int64_t target, int64_t cpu0, UINT interval, UINT flags)
    {
        Draw();
        PresentRecord r;
        r.frame = _frame.number;
        r.latchNs = _frame.latchNs;
        r.tickNs = tick;
        r.targetNs = target;
        const size_t id = _log.Add(r);
        _swap->Present(interval, flags);
        UINT count = 0;
        _swap->GetLastPresentCount(&count);
        {
            std::lock_guard<std::mutex> lock(_mapMutex);
            _pending[count] = id;
        }
        _log.SetTiming(id, NowNs(), ThreadCpuNs() - cpu0);
    }

    void RunWaitable()
    {
        RaiseThreadPriority(false);
        uint64_t shown = UINT64_MAX;
        // The waitable object is signalled once at the start and then once per present: consume it only with a frame
        // to present, or it never comes back
        if (_o.variant != "d")
            while (!_stop && !_ring.WaitNewer(UINT64_MAX, NowNs() + 100000000))
            {
            }
        while (!_stop)
        {
            if (WaitForSingleObjectEx(_waitable, 1000, TRUE) != WAIT_OBJECT_0)
                continue;
            const int64_t tick = NowNs();
            const int64_t cpu0 = ThreadCpuNs();
            if (_o.variant == "d")
            {
                _producer.Tick();
                _ring.WaitNewer(shown, tick + static_cast<int64_t>(1e9 / 60 * 0.6));
            }
            if (!_ring.CopyAt(0, _frame))
                continue;
            shown = _frame.number;
            Upload();
            PresentFrame(tick, 0, cpu0, 1, 0);
        }
    }

    void RunTimed()
    {
        RaiseThreadPriority(false);
        const int64_t period = static_cast<int64_t>(1e9 / _o.rate);
        uint64_t last = UINT64_MAX;
        while (!_stop)
        {
            if (!_ring.WaitNewer(last, NowNs() + 100000000))
                continue;
            if (!_ring.CopyAt(0, _frame))
                continue;
            last = _frame.number;
            // Present at latch + one period (+ delay): the latch jitter is absorbed, the cadence is the producer's
            const int64_t target = _frame.latchNs + period + static_cast<int64_t>(_o.delayMs * 1e6);
            WaitForSingleObjectEx(_waitable, 100, TRUE);
            const int64_t cpu0 = ThreadCpuNs();
            Upload();
            Draw();
            SleepUntilNs(target - 300000);  // the present call itself takes ~0.3 ms
            const int64_t tick = NowNs();
            PresentFrame(tick, target, cpu0, 0, _tearing ? DXGI_PRESENT_ALLOW_TEARING : 0);
        }
    }

    void Poll()
    {
        RaiseThreadPriority(false);
        UINT lastCount = 0;
        while (!_pollStop)
        {
            DXGI_FRAME_STATISTICS st{};
            const HRESULT hr = _swap->GetFrameStatistics(&st);
            if (SUCCEEDED(hr))
            {
                _statsOk++;
                _lastStats = st;
            }
            else
                _statsHr = hr;
            if (SUCCEEDED(hr) && st.PresentCount != lastCount)
            {
                lastCount = st.PresentCount;
                std::lock_guard<std::mutex> lock(_mapMutex);
                // Every pending present up to this count has been shown or replaced; this one is on screen now
                for (auto it = _pending.begin(); it != _pending.end() && it->first <= st.PresentCount;)
                {
                    _log.SetPresented(it->second, it->first == st.PresentCount ? QpcToNs(st.SyncQPCTime.QuadPart) : 0);
                    it = _pending.erase(it);
                }
            }
            SleepUntilNs(NowNs() + 500000);
        }
    }

    HWND _hwnd;
    FrameRing& _ring;
    Producer& _producer;
    PresentLog& _log;
    Options _o;
    ComPtr<ID3D11Device> _device;
    ComPtr<ID3D11DeviceContext> _context;
    ComPtr<IDXGISwapChain2> _swap;
    ComPtr<ID3D11RenderTargetView> _rtv;
    ComPtr<ID3D11VertexShader> _vs;
    ComPtr<ID3D11PixelShader> _ps;
    ComPtr<ID3D11Texture2D> _tex;
    ComPtr<ID3D11ShaderResourceView> _srv;
    ComPtr<ID3D11SamplerState> _sampler;
    HANDLE _waitable = nullptr;
    UINT _swapFlags = 0;
    UINT _w = 0, _h = 0;
    bool _tearing = false;
    bool _exclusive = false;
    Frame _frame;
    uint64_t _uploaded = UINT64_MAX;
    std::thread _thread, _poller;
    std::unique_ptr<VBlankRecorder> _vblanks;
    std::atomic<bool> _stop{true}, _pollStop{false};
    std::mutex _mapMutex;
    std::map<UINT, size_t> _pending;
    uint64_t _statsOk = 0;
    HRESULT _statsHr = S_OK;
    DXGI_FRAME_STATISTICS _lastStats{};
};

std::unique_ptr<NativePresenter> CreateNativePresenter(void* handle, FrameRing& ring, Producer& producer, PresentLog& log,
                                                       const Options& options)
{
    return std::make_unique<DxgiPresenter>(static_cast<HWND>(handle), ring, producer, log, options);
}

// region <VBlankRecorder>

namespace
{
// d3dkmthk.h is a WDK header; the two calls need only these
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

std::string Percentiles(const std::vector<int64_t>& t)
{
    std::vector<double> gaps;
    for (size_t i = 1; i < t.size(); i++)
        gaps.push_back(static_cast<double>(t[i] - t[i - 1]) / 1e6);
    std::sort(gaps.begin(), gaps.end());
    auto at = [&](double p) { return gaps.empty() ? 0.0 : gaps[static_cast<size_t>(p * static_cast<double>(gaps.size() - 1))]; };
    char buf[96];
    std::snprintf(buf, sizeof buf, "%.3f %.3f %.3f", at(0.01), at(0.5), at(0.99));
    return buf;
}
}  // namespace

VBlankRecorder::VBlankRecorder(void* hwnd)
{
    _vblanks.reserve(100000);
    _dwmVBlanks.reserve(100000);
    _kernel = std::thread([this, hwnd] { Run(hwnd); });
    _dwm = std::thread([this] {
        RaiseThreadPriority(false);
        UINT64 last = 0;
        while (!_stop)
        {
            DWM_TIMING_INFO ti{};
            ti.cbSize = sizeof ti;
            if (SUCCEEDED(DwmGetCompositionTimingInfo(nullptr, &ti)) && ti.qpcVBlank != last)
            {
                last = ti.qpcVBlank;
                _dwmVBlanks.push_back(QpcToNs(static_cast<int64_t>(ti.qpcVBlank)));
            }
            SleepUntilNs(NowNs() + 250000);
        }
    });
}

VBlankRecorder::~VBlankRecorder()
{
    _stop = true;
    if (_kernel.joinable())
        _kernel.join();
    if (_dwm.joinable())
        _dwm.join();
}

void VBlankRecorder::Run(void* hwnd)
{
    RaiseThreadPriority(false);
    HMODULE gdi = GetModuleHandleA("gdi32.dll");
    auto open = reinterpret_cast<OpenAdapterFn>(reinterpret_cast<void*>(GetProcAddress(gdi, "D3DKMTOpenAdapterFromHdc")));
    auto wait = reinterpret_cast<WaitVBlankFn>(reinterpret_cast<void*>(GetProcAddress(gdi, "D3DKMTWaitForVerticalBlankEvent")));
    auto close = reinterpret_cast<CloseAdapterFn>(reinterpret_cast<void*>(GetProcAddress(gdi, "D3DKMTCloseAdapter")));
    if (!open || !wait || !close)
    {
        _error = "no D3DKMT entry points";
        return;
    }
    MONITORINFOEXA mi{};
    mi.cbSize = sizeof mi;
    GetMonitorInfoA(MonitorFromWindow(static_cast<HWND>(hwnd), MONITOR_DEFAULTTONEAREST), &mi);
    HDC dc = CreateDCA(nullptr, mi.szDevice, nullptr, nullptr);
    D3dkmtOpenAdapterFromHdc oa{};
    oa.hDc = dc;
    const LONG st = open(&oa);
    DeleteDC(dc);
    if (st != 0)
    {
        char buf[64];
        std::snprintf(buf, sizeof buf, "D3DKMTOpenAdapterFromHdc 0x%08lX", static_cast<unsigned long>(st));
        _error = buf;
        return;
    }
    D3dkmtWaitForVerticalBlankEvent w{};
    w.hAdapter = oa.hAdapter;
    w.VidPnSourceId = oa.VidPnSourceId;
    while (!_stop)
    {
        if (const LONG status = wait(&w); status != 0)
        {
            char buf[64];
            std::snprintf(buf, sizeof buf, "D3DKMTWaitForVerticalBlankEvent 0x%08lX", static_cast<unsigned long>(status));
            _error = buf;
            break;
        }
        _vblanks.push_back(NowNs());
    }
    D3dkmtCloseAdapter ca{oa.hAdapter};
    close(&ca);
}

void VBlankRecorder::Stop(PresentLog& log)
{
    _stop = true;
    if (_kernel.joinable())
        _kernel.join();
    if (_dwm.joinable())
        _dwm.join();
    log.ResolveByVBlanks(_vblanks);
    log.Meta("presented_source", "D3DKMTWaitForVerticalBlankEvent: the first vblank after the present returned");
    log.Meta("vblanks", std::to_string(_vblanks.size()) + (_error.empty() ? "" : " (" + _error + ")"));
    log.Meta("vblank_interval_ms_p1_p50_p99", Percentiles(_vblanks));
    log.Meta("dwm_vblanks", std::to_string(_dwmVBlanks.size()));
    log.Meta("dwm_vblank_interval_ms_p1_p50_p99", Percentiles(_dwmVBlanks));
}

// endregion </VBlankRecorder>

DisplayFacts QueryDisplay(void* handle)
{
    DisplayFacts f;
    HMONITOR mon = MonitorFromWindow(static_cast<HWND>(handle), MONITOR_DEFAULTTONEAREST);
    MONITORINFOEXA mi{};
    mi.cbSize = sizeof mi;
    GetMonitorInfoA(mon, &mi);
    f.name = mi.szDevice;
    DEVMODEA dm{};
    dm.dmSize = sizeof dm;
    if (EnumDisplaySettingsA(mi.szDevice, ENUM_CURRENT_SETTINGS, &dm))
        f.refreshHz = f.maxHz = f.minHz = dm.dmDisplayFrequency;
    return f;
}

}  // namespace poc037
