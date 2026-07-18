#include "qsv_encoder.h"

#ifdef _WIN32

#include "qsv_probe.h"
#include "mfxdefs.h"
#include "mp4_muxer.h"
#include "emulator/video/screen.h"

#include <windows.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <vector>
#include <cstring>
#include <fstream>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")

// Function pointer types for MFX API
typedef mfxStatus (*MFXInitExFn)(mfxInitParam par, mfxSession* session);
typedef mfxStatus (*MFXCloseFn)(mfxSession session);
typedef mfxStatus (*MFXQueryIMPLFn)(mfxSession session, mfxIMPL* impl);
typedef mfxStatus (*MFXQueryVersionFn)(mfxSession session, mfxVersion* version);

typedef mfxStatus (*MFXVideoCORE_SetHandleFn)(mfxSession session, int type, mfxHDL hdl);
typedef mfxStatus (*MFXVideoCORE_SyncOperationFn)(mfxSession session, mfxSyncPoint syncp, mfxU32 wait);

typedef mfxStatus (*MFXVideoENCODE_QueryFn)(mfxSession session, mfxVideoParam* in, mfxVideoParam* out);
typedef mfxStatus (*MFXVideoENCODE_QueryIOSurfFn)(mfxSession session, mfxVideoParam* par, mfxFrameAllocRequest* request);
typedef mfxStatus (*MFXVideoENCODE_InitFn)(mfxSession session, mfxVideoParam* par);
typedef mfxStatus (*MFXVideoENCODE_GetVideoParamFn)(mfxSession session, mfxVideoParam* par);
typedef mfxStatus (*MFXVideoENCODE_EncodeFrameAsyncFn)(mfxSession session, mfxEncodeCtrl* ctrl,
                                                       mfxFrameSurface1* surface, mfxBitstream* bs,
                                                       mfxSyncPoint* syncp);
typedef mfxStatus (*MFXVideoENCODE_CloseFn)(mfxSession session);

// MFX handle types
#define MFX_HANDLE_D3D11_DEVICE  4

struct QsvEncoder::Impl
{
    ~Impl() { cleanup(); }

    bool init(const std::string& filename, const EncoderConfig& config);
    bool encode(const FramebufferDescriptor& framebuffer, double timestampSec);
    bool finish();

    uint64_t getFileSize() const;

private:
    bool loadLibrary();
    bool createD3D11Device();
    bool initSession();
    bool initEncoder();
    void allocateSurfaces();
    void cleanup();
    int getSurfaceIndex();
    bool drainEncoder();

    // Library handle
    HMODULE _lib = nullptr;

    // Function pointers
    MFXInitExFn MFXInitEx = nullptr;
    MFXCloseFn MFXClose = nullptr;
    MFXVideoCORE_SetHandleFn MFXVideoCORE_SetHandle = nullptr;
    MFXVideoCORE_SyncOperationFn MFXVideoCORE_SyncOperation = nullptr;
    MFXVideoENCODE_QueryFn MFXVideoENCODE_Query = nullptr;
    MFXVideoENCODE_QueryIOSurfFn MFXVideoENCODE_QueryIOSurf = nullptr;
    MFXVideoENCODE_InitFn MFXVideoENCODE_Init = nullptr;
    MFXVideoENCODE_GetVideoParamFn MFXVideoENCODE_GetVideoParam = nullptr;
    MFXVideoENCODE_EncodeFrameAsyncFn MFXVideoENCODE_EncodeFrameAsync = nullptr;
    MFXVideoENCODE_CloseFn MFXVideoENCODE_Close = nullptr;

    // D3D11
    ID3D11Device* _device = nullptr;
    ID3D11DeviceContext* _context = nullptr;

    // MFX session
    mfxSession _session = nullptr;
    mfxVideoParam _params{};

    // Surfaces
    std::vector<mfxFrameSurface1> _surfaces;
    std::vector<std::vector<uint8_t>> _surfaceData;
    int _numSurfaces = 0;

    // Bitstream buffer
    mfxBitstream _bitstream{};
    std::vector<uint8_t> _bitstreamData;

    // Muxer
    Mp4Muxer _muxer;
    bool _isHevc = false;

