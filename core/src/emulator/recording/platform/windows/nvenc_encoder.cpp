#include "nvenc_encoder.h"
#include "mp4_muxer.h"
#include "mf_aac_encoder.h"
#include "emulator/video/screen.h"

#ifdef _WIN32

#include <windows.h>
#include <d3d11.h>
#include <dxgi.h>
#include <cstring>
#include <algorithm>
#include <mutex>

// Official FFmpeg nv-codec-headers
#include "nvEncodeAPI.h"

// Suppress GCC warning for standard Windows GetProcAddress cast pattern
#if defined(__GNUC__) || defined(__clang__)
#define NVENC_GETPROC_CAST_BEGIN \
    _Pragma("GCC diagnostic push") \
    _Pragma("GCC diagnostic ignored \"-Wcast-function-type\"")
#define NVENC_GETPROC_CAST_END _Pragma("GCC diagnostic pop")
#else
#define NVENC_GETPROC_CAST_BEGIN
#define NVENC_GETPROC_CAST_END
#endif

// Zero-init NVENC struct and set version
template<typename T>
T nvencStruct(uint32_t version)
{
    T s;
    memset(&s, 0, sizeof(s));
    s.version = version;
    return s;
}

// ============================================================================
// Implementation
// ============================================================================

struct NvencEncoder::Impl
{
    // Resources
    HMODULE hNvenc = nullptr;
    ID3D11Device* d3dDevice = nullptr;
    ID3D11DeviceContext* d3dContext = nullptr;
    void* encoder = nullptr;
    NV_ENCODE_API_FUNCTION_LIST nvenc = {};
    Mp4Muxer muxer;

    // Audio encoder
    MfAacEncoder aacEncoder;
    bool hasAudio = false;
    std::mutex muxerMutex;

    // Buffer queue for B-frame encoding
    // Need at least (1 + numBFrames) buffer pairs for proper B-frame pipeline
    static constexpr int NUM_BUFFERS = 8;
    NV_ENC_INPUT_PTR inputBuffers[NUM_BUFFERS] = {};
    NV_ENC_OUTPUT_PTR outputBuffers[NUM_BUFFERS] = {};
    int encodeIdx = 0;  // Next buffer to encode into
    int outputIdx = 0;  // Next buffer to read output from
    int pendingFrames = 0;  // Frames submitted but not yet output

    // Encoding state
    // srcWidth/srcHeight = source framebuffer (before integer upscale)
    // width/height       = dimensions fed to NVENC (after upscale)
    uint32_t srcWidth = 0;
    uint32_t srcHeight = 0;
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t scaleFactor = 1;
    uint32_t pitch = 0;
    uint32_t frameIdx = 0;
    uint32_t fps = 50;
    bool useHevc = false;
    std::vector<uint8_t> nv12Buffer;

    // Initialization
    bool init(const std::string& filename, const EncoderConfig& config, std::string& error);
    void cleanup();
    void finalize();

    // Frame encoding
    bool encodeFrame(const uint8_t* rgba);
    void writeOutput();

    // Audio encoding
    void encodeAudio(const int16_t* samples, size_t sampleCount, int64_t pts);

private:
    bool loadNvencDll();
    bool createD3D11Device();
    bool openEncodeSession();
    bool initializeEncoder(const EncoderConfig& config);
    bool createBuffers();
    void convertRgbaToNv12(const uint8_t* rgba);
    void copyToInputBuffer(void* dst, uint32_t dstPitch);
};

