#include "sdb/models.h"

#include <algorithm>
#include <array>
#include <cctype>

namespace sdb
{
namespace
{
using M = Mirror;
using R = Rotation;
constexpr ImageFormat Bmp(int s, R r, M m)
{
    return {ImageMode::Bmp, s, s, r, m};
}
constexpr ImageFormat Jpg(int s, R r, M m)
{
    return {ImageMode::Jpeg, s, s, r, m};
}

// name, family, keys, rows, cols, dials, touch
// Sources: Elgato HID docs (docs.elgato.com/streamdeck/hid, 2026) for every model they cover; crate elgato-streamdeck
// 0.13 for the Original v1 (0x0060, not in the docs). Rotations: the docs' "180" = our Mirror::Both, "90 CCW" = Rot270,
// "90 CW" = Rot90 (applied before resize; see image.cpp).
Model MakeModel(std::uint16_t pid, std::string_view name, Family f, std::uint8_t keys, std::uint8_t rows,
                std::uint8_t cols, std::uint8_t dials, std::uint8_t touch, ImageFormat key)
{
    Model x{pid, name, f, keys, rows, cols, dials, touch, key};
    return x;
}
Model WithScreen(Model x, int w, int h, Rotation r, Mirror mi)
{
    const bool swap = r == Rotation::R90 || r == Rotation::R270;
    x.screen = {ImageMode::Jpeg, swap ? h : w, swap ? w : h, r, mi};
    x.screenLogicalW = w;
    x.screenLogicalH = h;
    return x;
}
Model WithWindow(Model x, LcdKind k, int w, int h, Rotation r, bool fillCmd, bool partial)
{
    const bool swap = r == Rotation::R90 || r == Rotation::R270;
    x.lcdKind = k;
    x.lcd = {ImageMode::Jpeg, swap ? h : w, swap ? w : h, r, Mirror::None};
    x.lcdLogicalW = w;
    x.lcdLogicalH = h;
    x.windowFillCmd = fillCmd;
    x.partialWindow = partial;
    return x;
}

Model Safe(Model x, int inset)
{
    x.keySafeInset = inset;
    return x;
}

const std::array<Model, 16> kModels{{
    MakeModel(0x0060, "Stream Deck Original", Family::V1, 15, 3, 5, 0, 0, Bmp(72, R::R0, M::Both)),
    WithScreen(MakeModel(0x006D, "Stream Deck Original V2", Family::V2, 15, 3, 5, 0, 0, Jpg(72, R::R0, M::Both)), 480,
               272, R::R0, M::Both),
    WithScreen(MakeModel(0x0080, "Stream Deck MK.2", Family::V2, 15, 3, 5, 0, 0, Jpg(72, R::R0, M::Both)), 480, 272,
               R::R0, M::Both),
    WithScreen(MakeModel(0x00A5, "Stream Deck MK.2 Scissor", Family::V2, 15, 3, 5, 0, 0, Jpg(72, R::R0, M::Both)), 480,
               272, R::R0, M::Both),
    WithScreen(MakeModel(0x00B9, "Stream Deck MK.2 Module", Family::V2, 15, 3, 5, 0, 0, Jpg(72, R::R0, M::Both)), 480,
               272, R::R0, M::Both),
    MakeModel(0x0063, "Stream Deck Mini", Family::V1, 6, 2, 3, 0, 0, Bmp(80, R::R90, M::X)),
    MakeModel(0x0090, "Stream Deck Mini MK.2", Family::V1, 6, 2, 3, 0, 0, Bmp(80, R::R90, M::X)),
    MakeModel(0x00B3, "Stream Deck Mini Discord", Family::V1, 6, 2, 3, 0, 0, Bmp(80, R::R90, M::X)),
    MakeModel(0x00B8, "Stream Deck Mini MK.2 Module", Family::V1, 6, 2, 3, 0, 0, Bmp(80, R::R90, M::X)),
    WithScreen(MakeModel(0x006C, "Stream Deck XL", Family::V2, 32, 4, 8, 0, 0, Jpg(96, R::R0, M::Both)), 1024, 600,
               R::R0, M::Both),
    WithScreen(MakeModel(0x008F, "Stream Deck XL V2", Family::V2, 32, 4, 8, 0, 0, Jpg(96, R::R0, M::Both)), 1024, 600,
               R::R0, M::Both),
    WithScreen(MakeModel(0x00BA, "Stream Deck XL V2 Module", Family::V2, 32, 4, 8, 0, 0, Jpg(96, R::R0, M::Both)), 1024,
               600, R::R0, M::Both),
    WithWindow(WithScreen(MakeModel(0x009A, "Stream Deck Neo", Family::V2, 8, 2, 4, 0, 2, Jpg(96, R::R0, M::Both)), 480,
                          320, R::R0, M::Both),
               LcdKind::Fill, 248, 58, R::R180, true, true),
    MakeModel(0x0086, "Stream Deck Pedal", Family::V2, 3, 1, 3, 0, 0, {}),
    WithWindow(WithScreen(MakeModel(0x0084, "Stream Deck +", Family::V2, 8, 2, 4, 4, 0, Jpg(120, R::R0, M::None)), 800,
                          480, R::R0, M::None),
               LcdKind::Region, 800, 100, R::R0, true, true),
    // Plus XL: keys are 112 px (docs + the device's own Get Unit Information), not the crate's 120.
    Safe(WithWindow(
             WithScreen(MakeModel(0x00C6, "Stream Deck + XL", Family::V2, 36, 4, 9, 6, 0, Jpg(112, R::R270, M::None)),
                        1280, 800, R::R270, M::None),
             LcdKind::Region, 1200, 100, R::R270, true, true),
         8),
}};

// The first name of each model is its token (ModelToken).
constexpr ModelAlias kAliases[] = {{"original", 0x0060}, {"classic", 0x0080},  {"mk2", 0x0080},  {"original2", 0x006D},
                                   {"scissor", 0x00A5},  {"module15", 0x00B9}, {"xl", 0x006C},   {"xl2", 0x008F},
                                   {"module32", 0x00BA}, {"neo", 0x009A},      {"plus", 0x0084}, {"+", 0x0084},
                                   {"plusxl", 0x00C6},   {"+xl", 0x00C6},      {"mini", 0x0063}, {"mini2", 0x0090},
                                   {"discord", 0x00B3},  {"module6", 0x00B8},  {"pedal", 0x0086}};
}  // namespace

std::span<const ModelAlias> ModelAliases()
{
    return kAliases;
}

std::string ModelToken(const Model& m)
{
    for (const auto& [a, pid] : kAliases)
        if (pid == m.pid)
            return std::string(a);
    static constexpr char kHex[] = "0123456789ABCDEF";
    std::string t = "0x";
    for (int shift = 12; shift >= 0; shift -= 4)
        t += kHex[(m.pid >> shift) & 0xF];
    return t;
}

std::span<const Model> AllModels()
{
    return kModels;
}

const Model* FindModel(std::uint16_t vid, std::uint16_t pid)
{
    if (vid != kElgatoVid)
        return nullptr;
    auto it = std::ranges::find(kModels, pid, &Model::pid);
    return it == kModels.end() ? nullptr : &*it;
}

const Model* FindModelByName(const std::string& n0)
{
    std::string n;
    for (char c : n0)
        n += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    for (const auto& [a, pid] : kAliases)
        if (n == a)
            return FindModel(kElgatoVid, pid);
    try
    {
        size_t used = 0;
        const unsigned long v = std::stoul(n, &used, 16);
        if (used == n.size() && v <= 0xFFFF)
            return FindModel(kElgatoVid, static_cast<std::uint16_t>(v));
    }
    catch (...)
    {
    }
    return nullptr;
}

namespace
{
std::vector<int> Edges(int start, int size, std::initializer_list<int> gaps)
{
    std::vector<int> v{start};
    for (int g : gaps)
        v.push_back(v.back() + size + g);
    return v;
}
}  // namespace

PanelLayout GetPanelLayout(const Model& m)
{
    PanelLayout l;
    switch (m.pid)
    {
        case 0x006D:
        case 0x0080:
        case 0x00A5:
        case 0x00B9:  // Classic 15 keys, 480 x 272
            l = {480, 272, 72, Edges(11, 72, {25, 25, 25, 25}), Edges(5, 72, {25, 25})};
            break;
        case 0x006C:
        case 0x008F:
        case 0x00BA:  // XL 32 keys, 1024 x 600
            l = {1024, 600, 96, Edges(14, 96, {32, 32, 32, 32, 32, 32, 32}), Edges(47, 96, {39, 39, 39})};
            break;
        case 0x009A:  // Neo, 480 x 320, window and two 96 x 16 sensors below
            l = {480, 320, 96, Edges(3, 96, {30, 30, 30}), Edges(9, 96, {30})};
            l.stripX = 116;
            l.stripY = 262;
            l.stripW = 248;
            l.stripH = 58;
            l.sensors = {{3, 283, 96, 16}, {381, 283, 96, 16}};
            break;
        case 0x0084:  // Plus, 800 x 480, window 800 x 100 at the bottom
            l = {800, 480, 120, Edges(13, 120, {99, 99, 99}), Edges(12, 120, {40})};
            l.stripX = 0;
            l.stripY = 380;
            l.stripW = 800;
            l.stripH = 100;
            break;
        case 0x00C6:  // Plus XL, 1280 x 800, gaps alternate 31 / 32, window 1200 x 100
            l = {1280, 800, 112, Edges(11, 112, {31, 32, 31, 32, 31, 32, 31, 32}), Edges(32, 112, {32, 30, 32})};
            l.stripX = 40;
            l.stripY = 674;
            l.stripW = 1200;
            l.stripH = 100;
            break;
        case 0x0063:
        case 0x0090:
        case 0x00B3:
        case 0x00B8:  // Mini 320 x 240: no full-panel command, keys only
            l = {320, 240, 80, Edges(14, 80, {28, 27}), Edges(26, 80, {28})};
            break;
        default:
            break;
    }
    return l;
}

}  // namespace sdb
