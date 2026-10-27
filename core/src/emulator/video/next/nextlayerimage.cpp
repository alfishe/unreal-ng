#include "stdafx.h"

#include "nextlayerimage.h"

#include <algorithm>
#include <cctype>
#include <fstream>

#include "3rdparty/lodepng/lodepng.h"
#include "emulator/emulatorcontext.h"
#include "emulator/video/next/nextvideorenderer.h"
#include "emulator/video/next/screennext.h"
#include "emulator/video/screenshotter.h"

namespace NextLayer
{
bool ParseKind(const std::string& text, Kind& kind)
{
    std::string word = text;
    std::transform(word.begin(), word.end(), word.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    static const struct
    {
        const char* name;
        Kind kind;
    } kinds[] = {{"composed", Kind::Composed}, {"ula", Kind::Ula},   {"layer2", Kind::Layer2},
                 {"tilemap", Kind::Tilemap},   {"sprites", Kind::Sprites}, {"ids", Kind::Ids}};
    for (const auto& k : kinds)
        if (word == k.name)
        {
            kind = k.kind;
            return true;
        }
    return false;
}

const char* KindName(Kind kind)
{
    switch (kind)
    {
        case Kind::Composed: return "composed";
        case Kind::Ula: return "ula";
        case Kind::Layer2: return "layer2";
        case Kind::Tilemap: return "tilemap";
        case Kind::Sprites: return "sprites";
        case Kind::Ids: return "ids";
    }
    return "composed";
}

const char* KindWords()
{
    return "composed, ula, layer2, tilemap, sprites or ids";
}

const char* StatusName(Status status)
{
    switch (status)
    {
        case Status::Ok: return "ok";
        case Status::NotNext: return "not-next";
        case Status::Off: return "layer-capture-off";
        case Status::NoFrame: return "no-frame";
        case Status::BadParameter: return "bad-parameter";
    }
    return "ok";
}

bool ParseImageFormat(const std::string& text, bool& rgba, std::string& message)
{
    std::string word = text;
    std::transform(word.begin(), word.end(), word.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (word.empty() || word == "png")
        rgba = false;
    else if (word == "rgba")
        rgba = true;
    else
    {
        message = "Unknown format '" + text + "': use png or rgba";
        return false;
    }
    return true;
}

bool ParseAlpha(const std::string& text, bool& alpha, std::string& message)
{
    std::string word = text;
    std::transform(word.begin(), word.end(), word.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (word.empty() || word == "true" || word == "1" || word == "yes" || word == "on")
        alpha = true;
    else if (word == "false" || word == "0" || word == "no" || word == "off")
        alpha = false;
    else
    {
        message = "Unknown alpha '" + text + "': use true or false";
        return false;
    }
    return true;
}

const char* IdName(unsigned id)
{
    static const char* const names[kIdCount] = {"fallback", "ula", "border", "layer2", "tilemap", "sprite", "blend", "stencil"};
    return id < kIdCount ? names[id] : "unknown";
}

uint32_t IdColour(unsigned id)
{
    static const uint8_t colours[kIdCount][3] = {{0x40, 0x40, 0x40}, {0x30, 0x60, 0xFF}, {0x00, 0x20, 0x80}, {0x30, 0xC0, 0x30},
                                                 {0xFF, 0xC0, 0x30}, {0xFF, 0x30, 0x30}, {0xC0, 0x30, 0xC0}, {0x30, 0xC0, 0xC0}};
    const unsigned i = id < kIdCount ? id : 0;
    return 0xFF000000u | (static_cast<uint32_t>(colours[i][2]) << 16) | (static_cast<uint32_t>(colours[i][1]) << 8) | colours[i][0];
}

ScreenNext* NextScreen(EmulatorContext* context)
{
    return context ? dynamic_cast<ScreenNext*>(context->pScreen) : nullptr;
}

Status LastFrame(EmulatorContext* context, std::shared_ptr<const Frame>& frame, std::string& message)
{
    ScreenNext* screen = NextScreen(context);
    if (!screen)
    {
        message = "This machine is not a ZX Spectrum Next: there are no layers to capture";
        return Status::NotNext;
    }
    if (!screen->LayerCaptureEnabled())
    {
        message = "Layer capture is off: switch it on (POST /api/v1/emulator/{id}/next/layer-capture {\"enabled\":true}, ini [NEXT] LayerCapture=on) and let a frame run";
        return Status::Off;
    }
    frame = screen->LastLayerFrame();
    if (!frame)
    {
        message = "Layer capture is on but no frame has been completed yet: resume the machine or step a frame";
        return Status::NoFrame;
    }
    return Status::Ok;
}

void Render(const Frame& frame, Kind kind, bool alpha, std::vector<uint8_t>& rgba)
{
    rgba.assign(static_cast<size_t>(kWidth) * kHeight * 2 * 4, 0);
    uint32_t* out = reinterpret_cast<uint32_t*>(rgba.data());
    for (unsigned y = 0; y < kHeight; y++)
    {
        uint32_t* row = out + static_cast<size_t>(y) * 2 * kWidth;
        const size_t at = static_cast<size_t>(y) * kWidth;
        if (kind == Kind::Ids)
        {
            for (unsigned x = 0; x < kWidth; x++)
                row[x] = IdColour(frame.ids[at + x]);
        }
        else
        {
            const Pixel* plane = frame.plane[kind == Kind::Ula ? kUla : kind == Kind::Layer2 ? kLayer2 : kind == Kind::Tilemap ? kTilemap : kSprites].data() + at;
            for (unsigned x = 0; x < kWidth; x++)
            {
                if (plane[x].opaque)
                    row[x] = NextVideoRenderer::Rgba(plane[x].colour & 0x1FF);
                else if (alpha)
                    row[x] = 0;  // not painted: RGBA all zero
                else
                {
                    const Segment* segment = frame.SegmentAt(y, x);
                    const uint8_t v = segment ? segment->Reg(0x4A) : 0;
                    row[x] = NextVideoRenderer::Rgba(static_cast<uint16_t>((v << 1) | ((v & 3) ? 1 : 0)));
                }
            }
        }
        std::copy(row, row + kWidth, row + kWidth);  // every line twice
    }
}

bool ParseScale(const std::string& text, Scale& scale, std::string& message)
{
    std::string lower;
    for (char c : text)
        lower.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    if (lower.empty() || lower == "screen")
        scale = Scale::Screen;
    else if (lower == "grid")
        scale = Scale::Grid;
    else if (lower == "pixel")
        scale = Scale::Pixel;
    else
    {
        message = "Unknown size '" + text + "': use screen (640x512), grid (640x256) or pixel (320x256)";
        return false;
    }
    return true;
}

const char* ScaleName(Scale scale)
{
    return scale == Scale::Grid ? "grid" : (scale == Scale::Pixel ? "pixel" : "screen");
}

void ScaleSize(Scale scale, unsigned& width, unsigned& height)
{
    width = scale == Scale::Pixel ? kWidth / 2 : kWidth;
    height = scale == Scale::Screen ? kHeight * 2 : kHeight;
}

void Reduce(const std::vector<uint8_t>& screen, Scale scale, std::vector<uint8_t>& out)
{
    if (scale == Scale::Screen)
    {
        out = screen;
        return;
    }
    unsigned width, height;
    ScaleSize(scale, width, height);
    out.assign(static_cast<size_t>(width) * height * 4, 0);
    const uint32_t* in = reinterpret_cast<const uint32_t*>(screen.data());
    uint32_t* dst = reinterpret_cast<uint32_t*>(out.data());
    const unsigned step = scale == Scale::Pixel ? 2 : 1;
    for (unsigned y = 0; y < height; y++)
        for (unsigned x = 0; x < width; x++)
            dst[static_cast<size_t>(y) * width + x] = in[static_cast<size_t>(y) * 2 * kWidth + x * step];  // the first of the doubled lines
}

Image Capture(EmulatorContext* context, Kind kind, bool alpha, Scale scale)
{
    Image image;
    image.status = LastFrame(context, image.frame, image.message);
    if (image.status != Status::Ok)
        return image;
    if (kind == Kind::Composed)
    {
        image.status = Status::BadParameter;
        image.message = "The composed picture is the screenshot of the presented frame (capture/screen without layer)";
        return image;
    }
    ScaleSize(scale, image.width, image.height);
    image.sequence = image.frame->sequence;
    image.frameCounter = image.frame->frameCounter;
    if (scale == Scale::Screen)
        Render(*image.frame, kind, alpha, image.rgba);
    else
    {
        std::vector<uint8_t> full;
        Render(*image.frame, kind, alpha, full);
        Reduce(full, scale, image.rgba);
    }
    return image;
}

std::vector<uint8_t> EncodePng(const std::vector<uint8_t>& rgba, unsigned width, unsigned height)
{
    std::vector<uint8_t> out;
    if (lodepng::encode(out, rgba, width, height) != 0)
        out.clear();
    return out;
}

Result Request(EmulatorContext* context, const std::string& layer, const std::string& format, const std::string& alpha, const std::string& path,
               const std::string& size)
{
    Result result;
    auto fail = [&](Status status, const std::string& message) {
        result.status = status;
        result.message = message;
        return result;
    };
    if (!ParseKind(layer, result.kind))
        return fail(Status::BadParameter, "Unknown layer '" + layer + "': use " + KindWords());
    std::string message;
    if (!ParseImageFormat(format, result.rgba, message) || !ParseAlpha(alpha, result.alpha, message) || !ParseScale(size, result.scale, message))
        return fail(Status::BadParameter, message);
    const Image image = Capture(context, result.kind, result.alpha, result.scale);
    if (image.status != Status::Ok)
        return fail(image.status, image.message);
    result.width = image.width;
    result.height = image.height;
    result.sequence = image.sequence;
    result.frameCounter = image.frameCounter;
    result.bytes = result.rgba ? image.rgba : EncodePng(image.rgba, image.width, image.height);
    result.size = result.bytes.size();
    if (result.bytes.empty())
        return fail(Status::BadParameter, "The PNG encoder produced nothing");
    if (!path.empty())
    {
        std::ofstream file(path, std::ios::binary);
        file.write(reinterpret_cast<const char*>(result.bytes.data()), static_cast<std::streamsize>(result.bytes.size()));
        if (!file)
            return fail(Status::BadParameter, "The file could not be written: " + path);
        result.savedFile = path;
        result.bytes.clear();
    }
    return result;
}

std::string Base64(const std::vector<uint8_t>& bytes)
{
    return Screenshotter::Base64Encode(bytes);
}
}  // namespace NextLayer