bool NvencEncoder::Impl::loadNvencDll()
{
    hNvenc = LoadLibraryA("nvEncodeAPI64.dll");
    if (!hNvenc)
        hNvenc = LoadLibraryA("nvEncodeAPI.dll");
    if (!hNvenc)
        return false;

    // Check driver version
    NVENC_GETPROC_CAST_BEGIN
    auto pfnGetMaxVersion = reinterpret_cast<NVENCSTATUS(NVENCAPI*)(uint32_t*)>(
        GetProcAddress(hNvenc, "NvEncodeAPIGetMaxSupportedVersion"));
    NVENC_GETPROC_CAST_END

    uint32_t maxVersion = 0;
    if (!pfnGetMaxVersion || pfnGetMaxVersion(&maxVersion) != NV_ENC_SUCCESS)
        return false;

    uint32_t requiredVersion = (NVENCAPI_MAJOR_VERSION << 4) | NVENCAPI_MINOR_VERSION;
    if (maxVersion < requiredVersion)
        return false;

    // Get function list
    NVENC_GETPROC_CAST_BEGIN
    auto pfnCreateInstance = reinterpret_cast<NVENCSTATUS(NVENCAPI*)(NV_ENCODE_API_FUNCTION_LIST*)>(
        GetProcAddress(hNvenc, "NvEncodeAPICreateInstance"));
    NVENC_GETPROC_CAST_END

    if (!pfnCreateInstance)
        return false;

    nvenc.version = NV_ENCODE_API_FUNCTION_LIST_VER;
    return pfnCreateInstance(&nvenc) == NV_ENC_SUCCESS;
}

bool NvencEncoder::Impl::createD3D11Device()
{
    // Find NVIDIA adapter
    IDXGIFactory1* factory = nullptr;
    if (FAILED(CreateDXGIFactory1(__uuidof(IDXGIFactory1), (void**)&factory)))
        return false;

    IDXGIAdapter1* nvidiaAdapter = nullptr;
    IDXGIAdapter1* adapter = nullptr;
    for (UINT i = 0; factory->EnumAdapters1(i, &adapter) != DXGI_ERROR_NOT_FOUND; i++)
    {
        DXGI_ADAPTER_DESC1 desc;
        adapter->GetDesc1(&desc);
        if (desc.VendorId == 0x10DE && !nvidiaAdapter)  // NVIDIA
            nvidiaAdapter = adapter;
        else
            adapter->Release();
    }
    factory->Release();

    if (!nvidiaAdapter)
        return false;

    // Create D3D11 device
    D3D_FEATURE_LEVEL featureLevel;
    HRESULT hr = D3D11CreateDevice(nvidiaAdapter, D3D_DRIVER_TYPE_UNKNOWN, nullptr, 0,
                                    nullptr, 0, D3D11_SDK_VERSION,
                                    &d3dDevice, &featureLevel, &d3dContext);
    nvidiaAdapter->Release();
    return SUCCEEDED(hr) && d3dDevice;
}

bool NvencEncoder::Impl::openEncodeSession()
{
    auto params = nvencStruct<NV_ENC_OPEN_ENCODE_SESSION_EX_PARAMS>(NV_ENC_OPEN_ENCODE_SESSION_EX_PARAMS_VER);
    params.device = d3dDevice;
    params.deviceType = NV_ENC_DEVICE_TYPE_DIRECTX;
    params.apiVersion = NVENCAPI_VERSION;
    return nvenc.nvEncOpenEncodeSessionEx(&params, &encoder) == NV_ENC_SUCCESS;
}

