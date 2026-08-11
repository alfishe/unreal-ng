#include "realtime_estimator.h"
#include "encoder_config.h"

#include <algorithm>
#include <cmath>
#include <sstream>

// ============================================================================
// Frame budget
// ============================================================================

double RealtimeEstimator::getFrameBudgetMs(const EncoderConfig& config)
{
    if (config.frameRate <= 0)
        return 20.0; // Default 50fps

    return 1000.0 / config.frameRate;
}

// ============================================================================
// Heuristic estimate
// ============================================================================

RealtimeEstimator::RealtimeCapability RealtimeEstimator::estimate(
    const EncoderConfig& config,
    bool useNativeEncoder,
    bool ffmpegHwVideoToolbox,
    bool ffmpegHwNVENC,
    bool ffmpegHwQS,
    bool ffmpegHwVA)
{
    std::string codec = config.videoCodec;
    std::transform(codec.begin(), codec.end(), codec.begin(), ::tolower);

    // GIF is always realtime (native encoder)
    if (codec == "gif")
        return RealtimeCapability::Realtime;

    // Native hardware encoder (VideoToolbox, NVENC, QuickSync, VA-API)
    if (useNativeEncoder)
    {
        // Hardware encoders are always realtime for 320x240@50
        return RealtimeCapability::Realtime;
    }

    // FFmpeg-based encoding
    // Check if hardware acceleration is available via ffmpeg
    bool hasHwAccel = ffmpegHwVideoToolbox || ffmpegHwNVENC || ffmpegHwQS || ffmpegHwVA;

    if (codec == "h264" || codec == "h.264" || codec == "avc")
    {
        if (hasHwAccel)
            return RealtimeCapability::Realtime; // HW H.264 via ffmpeg

        // Software libx264 at 320x240@50 is trivial for modern CPUs
        return RealtimeCapability::Realtime;
    }

    if (codec == "h265" || codec == "h.265" || codec == "hevc")
    {
        if (hasHwAccel)
            return RealtimeCapability::Realtime; // HW HEVC via ffmpeg

        // Software libx265 is CPU-intensive, may exceed budget
        return RealtimeCapability::NonRealtime;
    }

    if (codec == "vp9")
    {
        if (hasHwAccel && ffmpegHwVA)
            return RealtimeCapability::Realtime;

        // Software VP9 (libvpx-vp9) is very CPU-intensive
        return RealtimeCapability::NonRealtime;
    }

    if (codec == "av1")
    {
        if (hasHwAccel)
            return RealtimeCapability::Realtime;

        // Software AV1 (libaom) is extremely slow
        return RealtimeCapability::NonRealtime;
    }

    if (codec == "vp8")
    {
        // libvpx is moderately fast
        return RealtimeCapability::Realtime;
    }

    // Unknown codec — can't determine
    return RealtimeCapability::Unknown;
}

// ============================================================================
// Reason string
// ============================================================================

std::string RealtimeEstimator::getReason(const EncoderConfig& config, bool useNativeEncoder)
{
    std::string codec = config.videoCodec;
    std::transform(codec.begin(), codec.end(), codec.begin(), ::tolower);

    if (codec == "gif")
        return "Native GIF encoder — no external dependencies, always realtime";

    if (useNativeEncoder)
    {
#ifdef __APPLE__
        return "Hardware VideoToolbox encoder — zero-copy GPU encode, always realtime";
#elif defined(_WIN32)
        return "Hardware encoder (NVENC/QuickSync) — GPU-accelerated, always realtime";
#elif defined(__linux__)
        return "Hardware VA-API encoder — GPU-accelerated, always realtime";
#else
        return "Hardware encoder — always realtime";
#endif
    }

    // FFmpeg-based
    if (codec == "h264" || codec == "h.264" || codec == "avc")
        return "Software H.264 (libx264) — 320x240@50 is trivial for modern CPUs";

    if (codec == "h265" || codec == "h.265" || codec == "hevc")
        return "Software H.265 (libx265) — CPU-intensive, may exceed 20ms frame budget";

    if (codec == "vp9")
        return "Software VP9 (libvpx-vp9) — very CPU-intensive, likely non-realtime";

    if (codec == "av1")
        return "Software AV1 (libaom) — extremely slow, non-realtime";

    if (codec == "vp8")
        return "Software VP8 (libvpx) — moderately fast, likely realtime";

    return "Unknown codec — realtime capability uncertain";
}
