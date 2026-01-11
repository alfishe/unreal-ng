#include "gif_encoder.h"

#include <filesystem>

#include "emulator/video/screen.h"

GIFEncoder::~GIFEncoder()
{
    // RAII: Guarantee cleanup on destruction
    if (_isRecording)
    {
        Stop();
    }
}

bool GIFEncoder::Start(const std::string& filename, const EncoderConfig& config)
{
    // Already recording check
    if (_isRecording)
    {
        _lastError = "Already recording";
        return false;
    }

    // Validate filename
    if (filename.empty())
    {
        _lastError = "Empty filename";
        return false;
    }

    // Validate path is writable
    std::filesystem::path filePath(filename);
    std::filesystem::path parentDir = filePath.parent_path();

    // If parent path is empty, use current directory
    if (parentDir.empty())
    {
        parentDir = ".";
    }

    // Check if parent directory exists and is writable
    std::error_code ec;
    if (!std::filesystem::exists(parentDir, ec))
    {
        _lastError = "Directory does not exist: " + parentDir.string();
        return false;
    }

    // Store configuration
    _width = config.videoWidth;
    _height = config.videoHeight;
    _delayMs = config.gifDelayMs;
    _paletteMode = config.gifPaletteMode;
    _dither = config.gifDither;
    _filename = filename;

    // Validate dimensions
    if (_width == 0 || _height == 0)
    {
        _lastError = "Invalid dimensions (width or height is 0)";
        return false;
    }

    // Validate delay
    if (_delayMs == 0)
    {
        _delayMs = 20;  // Default to 50 fps
    }

    // Build fixed palette if requested
    _useFixedPalette = false;
    switch (_paletteMode)
    {
        case EncoderConfig::GifPaletteMode::FixedZX16:
            BuildZXSpectrum16Palette();
            _useFixedPalette = true;
            break;
        case EncoderConfig::GifPaletteMode::FixedZX256:
            BuildZXSpectrum256Palette();
            _useFixedPalette = true;
            break;
        case EncoderConfig::GifPaletteMode::Auto:
        default:
            // Use original per-frame palette calculation
            break;
    }

    // Start GIF animation
    _gifHelper.StartAnimation(filename, _width, _height, _delayMs);

    // Verify file was created (basic check)
    if (!std::filesystem::exists(filename, ec))
    {
        _lastError = "Failed to create output file";
        return false;
    }

    _isRecording = true;
    _framesEncoded = 0;
    _lastError.clear();

    return true;
}

void GIFEncoder::Stop()
{
    if (!_isRecording)
    {
        return;
    }

    _gifHelper.StopAnimation();
    _isRecording = false;
}

void GIFEncoder::OnVideoFrame(const FramebufferDescriptor& framebuffer, [[maybe_unused]] double timestampSec)
{
    if (!_isRecording)
    {
        return;
    }

    // Validate framebuffer
    if (framebuffer.memoryBuffer == nullptr)
    {
        return;  // Skip invalid frames silently
    }

    // Choose encoding path based on palette mode
    if (_useFixedPalette)
    {
        // Fast path: use pre-built palette, skip per-frame palette calculation
        _gifHelper.WriteFrameWithPalette(reinterpret_cast<uint32_t*>(framebuffer.memoryBuffer),
                                         framebuffer.width * framebuffer.height, &_fixedPalette, _dither);
    }
    else
    {
        // Original path: auto-calculate palette per frame
        _gifHelper.WriteFrame(reinterpret_cast<uint32_t*>(framebuffer.memoryBuffer),
                              framebuffer.width * framebuffer.height);
    }

    _framesEncoded++;
}

std::string GIFEncoder::GetLastError() const
{
    return _lastError;
}

std::string GIFEncoder::GetOutputFilename() const
{
    return _filename;
}

void GIFEncoder::BuildZXSpectrum16Palette()
{
    // Initialize palette for 16 colors (4-bit)
    _fixedPalette.bitDepth = 4;

    // ZX Spectrum 16-color palette
    // Index 0: Transparent/Black
    // Colors 1-7: Normal intensity (0xCD = 205)
    // Colors 8-15: Bright intensity (0xFF = 255)

    const uint8_t normal = 0xCD;
    const uint8_t bright = 0xFF;

    // Index 0 - transparent (black)
    _fixedPalette.r[0] = 0;
    _fixedPalette.g[0] = 0;
    _fixedPalette.b[0] = 0;

    // Standard ZX Spectrum color encoding:
    // Bit 0 = Blue, Bit 1 = Red, Bit 2 = Green
    // Colors 1-7: Normal, Colors 8-15: Bright
    for (int i = 1; i < 16; i++)
    {
        bool isBright = (i >= 8);
        uint8_t intensity = isBright ? bright : normal;
        int colorBits = i % 8;

        // Handle black specially (both normal and bright black are 0)
        if (colorBits == 0)
        {
            _fixedPalette.r[i] = 0;
            _fixedPalette.g[i] = 0;
            _fixedPalette.b[i] = 0;
        }
        else
        {
            _fixedPalette.r[i] = (colorBits & 0x02) ? intensity : 0;  // Bit 1 = Red
            _fixedPalette.g[i] = (colorBits & 0x04) ? intensity : 0;  // Bit 2 = Green
            _fixedPalette.b[i] = (colorBits & 0x01) ? intensity : 0;  // Bit 0 = Blue
        }
    }

    // Build k-d tree for fast color lookup
    GifBuildPaletteTree(&_fixedPalette);
}

void GIFEncoder::BuildZXSpectrum256Palette()
{
    // Initialize palette for 256 colors (8-bit)
    _fixedPalette.bitDepth = 8;

    // Build a 256-color palette suitable for TSConf and other modern clones
    // Using a 6-7-6 level RGB cube (252 colors) + some grays

    // Index 0 - transparent (black)
    _fixedPalette.r[0] = 0;
    _fixedPalette.g[0] = 0;
    _fixedPalette.b[0] = 0;

    int idx = 1;

    // Build an approximate RGB cube
    // 6 levels for R, 6 levels for G, 6 levels for B = 216 colors
    for (int r = 0; r < 6 && idx < 256; r++)
    {
        for (int g = 0; g < 6 && idx < 256; g++)
        {
            for (int b = 0; b < 6 && idx < 256; b++)
            {
                _fixedPalette.r[idx] = (uint8_t)(r * 51);  // 0, 51, 102, 153, 204, 255
                _fixedPalette.g[idx] = (uint8_t)(g * 51);
                _fixedPalette.b[idx] = (uint8_t)(b * 51);
                idx++;
            }
        }
    }

    // Fill remaining slots with grayscale
    while (idx < 256)
    {
        uint8_t gray = (uint8_t)((idx - 217) * 6);  // Spread remaining grays
        _fixedPalette.r[idx] = gray;
        _fixedPalette.g[idx] = gray;
        _fixedPalette.b[idx] = gray;
        idx++;
    }

    // Build k-d tree for fast color lookup
    GifBuildPaletteTree(&_fixedPalette);
}