bool NvencEncoder::Impl::initializeEncoder(const EncoderConfig& config)
{
    // Select codec based on config
    std::string codec = config.videoCodec;
    for (auto& ch : codec) ch = static_cast<char>(tolower(ch));
    useHevc = (codec == "h265" || codec == "h.265" || codec == "hevc");
    GUID codecGuid = useHevc ? NV_ENC_CODEC_HEVC_GUID : NV_ENC_CODEC_H264_GUID;
    fps = static_cast<uint32_t>(config.frameRate);

    // Map qualityPreset (0-10) to NVENC presets (P1-P7)
    // P1=fastest/lowest, P7=slowest/best.  Performance degrades and quality
    // improves as we move from P1 to P7.
    GUID presetGuid;
    int q = std::clamp(config.qualityPreset, 0, 10);
    if (q <= 2)
        presetGuid = NV_ENC_PRESET_P1_GUID;
    else if (q <= 4)
        presetGuid = NV_ENC_PRESET_P3_GUID;
    else if (q <= 6)
        presetGuid = NV_ENC_PRESET_P5_GUID;
    else if (q <= 8)
        presetGuid = NV_ENC_PRESET_P6_GUID;
    else
        presetGuid = NV_ENC_PRESET_P7_GUID;

    // Get preset config with HIGH_QUALITY tuning as a starting point.
    // We then override the problematic defaults (temporal filter, rate
    // control) that are designed for natural video, not pixel art.
    auto presetConfig = nvencStruct<NV_ENC_PRESET_CONFIG>(NV_ENC_PRESET_CONFIG_VER);
    presetConfig.presetCfg.version = NV_ENC_CONFIG_VER;

    if (nvenc.nvEncGetEncodePresetConfigEx(encoder, codecGuid, presetGuid,
                                            NV_ENC_TUNING_INFO_HIGH_QUALITY, &presetConfig) != NV_ENC_SUCCESS)
        return false;

    auto& encCfg = presetConfig.presetCfg;
    auto& rcParams = encCfg.rcParams;

    // --- Rate Control ---
    // VBR with target quality when explicit bitrate is provided; ConstQP
    // otherwise.  Both modes enable spatial AQ to preserve sharp pixel-art
    // edges in flat regions.
    rcParams.enableAQ = 1;
    rcParams.aqStrength = 8;  // Moderate AQ: protects text/edges without over-smoothing

    if (config.videoBitrate > 0)
    {
        // VBR mode respects the user-configured bitrate.
        rcParams.rateControlMode = NV_ENC_PARAMS_RC_VBR;
        rcParams.averageBitRate = config.videoBitrate * 1000;   // kbps -> bps
        rcParams.maxBitRate = config.videoBitrate * 1500;       // 1.5x headroom for peaks
        rcParams.vbvBufferSize = config.videoBitrate * 2000;   // 2x bitrate buffer
        rcParams.vbvInitialDelay = config.videoBitrate * 1000;
        // targetQuality is a VBR CQ hint (0-51, lower = better).
        rcParams.targetQuality = static_cast<uint8_t>(std::clamp(30 - q * 2, 8, 30));
        // Two-pass quarter-resolution analysis improves quality at cost of speed.
        if (q >= 6)
            rcParams.multiPass = NV_ENC_TWO_PASS_QUARTER_RESOLUTION;
    }
    else
    {
        // ConstQP mode: map quality 0-10 to QP 28-10.
        // Lower QP = better quality.  For pixel art we keep values conservative
        // to avoid smearing fine detail.
        rcParams.rateControlMode = NV_ENC_PARAMS_RC_CONSTQP;
        int qp = std::clamp(30 - q * 2, 8, 28);  // q=0 -> 28, q=10 -> 10
        rcParams.constQP.qpInterP = qp;
        rcParams.constQP.qpInterB = qp;
        rcParams.constQP.qpIntra = std::max(qp - 2, 1);
    }

    // Lookahead improves scene-cut detection and B-frame placement.
    // Enable for mid-to-high quality; disable for speed at low quality.
    if (q >= 5)
    {
        rcParams.enableLookahead = 1;
        rcParams.lookaheadDepth = static_cast<uint16_t>(std::min(fps, 8u));
    }

    // --- GOP Structure ---
    // For offline recording (not streaming) a 2-second GOP is a good
    // balance between compression efficiency and seekability.
    encCfg.gopLength = fps * 2;
    // frameIntervalP: 1=IP-only, 2=IBP, 3=IBBP.
    // B-frames improve compression but add latency (acceptable for recording).
    encCfg.frameIntervalP = (q >= 4) ? 3 : 1;

    // --- Profile & Codec-Specific Config ---
    if (useHevc)
    {
        encCfg.profileGUID = NV_ENC_HEVC_PROFILE_MAIN_GUID;

        auto& hevc = encCfg.encodeCodecConfig.hevcConfig;

        // CRITICAL: Disable temporal filtering.
        // The HIGH_QUALITY preset sets tfLevel=4 which blurs sharp pixel
        // edges across frames — the API docs say it's "recommended for
        // natural contents", i.e. the opposite of pixel art.
        hevc.tfLevel = NV_ENC_TEMPORAL_FILTER_LEVEL_0;

        // Main tier (0) is sufficient for our small resolution.
        // Level 0 = auto-select.
        hevc.tier = 0;
        hevc.level = 0;

        // Smallest CU size preserves fine pixel-art detail.
        hevc.minCUSize = NV_ENC_HEVC_CUSIZE_8x8;
        // maxCUSize 32x32 is the only supported value per NVENC SDK.
        hevc.maxCUSize = NV_ENC_HEVC_CUSIZE_32x32;

        // 4:2:0 chroma (matches NV12 input).
        hevc.chromaFormatIDC = 1;
    }
    else
    {
        encCfg.profileGUID = NV_ENC_H264_PROFILE_HIGH_GUID;

        auto& h264 = encCfg.encodeCodecConfig.h264Config;
        h264.chromaFormatIDC = 1;
    }

    // --- Initialize ---
    // width/height already include scaleFactor upscaling (set in init()).
    auto initParams = nvencStruct<NV_ENC_INITIALIZE_PARAMS>(NV_ENC_INITIALIZE_PARAMS_VER);
    initParams.encodeGUID = codecGuid;
    initParams.presetGUID = presetGuid;
    initParams.encodeWidth = width;
    initParams.encodeHeight = height;
    initParams.darWidth = width;
    initParams.darHeight = height;
    initParams.frameRateNum = static_cast<uint32_t>(config.frameRate);
    initParams.frameRateDen = 1;
    initParams.enablePTD = 1;
    initParams.encodeConfig = &encCfg;
    initParams.tuningInfo = NV_ENC_TUNING_INFO_HIGH_QUALITY;

    return nvenc.nvEncInitializeEncoder(encoder, &initParams) == NV_ENC_SUCCESS;
}