    // Config
    uint32_t _width = 0;
    uint32_t _height = 0;
    uint32_t _fps = 50;
    int _qualityPreset = 5;
    int _frameNum = 0;
    std::string _outputFilename;
};

bool QsvEncoder::Impl::loadLibrary()
{
    _lib = LoadLibraryA("libvpl.dll");
    if (!_lib) _lib = LoadLibraryA("libmfx-gen.dll");
    if (!_lib) _lib = LoadLibraryA("libmfxhw64.dll");
    if (!_lib) _lib = LoadLibraryA("mfxhw64.dll");

    if (!_lib)
        return false;

    MFXInitEx = (MFXInitExFn)GetProcAddress(_lib, "MFXInitEx");
    MFXClose = (MFXCloseFn)GetProcAddress(_lib, "MFXClose");
    MFXVideoCORE_SetHandle = (MFXVideoCORE_SetHandleFn)GetProcAddress(_lib, "MFXVideoCORE_SetHandle");
    MFXVideoCORE_SyncOperation = (MFXVideoCORE_SyncOperationFn)GetProcAddress(_lib, "MFXVideoCORE_SyncOperation");
    MFXVideoENCODE_Query = (MFXVideoENCODE_QueryFn)GetProcAddress(_lib, "MFXVideoENCODE_Query");
    MFXVideoENCODE_QueryIOSurf = (MFXVideoENCODE_QueryIOSurfFn)GetProcAddress(_lib, "MFXVideoENCODE_QueryIOSurf");
    MFXVideoENCODE_Init = (MFXVideoENCODE_InitFn)GetProcAddress(_lib, "MFXVideoENCODE_Init");
    MFXVideoENCODE_GetVideoParam = (MFXVideoENCODE_GetVideoParamFn)GetProcAddress(_lib, "MFXVideoENCODE_GetVideoParam");
    MFXVideoENCODE_EncodeFrameAsync = (MFXVideoENCODE_EncodeFrameAsyncFn)GetProcAddress(_lib, "MFXVideoENCODE_EncodeFrameAsync");
    MFXVideoENCODE_Close = (MFXVideoENCODE_CloseFn)GetProcAddress(_lib, "MFXVideoENCODE_Close");

    // Try legacy MFXInit if MFXInitEx not available
    if (!MFXInitEx)
    {
        typedef mfxStatus (*MFXInitFn)(mfxIMPL impl, mfxVersion* ver, mfxSession* session);
        auto MFXInit = (MFXInitFn)GetProcAddress(_lib, "MFXInit");
        if (MFXInit)
        {
            // Store for later use
            static MFXInitFn storedMFXInit = MFXInit;
            MFXInitEx = [](mfxInitParam par, mfxSession* session) -> mfxStatus {
                return storedMFXInit(par.Implementation, &par.Version, session);
            };
        }
    }

    return MFXInitEx && MFXClose && MFXVideoCORE_SyncOperation &&
           MFXVideoENCODE_Init && MFXVideoENCODE_EncodeFrameAsync && MFXVideoENCODE_Close;
}

bool QsvEncoder::Impl::createD3D11Device()
{
    IDXGIFactory1* factory = nullptr;
    if (FAILED(CreateDXGIFactory1(__uuidof(IDXGIFactory1), (void**)&factory)))
        return false;

    IDXGIAdapter1* intelAdapter = nullptr;
    IDXGIAdapter1* adapter = nullptr;
    for (UINT i = 0; factory->EnumAdapters1(i, &adapter) != DXGI_ERROR_NOT_FOUND; ++i)
    {
        DXGI_ADAPTER_DESC1 desc;
        if (SUCCEEDED(adapter->GetDesc1(&desc)) && desc.VendorId == 0x8086)
        {
            intelAdapter = adapter;
            break;
        }
        adapter->Release();
    }
    factory->Release();

    if (!intelAdapter)
        return false;

    D3D_FEATURE_LEVEL featureLevels[] = { D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0 };
    D3D_FEATURE_LEVEL actualLevel;

    HRESULT hr = D3D11CreateDevice(
        intelAdapter,
        D3D_DRIVER_TYPE_UNKNOWN,
        nullptr,
        D3D11_CREATE_DEVICE_VIDEO_SUPPORT,
        featureLevels,
        2,
        D3D11_SDK_VERSION,
        &_device,
        &actualLevel,
        &_context
    );

    intelAdapter->Release();
    return SUCCEEDED(hr);
}

