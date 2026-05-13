#pragma once

#include <cstdint>
#include <string>
#include <vector>

/// @brief Video capture region options
enum class VideoCaptureRegion
{
    MainScreen,  ///< 256×192 - ZX Spectrum main display area only (no border)
    FullFrame    ///< Full framebuffer including border (size varies by model)
};

/// @brief Configuration for encoder initialization
struct EncoderConfig
{
    /// region <Video Configuration>

    /// Capture region (main screen only or full frame with border)
    VideoCaptureRegion captureRegion = VideoCaptureRegion::FullFrame;

    /// Video frame width (0 = use source dimensions or capture region default)
    uint32_t videoWidth = 0;

    /// Video frame height (0 = use source dimensions or capture region default)
    uint32_t videoHeight = 0;

    /// Target frame rate (default: 50 Hz for ZX Spectrum)
    float frameRate = 50.0f;

    /// Video bitrate in kbps (0 = encoder default)
    uint32_t videoBitrate = 0;

    /// Video codec hint (e.g., "h264", "h265", "vp9")
    std::string videoCodec = "h264";

    /// endregion </Video Configuration>

    /// region <Audio Configuration>

    /// Audio sample rate in Hz
    uint32_t audioSampleRate = 44100;

    /// Number of audio channels (1 = mono, 2 = stereo)
    uint32_t audioChannels = 2;

    /// Audio bitrate in kbps (0 = encoder default)
    uint32_t audioBitrate = 192;

    /// Audio codec hint (e.g., "aac", "mp3", "flac", "pcm")
    std::string audioCodec = "aac";

    /// endregion </Audio Configuration>

    /// region <GIF-specific>

    /// GIF palette mode for optimization
    enum class GifPaletteMode
    {
        Auto,       ///< Recalculate palette per-frame (original behavior)
        FixedZX16,  ///< Fixed ZX Spectrum 16-color palette (fast)
        FixedZX256  ///< Fixed 256-color palette for modern clones (fast)
    };

    /// Palette mode (default: Auto for compatibility)
    GifPaletteMode gifPaletteMode = GifPaletteMode::Auto;

    /// Frame delay in milliseconds (for GIF animation)
    uint32_t gifDelayMs = 20;  // 50 fps = 20ms per frame

    /// Enable dithering for GIF palette reduction (only used in Auto mode)
    bool gifDither = false;  // Disabled by default for ZX Spectrum (fixed palette)

    /// endregion </GIF-specific>

    /// region <Quality Settings>

    /// Quality preset (0-10, 0 = fastest/lowest, 10 = slowest/best)
    int qualityPreset = 5;

    /// Enable lossless mode (where supported)
    bool lossless = false;

    /// endregion </Quality Settings>
};