bool NvencEncoder::Impl::createBuffers()
{
    for (int i = 0; i < NUM_BUFFERS; i++)
    {
        auto inParams = nvencStruct<NV_ENC_CREATE_INPUT_BUFFER>(NV_ENC_CREATE_INPUT_BUFFER_VER);
        inParams.width = width;
        inParams.height = height;
        inParams.bufferFmt = NV_ENC_BUFFER_FORMAT_NV12;
        if (nvenc.nvEncCreateInputBuffer(encoder, &inParams) != NV_ENC_SUCCESS)
            return false;
        inputBuffers[i] = inParams.inputBuffer;

        auto outParams = nvencStruct<NV_ENC_CREATE_BITSTREAM_BUFFER>(NV_ENC_CREATE_BITSTREAM_BUFFER_VER);
        if (nvenc.nvEncCreateBitstreamBuffer(encoder, &outParams) != NV_ENC_SUCCESS)
            return false;
        outputBuffers[i] = outParams.bitstreamBuffer;
    }
    encodeIdx = 0;
    outputIdx = 0;
    pendingFrames = 0;
    return true;
}

bool NvencEncoder::Impl::init(const std::string& filename, const EncoderConfig& config, std::string& error)
{
    // Source dimensions come from the framebuffer; the encoder dimensions
    // are scaled up by scaleFactor (nearest-neighbor) to preserve per-pixel
    // ZX color detail through 4:2:0 chroma subsampling.
    srcWidth = config.videoWidth;
    srcHeight = config.videoHeight;
    scaleFactor = config.scaleFactor > 0 ? config.scaleFactor : 1;
    width = srcWidth * scaleFactor;
    height = srcHeight * scaleFactor;
    nv12Buffer.resize(width * height * 3 / 2);

    if (!loadNvencDll())         { error = "Failed to load NVENC"; return false; }
    if (!createD3D11Device())    { error = "Failed to create D3D11 device"; return false; }
    if (!openEncodeSession())    { error = "Failed to open encode session"; return false; }
    if (!initializeEncoder(config)) { error = "Failed to initialize encoder"; return false; }
    if (!createBuffers())        { error = "Failed to create buffers"; return false; }

    // Initialize AAC encoder
    uint32_t audioSampleRate = config.audioSampleRate > 0 ? config.audioSampleRate : 44100;
    uint32_t audioChannels = config.audioChannels > 0 ? config.audioChannels : 2;
    uint32_t audioBitrate = config.audioBitrate > 0 ? config.audioBitrate * 1000 : 128000;

    hasAudio = aacEncoder.init(audioSampleRate, audioChannels, audioBitrate);

    // Open MP4 muxer
    auto codecType = useHevc ? Mp4Muxer::Codec::HEVC : Mp4Muxer::Codec::H264;
    if (!muxer.open(filename, codecType, width, height, fps))
    {
        error = "Failed to open output file";
        return false;
    }

    // Enable audio track if AAC encoder initialized
    if (hasAudio)
    {
        muxer.enableAudio(audioSampleRate, audioChannels, aacEncoder.getAudioSpecificConfig());
    }

    return true;
}

