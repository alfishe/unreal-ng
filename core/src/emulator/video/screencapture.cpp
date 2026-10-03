#include "screencapture.h"

#include "emulator/video/screenshotter.h"

// Legacy shim: the pictures, the crop and the encoders live in Screenshotter. What stays here is the old
// calling convention (format as a word that falls back to GIF, mode as an enum, a CaptureResult with
// base64) for the surfaces that have not moved to Screenshotter yet

ScreenCapture::CaptureResult ScreenCapture::captureAsGif(const std::string& emulatorId, CaptureMode mode)
{
    return captureScreen(emulatorId, "gif", mode);
}

ScreenCapture::CaptureResult ScreenCapture::captureAsPng(const std::string& emulatorId, CaptureMode mode)
{
    return captureScreen(emulatorId, "png", mode);
}

ScreenCapture::CaptureResult ScreenCapture::captureScreen(const std::string& emulatorId,
                                                           const std::string& format,
                                                           CaptureMode mode,
                                                           const std::string& filePath)
{
    ScreenshotOptions options;
    options.area = mode == CaptureMode::ScreenOnly ? ScreenshotArea::Screen : ScreenshotArea::Full;
    // The legacy convention: "png" is PNG, any other word was silently GIF (the surfaces validate now)
    options.format = format == "png" ? ScreenshotFormat::Png : ScreenshotFormat::Gif;
    options.saveTo = filePath;

    const ScreenshotResult shot = Screenshotter::Take(emulatorId, options);

    CaptureResult result;
    if (!shot.ok)
    {
        result.errorMessage = shot.errorMessage;
        return result;
    }
    result.success = true;
    result.format = Screenshotter::FormatName(shot.format);
    result.originalSize = shot.encodedSize;
    result.width = shot.width;
    result.height = shot.height;
    result.savedFile = shot.savedFile;
    if (shot.savedFile.empty())
        result.base64Data = Screenshotter::Base64Encode(shot.bytes);
    return result;
}
