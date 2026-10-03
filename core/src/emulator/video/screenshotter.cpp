#include "screenshotter.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>

#include "3rdparty/gif/gif.h"
#include "3rdparty/lodepng/lodepng.h"
#include "common/filehelper.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/emulatormanager.h"

namespace
{
/// The area's rectangle inside the frame
bool AreaRect(const PictureGeometry& g, ScreenshotArea area, FrameRect& rect, std::string& why)
{
    if (area == ScreenshotArea::Full)
    {
        rect = FrameRect{0, 0, g.width, g.height};
        return true;
    }

    rect = g.screenWindow;
    const bool fits = rect.width > 0 && rect.height > 0 && static_cast<uint32_t>(rect.x) + rect.width <= g.width &&
                      static_cast<uint32_t>(rect.y) + rect.height <= g.height;
    if (!fits)
    {
        why = "The screen window " + std::to_string(rect.width) + "x" + std::to_string(rect.height) + " at (" +
              std::to_string(rect.x) + "," + std::to_string(rect.y) + ") does not fit the " +
              std::to_string(g.width) + "x" + std::to_string(g.height) + " frame";
        return false;
    }
    return true;
}

/// RGBA of `rect` out of a frame of `stride` bytes per line
std::vector<uint8_t> Crop(const std::vector<uint8_t>& pixels, uint32_t stride, const FrameRect& rect)
{
    std::vector<uint8_t> out(static_cast<size_t>(rect.width) * rect.height * RGBA_SIZE);
    const size_t lineBytes = static_cast<size_t>(rect.width) * RGBA_SIZE;
    for (uint16_t y = 0; y < rect.height; y++)
    {
        const uint8_t* src = pixels.data() + static_cast<size_t>(rect.y + y) * stride + static_cast<size_t>(rect.x) * RGBA_SIZE;
        std::memcpy(out.data() + static_cast<size_t>(y) * lineBytes, src, lineBytes);
    }
    return out;
}

std::vector<uint8_t> EncodePng(const uint8_t* rgba, uint16_t width, uint16_t height)
{
    std::vector<uint8_t> out;
    if (lodepng::encode(out, rgba, width, height) != 0)
        out.clear();
    return out;
}

/// A temp file name no other capture can share: process-wide counter plus the clock. (The GIF writer
/// wants a file; the old name was derived from the buffer address, which two captures of the same
/// buffer shared)
std::filesystem::path UniqueTempGif()
{
    static std::atomic<uint64_t> counter{0};
    const uint64_t ticks = static_cast<uint64_t>(std::chrono::steady_clock::now().time_since_epoch().count());
    std::error_code ec;
    std::filesystem::path dir = std::filesystem::temp_directory_path(ec);
    if (ec)
        dir = ".";
    return dir / ("unreal_capture_" + std::to_string(ticks) + "_" + std::to_string(counter.fetch_add(1)) + ".gif");
}

std::vector<uint8_t> EncodeGif(const uint8_t* rgba, uint16_t width, uint16_t height)
{
    std::vector<uint8_t> out;
    const std::filesystem::path path = UniqueTempGif();
    const std::string pathText = path.string();

    GifWriter writer = {};
    if (!GifBegin(&writer, pathText.c_str(), width, height, 0))
        return out;
    const bool wrote = GifWriteFrame(&writer, rgba, width, height, 0);
    GifEnd(&writer);

    if (wrote)
    {
        if (FILE* f = std::fopen(pathText.c_str(), "rb"))
        {
            std::fseek(f, 0, SEEK_END);
            const long size = std::ftell(f);
            std::fseek(f, 0, SEEK_SET);
            if (size > 0)
            {
                out.resize(static_cast<size_t>(size));
                if (std::fread(out.data(), 1, out.size(), f) != out.size())
                    out.clear();
            }
            std::fclose(f);
        }
    }
    std::error_code ec;
    std::filesystem::remove(path, ec);
    return out;
}

ScreenshotResult Fail(ScreenshotError error, const std::string& message)
{
    ScreenshotResult r;
    r.error = error;
    r.errorMessage = message;
    return r;
}
}  // namespace