bool QsvEncoder::Impl::initSession()
{
    mfxInitParam initPar{};
    initPar.Implementation = MFX_IMPL_HARDWARE | MFX_IMPL_VIA_D3D11;
    initPar.Version.Major = 1;
    initPar.Version.Minor = 0;

    mfxStatus sts = MFXInitEx(initPar, &_session);
    if (sts != MFX_ERR_NONE)
    {
        initPar.Implementation = MFX_IMPL_HARDWARE;
        sts = MFXInitEx(initPar, &_session);
    }

    if (sts != MFX_ERR_NONE || !_session)
        return false;

    if (MFXVideoCORE_SetHandle && _device)
        MFXVideoCORE_SetHandle(_session, MFX_HANDLE_D3D11_DEVICE, _device);

    return true;
}

bool QsvEncoder::Impl::initEncoder()
{
    memset(&_params, 0, sizeof(_params));

    _params.mfx.FrameInfo.FourCC = MFX_FOURCC_NV12;
    _params.mfx.FrameInfo.ChromaFormat = MFX_CHROMAFORMAT_YUV420;
    _params.mfx.FrameInfo.PicStruct = MFX_PICSTRUCT_PROGRESSIVE;

    _params.mfx.FrameInfo.Width = (_width + 15) & ~15;
    _params.mfx.FrameInfo.Height = (_height + 15) & ~15;
    _params.mfx.FrameInfo.CropW = _width;
    _params.mfx.FrameInfo.CropH = _height;

    _params.mfx.FrameInfo.FrameRateExtN = _fps;
    _params.mfx.FrameInfo.FrameRateExtD = 1;

    _params.mfx.CodecId = _isHevc ? MFX_CODEC_HEVC : MFX_CODEC_AVC;
    _params.mfx.CodecProfile = _isHevc ? MFX_PROFILE_HEVC_MAIN : MFX_PROFILE_AVC_HIGH;

    _params.mfx.RateControlMethod = MFX_RATECONTROL_CQP;

    // Quality preset (1-7, lower = better quality)
    int targetUsage = 7 - (7 * _qualityPreset / 10);
    if (targetUsage < 1) targetUsage = 1;
    if (targetUsage > 7) targetUsage = 7;
    _params.mfx.TargetUsage = targetUsage;

    // QP values
    int qp = 28 - _qualityPreset * 2;
    if (qp < 8) qp = 8;
    if (qp > 51) qp = 51;
    _params.mfx.QPI = qp;
    _params.mfx.QPP = qp + 2;
    _params.mfx.QPB = qp + 4;

    _params.mfx.GopPicSize = _fps * 2;
    _params.mfx.GopRefDist = 4;
    _params.mfx.NumRefFrame = 2;

    _params.IOPattern = MFX_IOPATTERN_IN_SYSTEM_MEMORY;
    _params.AsyncDepth = 4;

    if (MFXVideoENCODE_Query)
    {
        mfxVideoParam queryParams = _params;
        mfxStatus sts = MFXVideoENCODE_Query(_session, &_params, &queryParams);
        if (sts >= MFX_ERR_NONE)
            _params = queryParams;
    }

    mfxStatus sts = MFXVideoENCODE_Init(_session, &_params);
    if (sts < MFX_ERR_NONE)
        return false;

    if (MFXVideoENCODE_GetVideoParam)
        MFXVideoENCODE_GetVideoParam(_session, &_params);

    return true;
}

