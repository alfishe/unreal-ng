#pragma once

/// @file nextlayerimage.h
/// @brief The images of a captured Next frame (design-layer-capture.md): one layer of the last completed frame as RGBA or PNG, with
/// alpha 0 where the layer does not paint (the "transparent pixels are transparent" requirement), in the geometry of the
/// framebuffer (640 x 512: the 640 x 256 grid with every line twice) so that it overlays `capture/screen` pixel for pixel.
/// One implementation for every surface (WebAPI, MCP, CLI, Lua, Python).

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "emulator/video/next/nextlayerframe.h"

class EmulatorContext;
class ScreenNext;

namespace NextLayer
{
/// What an image shows. `composed` is the picture the screen presented (the surfaces answer it with the ordinary screenshot)
enum class Kind
{
    Composed,
    Ula,
    Layer2,
    Tilemap,
    Sprites,
    Ids
};

/// "composed", "ula", "layer2", "tilemap", "sprites" or "ids" (case-insensitive)
bool ParseKind(const std::string& text, Kind& kind);
const char* KindName(Kind kind);
/// The words, for an error message
const char* KindWords();

enum class Status
{
    Ok,
    NotNext,       ///< the machine has no Next screen
    Off,           ///< the capture is switched off
    NoFrame,       ///< on, but no frame has been completed yet
    BadParameter,
};

const char* StatusName(Status status);

/// The words of one image request: `format` png (default, with alpha) or rgba (raw bytes R G B A), `alpha` true (default) or false
/// (flatten what the layer does not paint onto the fallback colour). An empty word keeps the default; any other is false with `message`
bool ParseImageFormat(const std::string& text, bool& rgba, std::string& message);
bool ParseAlpha(const std::string& text, bool& alpha, std::string& message);

/// The size of a layer image: `screen` 640 x 512 (the screenshot's geometry, the default), `grid` 640 x 256 (the grid's sub-pixels, every line once)
/// or `pixel` 320 x 256 (one sample per Next pixel: the first sub-pixel; a mode that draws the second one differently - Layer 2 640,
/// Timex hi-res - loses it, use grid there). The smaller sizes cost the reader less to move and to encode
enum class Scale
{
    Screen,
    Grid,
    Pixel
};
/// An empty word keeps `screen`; any other than screen / grid / pixel is false with `message`
bool ParseScale(const std::string& text, Scale& scale, std::string& message);
const char* ScaleName(Scale scale);
/// The width and height of an image of this scale
void ScaleSize(Scale scale, unsigned& width, unsigned& height);
/// Reduce a screen-size RGBA image (640 x 512) to `scale`
void Reduce(const std::vector<uint8_t>& screen, Scale scale, std::vector<uint8_t>& out);

struct Image
{
    Status status = Status::Ok;
    std::string message;  ///< why it failed, with what to do
    unsigned width = 0;   ///< 640
    unsigned height = 0;  ///< 512
    std::vector<uint8_t> rgba;       ///< width * height * 4 bytes, R G B A
    uint64_t sequence = 0;           ///< the n-th frame captured since the switch was turned on
    uint64_t frameCounter = 0;       ///< the emulator's frame counter when that frame completed
    std::shared_ptr<const Frame> frame;  ///< the frame it was made from
};

/// The ScreenNext of this machine, null on any other
ScreenNext* NextScreen(EmulatorContext* context);

/// The last completed frame of `context` (Status::Ok) or why there is none
Status LastFrame(EmulatorContext* context, std::shared_ptr<const Frame>& frame, std::string& message);

/// One layer of a frame as RGBA. `alpha` false: a pixel the layer does not paint is flattened onto the frame's fallback colour (NR #4A of
/// its segment) and the image is opaque. `ids`: the winner of every pixel in the colours of IdColour (always opaque)
void Render(const Frame& frame, Kind kind, bool alpha, std::vector<uint8_t>& rgba);

/// LastFrame + Render
Image Capture(EmulatorContext* context, Kind kind, bool alpha, Scale scale = Scale::Screen);

/// A PNG of an RGBA image (empty on an encoder failure)
std::vector<uint8_t> EncodePng(const std::vector<uint8_t>& rgba, unsigned width, unsigned height);

/// The name of a winner id ("fallback", "ula", "border", "layer2", "tilemap", "sprite", "blend", "stencil") and its colour in the `ids` image
const char* IdName(unsigned id);
uint32_t IdColour(unsigned id);  ///< RGBA bytes R G B A as a little-endian word (the layout of NextVideoRenderer::Rgba)

/// One request as the surfaces without their own HTTP shape (Lua, Python) make it: the words of the layer, the format and the alpha,
/// optionally a file to write the encoded image to. `status` Ok or why not (with `message`); bytes is empty when saved to `path`
struct Result
{
    Status status = Status::Ok;
    std::string message;
    Kind kind = Kind::Ula;
    bool rgba = false;
    bool alpha = true;
    Scale scale = Scale::Screen;
    unsigned width = 0, height = 0;
    std::vector<uint8_t> bytes;  ///< PNG or raw RGBA
    size_t size = 0;             ///< bytes of the encoded image, saved or not
    std::string savedFile;
    uint64_t sequence = 0;
    uint64_t frameCounter = 0;
};
Result Request(EmulatorContext* context, const std::string& layer, const std::string& format, const std::string& alpha, const std::string& path,
               const std::string& size = std::string());

/// base64 of bytes
std::string Base64(const std::vector<uint8_t>& bytes);
}  // namespace NextLayer