void NvencEncoder::Impl::writeOutput()
{
    auto lockOut = nvencStruct<NV_ENC_LOCK_BITSTREAM>(NV_ENC_LOCK_BITSTREAM_VER);
    lockOut.outputBitstream = outputBuffers[outputIdx];

    if (nvenc.nvEncLockBitstream(encoder, &lockOut) == NV_ENC_SUCCESS)
    {
        bool keyframe = (lockOut.pictureType == NV_ENC_PIC_TYPE_IDR ||
                         lockOut.pictureType == NV_ENC_PIC_TYPE_I);
        int64_t pts = static_cast<int64_t>(lockOut.outputTimeStamp);

        std::lock_guard<std::mutex> lock(muxerMutex);
        muxer.writeVideoFrame(static_cast<const uint8_t*>(lockOut.bitstreamBufferPtr),
                              lockOut.bitstreamSizeInBytes, keyframe, pts);
        nvenc.nvEncUnlockBitstream(encoder, outputBuffers[outputIdx]);
    }

    outputIdx = (outputIdx + 1) % NUM_BUFFERS;
    pendingFrames--;
}

void NvencEncoder::Impl::encodeAudio(const int16_t* samples, size_t sampleCount, int64_t pts)
{
    if (!hasAudio) return;

    aacEncoder.encode(samples, sampleCount, pts,
        [this](const uint8_t* data, size_t size, int64_t audioPts) {
            std::lock_guard<std::mutex> lock(muxerMutex);
            muxer.writeAudioFrame(data, size, audioPts);
        });
}

void NvencEncoder::Impl::finalize()
{
    if (!encoder || !nvenc.nvEncEncodePicture) return;

    // Send EOS to flush encoder pipeline
    auto eos = nvencStruct<NV_ENC_PIC_PARAMS>(NV_ENC_PIC_PARAMS_VER);
    eos.encodePicFlags = NV_ENC_PIC_FLAG_EOS;
    nvenc.nvEncEncodePicture(encoder, &eos);

    // Drain remaining video frames (outputs come in order submitted)
    while (pendingFrames > 0)
        writeOutput();

    // Flush remaining audio frames
    if (hasAudio)
    {
        aacEncoder.flush([this](const uint8_t* data, size_t size, int64_t audioPts) {
            std::lock_guard<std::mutex> lock(muxerMutex);
            muxer.writeAudioFrame(data, size, audioPts);
        });
    }

    muxer.close();
}


void NvencEncoder::Impl::cleanup()
{
    if (encoder && nvenc.nvEncDestroyEncoder)
    {
        for (int i = 0; i < NUM_BUFFERS; i++)
        {
            if (inputBuffers[i]) nvenc.nvEncDestroyInputBuffer(encoder, inputBuffers[i]);
            if (outputBuffers[i]) nvenc.nvEncDestroyBitstreamBuffer(encoder, outputBuffers[i]);
            inputBuffers[i] = nullptr;
            outputBuffers[i] = nullptr;
        }
        nvenc.nvEncDestroyEncoder(encoder);
    }
    if (d3dContext) d3dContext->Release();
    if (d3dDevice) d3dDevice->Release();
    if (hNvenc) FreeLibrary(hNvenc);

    encoder = nullptr;
    d3dContext = nullptr;
    d3dDevice = nullptr;
    hNvenc = nullptr;
}

