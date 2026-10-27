// Static description of every supported Stream Deck model (VID 0x0FD9).
// Source of truth: crate elgato-streamdeck 0.13.2 info.rs; see docs/companions/streamdeck/hid/03-models.md.
#pragma once
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace sdb
{

inline constexpr std::uint16_t kElgatoVid = 0x0FD9;

enum class Family : std::uint8_t
{
    V1,
    V2
};  // V1: BMP/16 B header. V2: JPEG/8 B header.
enum class ImageMode : std::uint8_t
{
    None,
    Bmp,
    Jpeg
};
enum class Rotation : std::uint8_t
{
    R0,
    R90,
    R180,
    R270
};  // clockwise degrees, applied BEFORE resize
enum class Mirror : std::uint8_t
{
    None,
    X,
    Y,
    Both
};
enum class LcdKind : std::uint8_t
{
    None,
    Fill /*Neo: whole-surface*/,
    Region /*Plus/XL: rectangles*/
};

struct ImageFormat
{
    ImageMode mode = ImageMode::None;
    int w = 0, h = 0;  // device-space size after rotation
    Rotation rotation = Rotation::R0;
    Mirror mirror = Mirror::None;
};

struct Model
{
    std::uint16_t pid;
    std::string_view name;
    Family family;
    std::uint8_t keys, rows, cols, dials, touchPoints;
    ImageFormat key;  // mode None => no key screens (Pedal)
    LcdKind lcdKind = LcdKind::None;
    ImageFormat lcd{};                     // device-space format (what is written to the wire)
    int lcdLogicalW = 0, lcdLogicalH = 0;  // landscape size as seen by the user
    int dialTileW = 200;                   // width of one dial's touch-strip tile (logical)
    // Whole-panel image (Elgato docs "Update Full Screen Image", output 0x02/0x08): the keys are windows into one LCD.
    // screen = device-space format (rotation applied before upload); screenLogicalW/H = how the user sees it.
    ImageFormat screen{};
    int screenLogicalW = 0, screenLogicalH = 0;
    bool windowFillCmd = false;  // supports "Update Window Image" 0x0B (full strip in one command)
    bool partialWindow = false;  // supports "Update Partial Window Image" 0x0C
    int keySafeInset = 0;        // device px the key bezel hides at the edges (measured: Plus XL 8 of 112)

    [[nodiscard]] bool HasKeyScreens() const
    {
        return key.mode != ImageMode::None;
    }
    [[nodiscard]] bool HasLcd() const
    {
        return lcdKind != LcdKind::None;
    }
    [[nodiscard]] int TotalButtons() const
    {
        return keys + touchPoints;
    }
    [[nodiscard]] bool HasFullScreen() const
    {
        return screen.mode != ImageMode::None;
    }
};

[[nodiscard]] std::span<const Model> AllModels();
[[nodiscard]] const Model* FindModel(std::uint16_t vid, std::uint16_t pid);
// "0x0084", "84", or a name: original classic mk2 scissor module15 xl xl2 module32 neo plus plusxl mini mini2 discord
// module6 pedal
[[nodiscard]] const Model* FindModelByName(const std::string& name);
// The names FindModelByName knows (the first one of each model is its token), and a model's token: what a scenario's
// "model", the studio and the wire write ("0x00C6" style for a model without a name).
struct ModelAlias
{
    std::string_view name;
    std::uint16_t pid;
};
[[nodiscard]] std::span<const ModelAlias> ModelAliases();
[[nodiscard]] std::string ModelToken(const Model& m);

// Where every key (and the window strip) sits inside the full-panel image, in logical pixels.
// Source: Elgato HID docs, "Unit Pixel Layout" drawings (2025-11). Empty colX for models without a full-panel image.
struct PanelLayout
{
    int w = 0, h = 0, key = 0;
    std::vector<int> colX, rowY;                         // left / top edge of each key column / row
    int stripX = 0, stripY = 0, stripW = 0, stripH = 0;  // window strip (Plus, Plus XL, Neo); 0 = none
    struct Rect
    {
        int x, y, w, h;
    };
    std::vector<Rect> sensors{};  // Neo touch sensors (96 x 16)
    [[nodiscard]] Rect KeyRect(int index, int cols) const
    {
        return {colX[static_cast<size_t>(index % cols)], rowY[static_cast<size_t>(index / cols)], key, key};
    }
};
[[nodiscard]] PanelLayout GetPanelLayout(const Model& m);

// Rotation helpers shared by image code and LCD coordinate mapping.
[[nodiscard]] constexpr int Degrees(Rotation r)
{
    return static_cast<int>(r) * 90;
}

// Row-major key index for (row, col); nullopt if out of range or the model has no such cell.
[[nodiscard]] inline std::optional<int> KeyIndex(const Model& m, int row, int col)
{
    if (row < 0 || col < 0 || row >= m.rows || col >= m.cols)
        return std::nullopt;
    return row * m.cols + col;
}

}  // namespace sdb