void QsvEncoder::Impl::allocateSurfaces()
{
    mfxFrameAllocRequest request{};
    if (MFXVideoENCODE_QueryIOSurf)
        MFXVideoENCODE_QueryIOSurf(_session, &_params, &request);

    _numSurfaces = request.NumFrameSuggested > 0 ? request.NumFrameSuggested + 4 : 8;

    uint32_t alignedW = (_width + 15) & ~15;
    uint32_t alignedH = (_height + 15) & ~15;
    size_t surfaceSize = alignedW * alignedH * 3 / 2;

    _surfaces.resize(_numSurfaces);
    _surfaceData.resize(_numSurfaces);

    for (int i = 0; i < _numSurfaces; ++i)
    {
        _surfaceData[i].resize(surfaceSize);

        memset(&_surfaces[i], 0, sizeof(mfxFrameSurface1));
        _surfaces[i].Info = _params.mfx.FrameInfo;
        _surfaces[i].Data.Y = _surfaceData[i].data();
        _surfaces[i].Data.UV = _surfaceData[i].data() + alignedW * alignedH;
        _surfaces[i].Data.Pitch = alignedW;
    }

    _bitstreamData.resize(_width * _height * 2);
    memset(&_bitstream, 0, sizeof(_bitstream));
    _bitstream.Data = _bitstreamData.data();
    _bitstream.MaxLength = (mfxU32)_bitstreamData.size();
}

int QsvEncoder::Impl::getSurfaceIndex()
{
    for (int i = 0; i < _numSurfaces; ++i)
    {
        if (_surfaces[i].Data.Locked == 0)
            return i;
    }
    return -1;
}

bool QsvEncoder::Impl::init(const std::string& filename, const EncoderConfig& config)
{
    _outputFilename = filename;
    _width = config.videoWidth;
    _height = config.videoHeight;
    _fps = static_cast<uint32_t>(config.frameRate);
    _qualityPreset = config.qualityPreset;

    std::string codec = config.videoCodec;
    for (auto& c : codec) c = static_cast<char>(tolower(c));
    _isHevc = (codec == "hevc" || codec == "h265" || codec == "h.265");

    if (!loadLibrary())
        return false;

    if (!createD3D11Device())
        return false;

    if (!initSession())
        return false;

    if (!initEncoder())
        return false;

    allocateSurfaces();

    auto muxCodec = _isHevc ? Mp4Muxer::Codec::HEVC : Mp4Muxer::Codec::H264;
    if (!_muxer.open(filename, muxCodec, _width, _height, _fps))
        return false;

    return true;
}

bool QsvEncoder::Impl::encode(const FramebufferDescriptor& framebuffer, double timestampSec)
{
    (void)timestampSec;

    int idx = getSurfaceIndex();
    if (idx < 0)
    {
        drainEncoder();
        idx = getSurfaceIndex();
        if (idx < 0)
            return false;
    }

    // Convert RGBA to NV12
    const uint8_t* rgba = framebuffer.memoryBuffer;
    uint32_t srcWidth = framebuffer.width;
    uint32_t srcHeight = framebuffer.height;
    uint32_t alignedW = (_width + 15) & ~15;

    uint8_t* Y = _surfaces[idx].Data.Y;
    uint8_t* UV = _surfaces[idx].Data.UV;

    for (uint32_t y = 0; y < _height && y < srcHeight; ++y)
    {
        for (uint32_t x = 0; x < _width && x < srcWidth; ++x)
        {
            const uint8_t* pixel = rgba + (y * srcWidth + x) * 4;
            uint8_t r = pixel[0];
            uint8_t g = pixel[1];
            uint8_t b = pixel[2];

            int yVal = ((66 * r + 129 * g + 25 * b + 128) >> 8) + 16;
            Y[y * alignedW + x] = (uint8_t)(yVal < 0 ? 0 : (yVal > 255 ? 255 : yVal));

            if ((x & 1) == 0 && (y & 1) == 0)
            {
                int u = ((-38 * r - 74 * g + 112 * b + 128) >> 8) + 128;
                int v = ((112 * r - 94 * g - 18 * b + 128) >> 8) + 128;
                UV[(y / 2) * alignedW + x] = (uint8_t)(u < 0 ? 0 : (u > 255 ? 255 : u));
                UV[(y / 2) * alignedW + x + 1] = (uint8_t)(v < 0 ? 0 : (v > 255 ? 255 : v));
            }
        }
    }

    _surfaces[idx].Data.TimeStamp = _frameNum * 90000 / _fps;
    _surfaces[idx].Data.FrameOrder = _frameNum;
    _frameNum++;

    mfxSyncPoint syncp = nullptr;
    _bitstream.DataOffset = 0;
    _bitstream.DataLength = 0;

    mfxStatus sts = MFXVideoENCODE_EncodeFrameAsync(_session, nullptr, &_surfaces[idx], &_bitstream, &syncp);

    if (sts == MFX_ERR_MORE_DATA)
        return true;

    if (sts == MFX_WRN_DEVICE_BUSY)
    {
        Sleep(1);
        sts = MFXVideoENCODE_EncodeFrameAsync(_session, nullptr, &_surfaces[idx], &_bitstream, &syncp);
    }

    if (sts < MFX_ERR_NONE)
        return false;

    if (syncp)
    {
        sts = MFXVideoCORE_SyncOperation(_session, syncp, 60000);
        if (sts == MFX_ERR_NONE && _bitstream.DataLength > 0)
        {
            bool keyframe = (_bitstream.FrameType & MFX_FRAMETYPE_IDR) ||
                           (_bitstream.FrameType & MFX_FRAMETYPE_I);

            int64_t pts = _bitstream.TimeStamp * 1000 / 90000;
            _muxer.writeFrame(_bitstream.Data + _bitstream.DataOffset,
                             _bitstream.DataLength, keyframe, pts);
        }
    }

    return true;
}

