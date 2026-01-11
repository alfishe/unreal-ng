#pragma once

#include <string>

#include "../encoder_base.h"
#include "../encoder_config.h"
#include "3rdparty/gif/gif.h"

/// @brief GIF Animation Encoder
///
/// Produces animated GIF files from emulator video frames.
/// Directly wraps gif.h functions for optimal performance.
///
/// Features:
/// - Video-only (GIF has no audio support)
/// - 256-color palette with optional dithering
/// - Multiple palette modes for different use cases
/// - Hash lookup for exact color matching
/// - Configurable frame delay
/// - RAII cleanup guarantee
/// - Path validation before recording
///
/// Optimization Modes:
/// - Auto: Per-frame palette calculation (compatible with any content)
/// - FixedZX16: Pre-built ZX Spectrum 16-color palette (fast)
/// - FixedZX256: Pre-built 256-color palette for modern clones (fast)
/// - HashLookup: Exact color matching with O(1) lookup (recommended)
class GIFEncoder : public EncoderBase
{
public:
    GIFEncoder() = default;
    ~GIFEncoder() override;

    // Non-copyable, non-movable (owns file resource)
    GIFEncoder(const GIFEncoder&) = delete;
    GIFEncoder& operator=(const GIFEncoder&) = delete;
    GIFEncoder(GIFEncoder&&) = delete;
    GIFEncoder& operator=(GIFEncoder&&) = delete;

    /// region <Lifecycle>

    /// @brief Start recording to file
    /// @param filename Output file path (directory must exist and be writable)
    /// @param config Encoder configuration (videoWidth, videoHeight, gifDelayMs, gifPaletteMode)
    /// @return true if recording started, false on error (call GetLastError)
    bool Start(const std::string& filename, const EncoderConfig& config) override;

    /// @brief Stop recording and finalize file
    /// Safe to call multiple times or when not recording
    void Stop() override;

    /// endregion </Lifecycle>

    /// region <Frame Input>

    /// @brief Write video frame to GIF
    /// @param framebuffer Frame data (RGBA format, memoryBuffer must not be null)
    /// @param timestampSec Presentation timestamp (unused for GIF)
    void OnVideoFrame(const FramebufferDescriptor& framebuffer, double timestampSec) override;
    // OnAudioSamples - uses default no-op (GIF has no audio)

    /// endregion </Frame Input>

    /// region <State>

    bool IsRecording() const override
    {
        return _isRecording;
    }
    std::string GetType() const override
    {
        return "gif";
    }
    std::string GetDisplayName() const override
    {
        return "GIF Animation";
    }
    bool SupportsVideo() const override
    {
        return true;
    }
    bool SupportsAudio() const override
    {
        return false;
    }

    /// endregion </State>

    /// region <Statistics>

    uint64_t GetFramesEncoded() const override
    {
        return _framesEncoded;
    }

    /// endregion </Statistics>

    /// region <Error handling>

    /// @brief Get last error message
    /// @return Error string from last failed operation, empty if no error
    std::string GetLastError() const;

    /// @brief Get output filename
    /// @return Filename passed to Start(), empty if not recording
    std::string GetOutputFilename() const;

    /// endregion </Error handling>

private:
    /// Build pre-computed ZX Spectrum 16-color palette
    void BuildZXSpectrum16Palette();

    /// Build pre-computed 256-color palette for modern clones
    void BuildZXSpectrum256Palette();

    /// Build hash lookup table from palette
    void BuildHashLookup();

    // GIF writer state (direct from gif.h)
    GifWriter _gifWriter = {};
    bool _isRecording = false;
    uint64_t _framesEncoded = 0;

    // Configuration
    uint32_t _width = 0;
    uint32_t _height = 0;
    uint32_t _delayMs = 20;
    EncoderConfig::GifPaletteMode _paletteMode = EncoderConfig::GifPaletteMode::Auto;
    bool _dither = false;

    // Fixed palette (for FixedZX16/FixedZX256 modes)
    GifPalette _fixedPalette = {};
    GifColorLookup _colorLookup = {};
    bool _useFixedPalette = false;
    bool _useHashLookup = false;

    // State
    std::string _filename;
    std::string _lastError;
};
