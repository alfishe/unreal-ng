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
#include "emulator/video/displaygeometry.h"
#include "emulator/zxpoly/zxpolygroup.h"

namespace
{
/// The area's rectangle inside the frame
bool AreaRect(const PictureGeometry& g, ScreenshotArea area, PictureRect& rect, std::string& why)
{
    if (area == ScreenshotArea::Full)
    {
        rect = PictureRect{0, 0, g.width, g.height};
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
std::vector<uint8_t> Crop(const std::vector<uint8_t>& pixels, uint32_t stride, const PictureRect& rect)
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
    // The project's path convention: UTF-8 std::string (path.string() is the ANSI code page on Windows). GifBegin and
    // FileHelper both take UTF-8, so a temp folder with any characters works
    const auto u8 = path.u8string();
    const std::string pathText(u8.begin(), u8.end());

    GifWriter writer = {};
    if (!GifBegin(&writer, pathText.c_str(), width, height, 0))
        return out;
    const bool wrote = GifWriteFrame(&writer, rgba, width, height, 0);
    GifEnd(&writer);

    if (wrote)
    {
        if (FILE* f = FileHelper::OpenFile(pathText, "rb"))
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
            FileHelper::CloseFile(f);
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
/// Encode `rgba` (width x height) as the options say, into `r` (the bytes, or the file): r.ok on success
void EncodeInto(const uint8_t* rgba, uint16_t width, uint16_t height, const ScreenshotOptions& options, ScreenshotResult& r)
{
    std::vector<uint8_t> encoded = options.format == ScreenshotFormat::Png ? EncodePng(rgba, width, height)
                                                                          : EncodeGif(rgba, width, height);
    if (encoded.empty())
    {
        r.error = ScreenshotError::EncodeFailed;
        r.errorMessage = std::string("The ") + Screenshotter::FormatName(options.format) + " encoder produced nothing";
        return;
    }

    r.format = options.format;
    r.width = width;
    r.height = height;
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
            r.error = ScreenshotError::IoFailed;
            r.errorMessage = "Failed to write the image to " + options.saveTo;
            return;
        }
        // The absolute path: a relative one resolves against the emulator's working directory,
        // which the caller (an MCP client, a script elsewhere) does not know
        std::error_code ec;
        const std::filesystem::path absolute = std::filesystem::absolute(out, ec);
        r.savedFile = ec ? options.saveTo : FileHelper::FromFsPath(absolute.lexically_normal());
    }
    else
    {
        r.bytes = std::move(encoded);
    }
    r.ok = true;
}

/// Source window: the picture the front end's window draws for this emulator (WindowView), the frame it shows
ScreenshotResult TakeWindow(const std::string& emulatorId, const PictureGeometry& frame, const ScreenshotOptions& options)
{
    if (options.area != ScreenshotArea::Full)
        return Fail(ScreenshotError::BadParameter, "source=window gives the whole picture the window draws: use area=full");

    const std::shared_ptr<IWindowViewProvider> provider = WindowView::Provider();
    if (!provider)
        return Fail(ScreenshotError::NoWindow, "No GUI window: source=window needs a front end that draws the machine (unreal-qt)");

    WindowViewGrab grab;
    std::string error;
    if (!provider->GrabWindowView(emulatorId, grab, error))
        return Fail(ScreenshotError::NoWindow, error.empty() ? "The window could not be grabbed" : error);
    if (grab.width == 0 || grab.height == 0 || grab.rgba.size() != static_cast<size_t>(grab.width) * grab.height * RGBA_SIZE)
        return Fail(ScreenshotError::NoFrame, "The window gave no picture");

    ScreenshotResult r;
    r.crop = PictureRect{0, 0, frame.width, frame.height};
    r.frame = frame;
    EncodeInto(grab.rgba.data(), grab.width, grab.height, options, r);
    if (!r.ok)
        return r;
    grab.rgba.clear();
    grab.rgba.shrink_to_fit();
    r.fromWindow = true;
    r.window = std::move(grab);
    return r;
}
}  // namespace

ScreenshotResult Screenshotter::Render(const FrameSnapshot& snapshot, const ScreenshotOptions& options)
{
    const PictureGeometry& g = snapshot.geometry;
    if (g.width == 0 || g.height == 0 || snapshot.pixels.size() != static_cast<size_t>(g.stride) * g.height)
        return Fail(ScreenshotError::NoFrame, "The frame has no pixels or its geometry does not match them");

    PictureRect rect;
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

    // The picture as the CRT shows it, the same as a recording (DisplayGeometry::Picture): the whole frame in the
    // reference 352:288 shape, the working window in its share of it. A Sprinter / TS-Conf frame of 736x288 / 720x288
    // doubles its lines, an ATM's 640x200 window grows to its shape; a Spectrum's 352x288 stays as it is. The stored
    // pixels are the crop's (and /capture/framebuffer's). A video card's picture (the FT812's VGA modes) has square
    // pixels of its own and stays as it is
    DisplayGeometry::Size shown{rect.width, rect.height, 1};
    if (g.source != FrameSource::External)
    {
        const double aspect = options.area == ScreenshotArea::Full
                                  ? 0.0
                                  : DisplayGeometry::WindowAspect(g.width, g.height, rect.width, rect.height);
        shown = DisplayGeometry::Picture(rect.width, rect.height, aspect);
    }
    std::vector<uint8_t> shaped;
    if (shown.width != rect.width || shown.height != rect.height)
    {
        shaped.resize(static_cast<size_t>(shown.width) * shown.height * RGBA_SIZE);
        DisplayGeometry::StretchNearest(rgba, rect.width, rect.height, shaped.data(), shown.width, shown.height);
        rgba = shaped.data();
    }

    ScreenshotResult r;
    r.crop = rect;
    r.frame = g;
    EncodeInto(rgba, shown.width, shown.height, options, r);
    return r;
}

ScreenshotResult Screenshotter::TakeFrom(Screen& screen, const ScreenshotOptions& options, bool emulationParked)
{
    FrameSnapshot snapshot;
    if (options.source == ScreenshotSource::Window)
    {
        const EmulatorContext* context = screen.GetEmulatorContext();
        if (!context || !context->pEmulator)
            return Fail(ScreenshotError::NoWindow, "The screen belongs to no emulator: no window shows it");
        return TakeWindow(context->pEmulator->GetId(), screen.DescribeCurrentFrame(), options);
    }
    if (options.source == ScreenshotSource::Live)
    {
        if (!screen.SnapshotLive(snapshot, emulationParked, options.liveTimeoutMs))
            return Fail(ScreenshotError::NoFrame,
                        emulationParked ? "The emulator has no frame buffer"
                                        : "The emulation thread did not finish a frame within " +
                                              std::to_string(options.liveTimeoutMs) + " ms");
    }
    else if (!screen.SnapshotPresented(snapshot))
    {
        return Fail(ScreenshotError::NoFrame, "The emulator has not presented a frame yet");
    }
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

    // The master of a ZX-Poly group shows the group's composed frame (2x, the four modules' paper): that is what
    // the user sees, so that is the screenshot. The slaves keep their own frames
    // (the group composes its frame at every master frame end: its latest is also the live one)
    ZXPolyGroup* group = manager->GetZXPolyGroup(emulatorId);
    if (group && group->GetContext(0) == context)
    {
        FrameSnapshot snapshot;
        if (!group->SnapshotDisplay(snapshot))
            return Fail(ScreenshotError::NoFrame, "The ZX-Poly group has not composed a frame yet");
        if (options.source == ScreenshotSource::Window)
            return TakeWindow(emulatorId, snapshot.geometry, options);
        return Render(snapshot, options);
    }
    return TakeFrom(*context->pScreen, options, emulator->IsEmulationParked());
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

bool Screenshotter::ParseSource(const std::string& text, ScreenshotSource& source)
{
    if (text == "presented")
        source = ScreenshotSource::Presented;
    else if (text == "live")
        source = ScreenshotSource::Live;
    else if (text == "window")
        source = ScreenshotSource::Window;
    else
        return false;
    return true;
}

bool Screenshotter::ParseRequestWords(const std::string& area, const std::string& mode, const std::string& format,
                                      const std::string& source, ScreenshotOptions& options, std::string& message)
{
    if (!area.empty() && !ParseArea(area, options.area))
    {
        message = "Unknown area '" + area + "': use full or screen";
        return false;
    }
    if (!mode.empty())
    {
        ScreenshotArea fromMode = ScreenshotArea::Full;
        if (!ParseArea(mode, fromMode))
        {
            message = "Unknown mode '" + mode + "' (deprecated, use area): use full or screen";
            return false;
        }
        if (!area.empty() && fromMode != options.area)
        {
            message = "area=" + area + " and mode=" + mode + " disagree (mode is a deprecated alias of area)";
            return false;
        }
        options.area = fromMode;
    }
    if (!format.empty() && !ParseFormat(format, options.format))
    {
        message = "Unknown format '" + format + "': use png or gif";
        return false;
    }
    if (!source.empty() && !ParseSource(source, options.source))
    {
        message = "Unknown source '" + source + "': use presented, live or window";
        return false;
    }
    return true;
}

const char* Screenshotter::AreaName(ScreenshotArea area)
{
    return area == ScreenshotArea::Full ? "full" : "screen";
}

const char* Screenshotter::RequestSourceName(ScreenshotSource source)
{
    switch (source)
    {
        case ScreenshotSource::Presented: return "presented";
        case ScreenshotSource::Live: return "live";
        case ScreenshotSource::Window: return "window";
    }
    return "unknown";
}

const char* Screenshotter::FormatName(ScreenshotFormat format)
{
    return format == ScreenshotFormat::Png ? "png" : "gif";
}

const char* Screenshotter::SourceName(FrameSource source)
{
    switch (source)
    {
        case FrameSource::Native: return "native";
        case FrameSource::External: return "external";
        case FrameSource::Composed: return "composed";
    }
    return "unknown";
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
        case ScreenshotError::NoWindow: return "no-window";
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