bool QsvEncoder::Impl::drainEncoder()
{
    mfxSyncPoint syncp = nullptr;
    _bitstream.DataOffset = 0;
    _bitstream.DataLength = 0;

    mfxStatus sts = MFXVideoENCODE_EncodeFrameAsync(_session, nullptr, nullptr, &_bitstream, &syncp);

    if (sts == MFX_ERR_MORE_DATA)
        return false;

    if (syncp)
    {
        sts = MFXVideoCORE_SyncOperation(_session, syncp, 60000);
        if (sts == MFX_ERR_NONE && _bitstream.DataLength > 0)
        {
            bool keyframe = (_bitstream.FrameType & MFX_FRAMETYPE_IDR) ||
                           (_bitstream.FrameType & MFX_FRAMETYPE_I);

            int64_t pts = _bitstream.TimeStamp * 1000 / 90000;
            _muxer.writeFrame(_bitstream.Data + _bitstream.DataOffset,
                             _bitstream.DataLength, keyframe, pts);
            return true;
        }
    }

    return false;
}

bool QsvEncoder::Impl::finish()
{
    while (drainEncoder())
        ;

    _muxer.close();
    return true;
}

uint64_t QsvEncoder::Impl::getFileSize() const
{
    std::ifstream file(_outputFilename, std::ios::binary | std::ios::ate);
    return file.good() ? static_cast<uint64_t>(file.tellg()) : 0;
}

void QsvEncoder::Impl::cleanup()
{
    if (_session)
    {
        if (MFXVideoENCODE_Close)
            MFXVideoENCODE_Close(_session);
        if (MFXClose)
            MFXClose(_session);
        _session = nullptr;
    }

    if (_context)
    {
        _context->Release();
        _context = nullptr;
    }

    if (_device)
    {
        _device->Release();
        _device = nullptr;
    }

    if (_lib)
    {
        FreeLibrary(_lib);
        _lib = nullptr;
    }

    _surfaces.clear();
    _surfaceData.clear();
}

// Public interface

QsvEncoder::QsvEncoder() : _impl(std::make_unique<Impl>()) {}
QsvEncoder::~QsvEncoder() = default;

bool QsvEncoder::Start(const std::string& filename, const EncoderConfig& config)
{
    if (_isRecording)
        return false;

    _outputFilename = filename;

    if (!_impl->init(filename, config))
    {
        _lastError = "Failed to initialize QSV encoder";
        return false;
    }

    _isRecording = true;
    _framesEncoded = 0;
    return true;
}

void QsvEncoder::Stop()
{
    if (!_isRecording)
        return;

    _impl->finish();
    _isRecording = false;
}

void QsvEncoder::OnVideoFrame(const FramebufferDescriptor& framebuffer, double timestampSec)
{
    if (!_isRecording)
        return;

    if (_impl->encode(framebuffer, timestampSec))
        _framesEncoded++;
}

void QsvEncoder::OnAudioSamples(const int16_t* samples, size_t sampleCount, double timestampSec)
{
    (void)samples;
    (void)sampleCount;
    (void)timestampSec;
}

uint64_t QsvEncoder::GetOutputFileSize() const
{
    return _impl ? _impl->getFileSize() : 0;
}

#endif // _WIN32
