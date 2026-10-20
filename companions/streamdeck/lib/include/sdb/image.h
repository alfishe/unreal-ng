// Minimal RGB image toolkit: transform + encode exactly like the crate's convert_image_with_format
// (rotate -> resize_exact(triangle) -> mirror -> encode).
#pragma once
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "sdb/models.h"

namespace sdb
{

struct Rgb
{
    int w = 0, h = 0;
    std::vector<std::uint8_t> px;  // w*h*3, row-major, RGB
    Rgb() = default;
    Rgb(int w, int h, std::uint8_t r = 0, std::uint8_t g = 0, std::uint8_t b = 0);
    [[nodiscard]] std::uint8_t* At(int x, int y)
    {
        return &px[(static_cast<size_t>(y) * w + x) * 3];
    }
    [[nodiscard]] const std::uint8_t* At(int x, int y) const
    {
        return &px[(static_cast<size_t>(y) * w + x) * 3];
    }
    [[nodiscard]] bool Empty() const
    {
        return px.empty();
    }
};

[[nodiscard]] Rgb Rotate(const Rgb& src, Rotation r);
[[nodiscard]] Rgb MirrorImage(const Rgb& src, Mirror m);
[[nodiscard]] Rgb Resize(const Rgb& src, int w, int h);  // triangle filter, scale-aware (box-ish when shrinking)
[[nodiscard]] Rgb Crop(const Rgb& src, int x, int y, int w, int h);
void Blit(Rgb& dst, const Rgb& src, int x, int y);

// How a source image is placed into a differently shaped target. Contain keeps the aspect ratio (letterbox),
// Cover keeps it and crops the overflow, Stretch distorts (what a plain resize does).
enum class Fit
{
    Contain,
    Cover,
    Stretch
};
[[nodiscard]] std::optional<Fit> FitByName(const std::string& name);
[[nodiscard]] Rgb FitImage(const Rgb& src, int w, int h, Fit fit, std::uint8_t bgR = 0, std::uint8_t bgG = 0,
                           std::uint8_t bgB = 0);

// Shrinks the whole picture by `pad` px on every side onto `bg` (keeps generated content inside a key's visible area).
[[nodiscard]] Rgb InsetContent(const Rgb& src, int pad, std::uint8_t r = 0, std::uint8_t g = 0, std::uint8_t b = 0);

// Crops a uniform frame (e.g. a ZX Spectrum border) whose colour matches the top-left pixel within `tolerance` per
// channel.
[[nodiscard]] Rgb TrimBorder(const Rgb& src, int tolerance = 12);

// Alpha-composites onto black when the file has transparency.
[[nodiscard]] std::optional<Rgb> LoadImage(const std::filesystem::path& file);

[[nodiscard]] std::vector<std::uint8_t> EncodeJpeg(const Rgb& img, int quality = 90);
[[nodiscard]] std::vector<std::uint8_t> EncodeBmp(const Rgb& img);
[[nodiscard]] std::optional<Rgb> DecodeJpeg(const std::vector<std::uint8_t>& data);  // for tests/diagnostics
[[nodiscard]] std::optional<Rgb> DecodeBmp(const std::vector<std::uint8_t>& data);   // 24-bit, as encodeBmp writes

// Full device pipeline: returns wire-ready bytes (JPEG/BMP) in the model's native orientation.
[[nodiscard]] Rgb ToDeviceSpace(const Rgb& src, const ImageFormat& fmt);
[[nodiscard]] Rgb FromDeviceSpace(const Rgb& dev,
                                  const ImageFormat& fmt);  // inverse of the rotate + mirror (size kept)
// Decodes wire bytes of `fmt` (JPEG or BMP) back to an upright picture: the round trip a real device would display.
[[nodiscard]] std::optional<Rgb> DecodeForFormat(const std::vector<std::uint8_t>& data, const ImageFormat& fmt);
[[nodiscard]] std::vector<std::uint8_t> EncodeForFormat(const Rgb& src, const ImageFormat& fmt, int jpegQuality = 90);

}  // namespace sdb
