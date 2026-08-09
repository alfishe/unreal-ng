#pragma once

#include <memory>
#include <string>
#include <vector>

#include "encoder_base.h"
#include "encoder_config.h"

/// @brief Platform-native encoder factory
///
/// Detects available hardware-accelerated encoders on the current platform
/// and creates instances of the appropriate native encoder.
///
/// Supported platforms:
/// - macOS: VideoToolboxEncoder (AVFoundation + VTCompressionSession)
/// - Windows: (future) NVENC / QuickSync via Media Foundation
/// - Linux: (future) VA-API
///
/// Native encoders are preferred over FFmpegPipeEncoder when available,
/// as they have zero external dependencies and use zero-copy GPU pipelines.
class PlatformEncoderFactory
{
public:
    /// @brief Information about an available native encoder
    struct NativeEncoderInfo
    {
        std::string id;            ///< Encoder ID ("videotoolbox", "nvenc", "qsv", "vaapi")
        std::string displayName;   ///< Human-readable name
        bool supportsH264 = false; ///< H.264 support
        bool supportsHEVC = false; ///< HEVC/H.265 support
        bool supportsAV1 = false;  ///< AV1 support (Ada Lovelace+)
        bool supportsAudio = false; ///< Audio muxing support (AAC via AVAssetWriter)
    };

    /// @brief Check if any native encoder is available on this platform
    /// @return true if at least one native encoder can be created
    static bool isNativeAvailable();

    /// @brief Get list of available native encoders with display names
    /// @return Vector of encoder info
    static std::vector<NativeEncoderInfo> getAvailableNativeEncoders();

    /// @brief Create a native encoder for the current platform
    /// @param config Encoder configuration (determines codec selection)
    /// @return Encoder instance, or nullptr if not available
    static EncoderPtr createNativeEncoder(const EncoderConfig& config);

    /// @brief Get the display name for the best native encoder on this platform
    /// @return Display name string, empty if no native encoder
    static std::string getNativeDisplayName();
};