ScreenshotResult Screenshotter::Render(const FrameSnapshot& snapshot, const ScreenshotOptions& options)
{
    const PictureGeometry& g = snapshot.geometry;
    if (g.width == 0 || g.height == 0 || snapshot.pixels.size() != static_cast<size_t>(g.stride) * g.height)
        return Fail(ScreenshotError::NoFrame, "The frame has no pixels or its geometry does not match them");

    FrameRect rect;
    std::string why;
    if (!AreaRect(g, options.area, rect, why))
        return Fail(ScreenshotError::BadGeometry, why);

    // Whole frame: no copy
    std::vector<uint8_t> cropped;
    const uint8_t* rgba = snapshot.pixels.data();
    if (!(rect.x == 0 && rect.y == 0 && rect.width == g.width && rect.height == g.height))
    {
        cropped = Crop(snapshot.pixels, g.stride, rect);
        rgba = cropped.data();
    }

    std::vector<uint8_t> encoded = options.format == ScreenshotFormat::Png ? EncodePng(rgba, rect.width, rect.height)
                                                                          : EncodeGif(rgba, rect.width, rect.height);
    if (encoded.empty())
        return Fail(ScreenshotError::EncodeFailed, std::string("The ") + FormatName(options.format) + " encoder produced nothing");

    ScreenshotResult r;
    r.format = options.format;
    r.width = rect.width;
    r.height = rect.height;
    r.crop = rect;
    r.frame = g;
    r.encodedSize = encoded.size();

    if (!options.saveTo.empty())
    {
        const std::filesystem::path out = FileHelper::ToFsPath(options.saveTo);
        if (out.has_parent_path())
        {
            std::error_code ec;
            std::filesystem::create_directories(out.parent_path(), ec);  // a failure shows in the open below
        }
        if (!FileHelper::SaveBufferToFile(options.saveTo, encoded.data(), encoded.size()))
        {
            std::remove(options.saveTo.c_str());  // never leave a truncated image behind
            return Fail(ScreenshotError::IoFailed, "Failed to write the image to " + options.saveTo);
        }
        r.savedFile = options.saveTo;
    }
    else
    {
        r.bytes = std::move(encoded);
    }
    r.ok = true;
    return r;
}

ScreenshotResult Screenshotter::TakeFrom(Screen& screen, const ScreenshotOptions& options)
{
    FrameSnapshot snapshot;
    if (!screen.SnapshotPresented(snapshot))
        return Fail(ScreenshotError::NoFrame, "The emulator has not presented a frame yet");
    return Render(snapshot, options);
}

ScreenshotResult Screenshotter::Take(const std::string& emulatorId, const ScreenshotOptions& options)
{
    auto* manager = EmulatorManager::GetInstance();
    if (!manager)
        return Fail(ScreenshotError::NotFound, "EmulatorManager not available");

    auto emulator = manager->GetEmulator(emulatorId);
    if (!emulator)
        return Fail(ScreenshotError::NotFound, "Emulator not found: " + emulatorId);

    EmulatorContext* context = emulator->GetContext();
    if (!context || !context->pScreen)
        return Fail(ScreenshotError::NoFrame, "The emulator has no screen");
    return TakeFrom(*context->pScreen, options);
}

bool Screenshotter::ParseArea(const std::string& text, ScreenshotArea& area)
{
    if (text == "full")
        area = ScreenshotArea::Full;
    else if (text == "screen")
        area = ScreenshotArea::Screen;
    else
        return false;
    return true;
}

bool Screenshotter::ParseFormat(const std::string& text, ScreenshotFormat& format)
{
    if (text == "png")
        format = ScreenshotFormat::Png;
    else if (text == "gif")
        format = ScreenshotFormat::Gif;
    else
        return false;
    return true;
}

const char* Screenshotter::AreaName(ScreenshotArea area)
{
    return area == ScreenshotArea::Full ? "full" : "screen";
}

const char* Screenshotter::FormatName(ScreenshotFormat format)
{
    return format == ScreenshotFormat::Png ? "png" : "gif";
}

const char* Screenshotter::ErrorName(ScreenshotError error)
{
    switch (error)
    {
        case ScreenshotError::None: return "none";
        case ScreenshotError::NotFound: return "not-found";
        case ScreenshotError::NoFrame: return "no-frame";
        case ScreenshotError::BadParameter: return "bad-parameter";
        case ScreenshotError::BadGeometry: return "bad-geometry";
        case ScreenshotError::EncodeFailed: return "encode-failed";
        case ScreenshotError::IoFailed: return "io-failed";
    }
    return "unknown";
}

std::string Screenshotter::Base64Encode(const std::vector<uint8_t>& data)
{
    static const char* chars =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
        "abcdefghijklmnopqrstuvwxyz"
        "0123456789+/";

    std::string result;
    result.reserve(((data.size() + 2) / 3) * 4);
    for (size_t i = 0; i < data.size(); i += 3)
    {
        uint32_t n = static_cast<uint32_t>(data[i]) << 16;
        if (i + 1 < data.size())
            n |= static_cast<uint32_t>(data[i + 1]) << 8;
        if (i + 2 < data.size())
            n |= static_cast<uint32_t>(data[i + 2]);

        result.push_back(chars[(n >> 18) & 0x3F]);
        result.push_back(chars[(n >> 12) & 0x3F]);
        result.push_back(i + 1 < data.size() ? chars[(n >> 6) & 0x3F] : '=');
        result.push_back(i + 2 < data.size() ? chars[n & 0x3F] : '=');
    }
    return result;
}