void NvencEncoder::Impl::convertRgbaToNv12(const uint8_t* rgba)
{
    // rgba is srcWidth x srcHeight; nv12Buffer is width x height (scaled).
    // Nearest-neighbor upscaling is applied during conversion so that
    // each source pixel maps to scaleFactor x scaleFactor output pixels,
    // preserving sharp edges through the subsequent 4:2:0 chroma subsampling.
    uint8_t* yPlane = nv12Buffer.data();
    uint8_t* uvPlane = yPlane + width * height;

    // Y plane (BT.601) with nearest-neighbor upscaling
    for (uint32_t y = 0; y < height; y++)
    {
        uint32_t sy = y / scaleFactor;
        if (sy >= srcHeight) sy = srcHeight - 1;
        for (uint32_t x = 0; x < width; x++)
        {
            uint32_t sx = x / scaleFactor;
            if (sx >= srcWidth) sx = srcWidth - 1;
            const uint8_t* px = rgba + (sy * srcWidth + sx) * 4;
            int yVal = ((66 * px[0] + 129 * px[1] + 25 * px[2] + 128) >> 8) + 16;
            yPlane[y * width + x] = static_cast<uint8_t>(std::clamp(yVal, 0, 255));
        }
    }

    // UV plane (2x2 subsampled) with nearest-neighbor upscaling.
    // At scaleFactor >= 2, each 2x2 block in the scaled domain maps to a
    // single source pixel, so the chroma value exactly matches the source —
    // this is what eliminates the color smearing that 4:2:0 would otherwise
    // introduce at native resolution.
    for (uint32_t y = 0; y < height; y += 2)
    {
        for (uint32_t x = 0; x < width; x += 2)
        {
            int rSum = 0, gSum = 0, bSum = 0;
            for (int dy = 0; dy < 2; dy++)
                for (int dx = 0; dx < 2; dx++)
                {
                    uint32_t sx = (x + dx) / scaleFactor;
                    uint32_t sy = (y + dy) / scaleFactor;
                    if (sx >= srcWidth) sx = srcWidth - 1;
                    if (sy >= srcHeight) sy = srcHeight - 1;
                    const uint8_t* px = rgba + (sy * srcWidth + sx) * 4;
                    rSum += px[0]; gSum += px[1]; bSum += px[2];
                }
            int r = rSum / 4, g = gSum / 4, b = bSum / 4;
            int u = ((-38 * r - 74 * g + 112 * b + 128) >> 8) + 128;
            int v = ((112 * r - 94 * g - 18 * b + 128) >> 8) + 128;
            size_t uvIdx = (y / 2) * width + x;
            uvPlane[uvIdx] = static_cast<uint8_t>(std::clamp(u, 0, 255));
            uvPlane[uvIdx + 1] = static_cast<uint8_t>(std::clamp(v, 0, 255));
        }
    }
}

void NvencEncoder::Impl::copyToInputBuffer(void* dst, uint32_t dstPitch)
{
    uint8_t* dstPtr = static_cast<uint8_t*>(dst);
    uint8_t* yPlane = nv12Buffer.data();
    uint8_t* uvPlane = yPlane + width * height;

    for (uint32_t y = 0; y < height; y++)
        memcpy(dstPtr + y * dstPitch, yPlane + y * width, width);

    uint8_t* dstUV = dstPtr + dstPitch * height;
    for (uint32_t y = 0; y < height / 2; y++)
        memcpy(dstUV + y * dstPitch, uvPlane + y * width, width);
}

