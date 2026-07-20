#include "platform_encoder.h"

#include <cctype>

#ifdef __APPLE__
#include "encoders/videotoolbox_encoder.h"
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
    // Future: check for NVENC / QuickSync support
    return false;
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
#endif

    // Future: Windows NVENC/QuickSync
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
    // Future: create NVENC or QuickSync encoder
    (void)config;
    return nullptr;
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
