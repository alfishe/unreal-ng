#pragma once

#include <string>
#include <vector>

/// @brief FFmpeg binary detection and capability probing
///
/// Locates the ffmpeg binary on the system, queries available encoders,
/// and detects hardware acceleration support.
class FFmpegProbe
{
public:
    /// @brief Find ffmpeg binary on the system
    /// @return Path to ffmpeg, or empty string if not found
    static std::string findFFmpeg();

    /// @brief Find ffprobe binary on the system
    /// @return Path to ffprobe, or empty string if not found
    static std::string findFFprobe();

    /// @brief Check if ffmpeg is available
    /// @param path Optional explicit path (empty = auto-detect)
    /// @return true if ffmpeg binary exists and is executable
    static bool isAvailable(const std::string& path = "");

    /// @brief Get ffmpeg version string
    /// @param path Optional explicit path
    /// @return Version string (e.g., "ffmpeg version 6.0")
    static std::string getVersion(const std::string& path = "");

    /// @brief Hardware acceleration capabilities detected from ffmpeg
    struct HWAccelInfo
    {
        bool videoToolbox = false; ///< macOS VideoToolbox
        bool nvenc = false;        ///< NVIDIA NVENC
        bool qsv = false;          ///< Intel QuickSync
        bool vaapi = false;        ///< Linux VA-API
        bool amf = false;          ///< AMD AMF
    };

    /// @brief Detect available hardware acceleration support
    /// @param ffmpegPath Optional explicit ffmpeg path
    /// @return HW acceleration info
    static HWAccelInfo detectHWAccel(const std::string& ffmpegPath = "");

    /// @brief Query available encoders from ffmpeg
    /// @param ffmpegPath Optional explicit ffmpeg path
    /// @return List of encoder names
    static std::vector<std::string> getAvailableEncoders(const std::string& ffmpegPath = "");

    /// @brief Check if a specific encoder is available
    /// @param encoder Encoder name (e.g., "h264_videotoolbox", "libx264")
    /// @param ffmpegPath Optional explicit ffmpeg path
    /// @return true if encoder is available
    static bool isEncoderAvailable(const std::string& encoder, const std::string& ffmpegPath = "");

    /// @brief Get best available HW encoder for the current platform
    /// @param ffmpegPath Optional explicit ffmpeg path
    /// @return Encoder name (e.g., "h264_videotoolbox"), or empty if none found
    static std::string getBestHWEncoder(const std::string& ffmpegPath = "");

    /// @brief Get a human-readable description of detected capabilities
    /// @param ffmpegPath Optional explicit ffmpeg path
    /// @return Description string
    static std::string getCapabilityReport(const std::string& ffmpegPath = "");

private:
    /// Run a command and capture its stdout output
    static std::string runCommand(const std::string& command, const std::vector<std::string>& args, int timeoutMs = 5000);

    /// Check if a file exists and is executable
    static bool isExecutable(const std::string& path);

    /// Search PATH environment variable for a binary
    static std::string searchPath(const std::string& binaryName);

    /// Get platform-specific default search paths
    static std::vector<std::string> getDefaultSearchPaths();
};