bool NvencEncoder::Impl::encodeFrame(const uint8_t* rgba)
{
    convertRgbaToNv12(rgba);

    // If all buffers are pending, read one output first
    if (pendingFrames >= NUM_BUFFERS)
        writeOutput();

    // Lock and fill input buffer at current encode index
    auto lockIn = nvencStruct<NV_ENC_LOCK_INPUT_BUFFER>(NV_ENC_LOCK_INPUT_BUFFER_VER);
    lockIn.inputBuffer = inputBuffers[encodeIdx];
    if (nvenc.nvEncLockInputBuffer(encoder, &lockIn) != NV_ENC_SUCCESS)
        return false;

    pitch = lockIn.pitch;
    copyToInputBuffer(lockIn.bufferDataPtr, pitch);
    nvenc.nvEncUnlockInputBuffer(encoder, inputBuffers[encodeIdx]);

    // Encode frame
    auto picParams = nvencStruct<NV_ENC_PIC_PARAMS>(NV_ENC_PIC_PARAMS_VER);
    picParams.inputWidth = width;
    picParams.inputHeight = height;
    picParams.inputPitch = pitch;
    picParams.inputBuffer = inputBuffers[encodeIdx];
    picParams.outputBitstream = outputBuffers[encodeIdx];
    picParams.bufferFmt = NV_ENC_BUFFER_FORMAT_NV12;
    picParams.pictureStruct = NV_ENC_PIC_STRUCT_FRAME;
    picParams.frameIdx = frameIdx;
    picParams.inputTimeStamp = frameIdx * 1000;  // PTS in timescale units
    frameIdx++;

    NVENCSTATUS status = nvenc.nvEncEncodePicture(encoder, &picParams);

    if (status == NV_ENC_SUCCESS)
    {
        // Output ready - but in synchronous mode we still track it
        // Outputs come out IN ORDER OF SUBMISSION
        encodeIdx = (encodeIdx + 1) % NUM_BUFFERS;
        pendingFrames++;
        writeOutput();  // Read output immediately
    }
    else if (status == NV_ENC_ERR_NEED_MORE_INPUT)
    {
        // Frame buffered for B-frame reordering - track as pending
        encodeIdx = (encodeIdx + 1) % NUM_BUFFERS;
        pendingFrames++;
    }
    else
    {
        return false;
    }

    return true;
}

// ============================================================================
// Public interface
// ============================================================================

NvencEncoder::NvencEncoder() : _impl(std::make_unique<Impl>()) {}
NvencEncoder::~NvencEncoder() { Stop(); }

bool NvencEncoder::Start(const std::string& filename, const EncoderConfig& config)
{
    if (_isRecording) { _lastError = "Already recording"; return false; }

    _outputFilename = filename;
    if (!_impl->init(filename, config, _lastError))
    {
        _impl->cleanup();
        return false;
    }

    _framesEncoded = 0;
    _isRecording = true;
    return true;
}

void NvencEncoder::Stop()
{
    if (!_isRecording) return;
    _isRecording = false;

    _impl->finalize();
    _impl->cleanup();
}

void NvencEncoder::OnVideoFrame(const FramebufferDescriptor& framebuffer, double)
{
    if (_isRecording && _impl->encoder && _impl->encodeFrame(framebuffer.memoryBuffer))
        _framesEncoded++;
}

void NvencEncoder::OnAudioSamples(const int16_t* samples, size_t sampleCount, double timestampSec)
{
    if (!_isRecording || !_impl) return;

    // Convert timestamp to audio samples (assuming 44100 Hz default)
    int64_t pts = static_cast<int64_t>(timestampSec * _impl->aacEncoder.getSampleRate());
    _impl->encodeAudio(samples, sampleCount, pts);
}

uint64_t NvencEncoder::GetOutputFileSize() const
{
    if (_outputFilename.empty()) return 0;
    WIN32_FILE_ATTRIBUTE_DATA attrs;
    if (GetFileAttributesExA(_outputFilename.c_str(), GetFileExInfoStandard, &attrs))
        return (static_cast<uint64_t>(attrs.nFileSizeHigh) << 32) | attrs.nFileSizeLow;
    return 0;
}

#endif // _WIN32
