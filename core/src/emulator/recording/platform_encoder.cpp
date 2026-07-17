#include "platform_encoder.h"

#include <cctype>

#ifdef __APPLE__
#include "platform/macos/videotoolbox_encoder.h"
#elif defined(_WIN32)
#include "platform/windows/nvenc_probe.h"
#include "platform/windows/nvenc_encoder.h"
#endif

// ============================================================================
// isNativeAvailable
// ============================================================================

bool PlatformEncoderFactory::isNativeAvailable()
{
#ifdef __APPLE__
    // VideoToolbox is always available on macOS 10.8+
    return true;
#elif defined(_WIN32)
    // Check for NVENC support (NVIDIA GPU with driver installed)
    return NvencProbe::isAvailable();
#elif defined(__linux__)
    // Future: check for VA-API support
    return false;
#else
    return false;
#endif
}

// ============================================================================
// getAvailableNativeEncoders
// ============================================================================

std::vector<PlatformEncoderFactory::NativeEncoderInfo> PlatformEncoderFactory::getAvailableNativeEncoders()
{
    std::vector<NativeEncoderInfo> encoders;

#ifdef __APPLE__
    {
        NativeEncoderInfo info;
        info.id = "videotoolbox";
        info.displayName = "macOS VideoToolbox (H.264/HEVC)";
        info.supportsH264 = true;
        info.supportsHEVC = true;
        info.supportsAudio = true; // AVAssetWriter handles AAC
        encoders.push_back(info);
    }
#elif defined(_WIN32)
    {
        auto nvencInfo = NvencProbe::getInfo();
        if (nvencInfo.available)
        {
            NativeEncoderInfo info;
            info.id = "nvenc";
            info.displayName = "NVIDIA NVENC";
            info.supportsH264 = nvencInfo.supportsH264;
            info.supportsHEVC = nvencInfo.supportsHEVC;
            // AV1 encoding not yet implemented in NvencEncoder
            info.supportsAV1 = false;
            info.supportsAudio = false;
            encoders.push_back(info);
        }
    }
#endif

    // Future: Linux VA-API

    return encoders;
}

// ============================================================================
// createNativeEncoder
// ============================================================================

EncoderPtr PlatformEncoderFactory::createNativeEncoder(const EncoderConfig& config)
{
#ifdef __APPLE__
    // macOS: VideoToolbox handles H.264 and HEVC only. Returning nullptr for
    // anything else lets the caller report "codec unsupported by native encoder"
    // instead of silently encoding the wrong codec.
    std::string codec = config.videoCodec;
    for (auto& ch : codec) ch = static_cast<char>(::tolower(ch));
    if (codec != "h264" && codec != "h.264" && codec != "avc" &&
        codec != "h265" && codec != "h.265" && codec != "hevc")
        return nullptr;

    return std::make_unique<VideoToolboxEncoder>();
#elif defined(_WIN32)
    // Windows: NVENC for H.264/HEVC if NVIDIA GPU available
    if (!NvencProbe::isAvailable())
        return nullptr;

    std::string codec = config.videoCodec;
    for (auto& ch : codec) ch = static_cast<char>(::tolower(ch));
    if (codec != "h264" && codec != "h.264" && codec != "avc" &&
        codec != "h265" && codec != "h.265" && codec != "hevc")
        return nullptr;

    return std::make_unique<NvencEncoder>();
#elif defined(__linux__)
    // Future: create VA-API encoder
    (void)config;
    return nullptr;
#else
    (void)config;
    return nullptr;
#endif
}

// ============================================================================
// getNativeDisplayName
// ============================================================================

std::string PlatformEncoderFactory::getNativeDisplayName()
{
    auto encoders = getAvailableNativeEncoders();
    if (encoders.empty())
        return {};

    return encoders[0].displayName;
}
