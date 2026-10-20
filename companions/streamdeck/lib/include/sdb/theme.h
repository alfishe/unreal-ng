// Theme = directory with theme.json + PNG/JPEG/GIF(first frame)/BMP images.
//   { "name": "...", "brightness": 60, "quality": 90, "clear_unlisted": true,
//     "keys": [ {"key": 0, "image": "keys/run.png", "fit": "cover"}, ... ],
//     "fit": "contain",                                      // contain (default, keeps aspect) | cover | stretch
//     "grids": [ {"image": "strip.png", "rows": "3", "cols": "0-8", "fit": "contain", "gap": 0} ],   // spans keys,
//     aspect kept "lcd":  { "image": "lcd/strip.png" },                 // full landscape strip "tiles": [ {"dial": 0,
//     "image": "lcd/t0.png"} ],      // per-dial 200xH tiles (Plus / Plus XL) "touch_leds": [ {"point": 0, "rgb":
//     [255,0,0]} ] }    // Neo
#pragma once
#include <chrono>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "sdb/device.h"

namespace sdb
{

// Splits one image over a block of keys (rows r0..r1, cols c0..c1), keeping the aspect ratio inside the block
// canvas according to `fit`. Returns (keyIndex, wire-ready 144x144 RGB tile) pairs. `gap` draws a dark bezel per key.
struct GridSpec
{
    int r0 = 0, r1 = 0, c0 = 0, c1 = 0;
    Fit fit = Fit::Contain;
    std::uint8_t bg[3] = {0, 0, 0};
    int gap = 0;
};
[[nodiscard]] std::vector<std::pair<int, Rgb>> SliceGrid(const Model& m, const Rgb& src, const GridSpec& g,
                                                         Rgb* canvasOut = nullptr);
[[nodiscard]] bool GridInRange(const Model& m, const GridSpec& g);

struct Theme
{
    std::string name;
    int brightness = -1;  // -1 = leave unchanged
    int quality = 90;
    bool clearUnlisted = true;
    Fit fit = Fit::Contain;  // default for every image; keys/tiles may override
    struct Key
    {
        int key;
        Rgb img;
        Fit fit;
    };
    struct Tile
    {
        int dial;
        Rgb img;
        Fit fit;
    };
    Fit lcdFit = Fit::Contain;
    struct Led
    {
        int point;
        std::uint8_t r, g, b;
    };
    std::vector<Key> keys;
    struct Grid
    {
        Rgb img;
        GridSpec spec;
    };
    std::vector<Grid> grids;  // one picture spread over several keys
    std::optional<Rgb> lcd;
    std::vector<Tile> tiles;
    std::vector<Led> leds;
};

struct ThemeLoad
{
    std::optional<Theme> theme;
    std::vector<std::string> warnings;
    std::string error;
};
[[nodiscard]] ThemeLoad LoadTheme(const std::filesystem::path& dirOrJson);

struct UploadReport
{
    std::chrono::microseconds encode{0}, upload{0}, total{0};
    std::size_t keys = 0, reports = 0, bytes = 0;
    std::vector<std::string> warnings;
};
// Pre-encodes everything first, then streams to the device (brightness 0 -> content -> target brightness).
[[nodiscard]] UploadReport ApplyTheme(Device& dev, const Theme& theme, bool hideWhileLoading = true);

}  // namespace sdb
