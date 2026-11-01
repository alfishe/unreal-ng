// frame-render.cpp - see frame-render.h.
//
// kMonitorUtf8 below is generated data (0x00 -> '.', 0x01..0x1F CP437
// control pictures, 0x20..0xFF CP866) - regenerate, do not hand-edit.
#include "tui/frame-render.h"

#include <string>
#include <utility>
#include <vector>

namespace dbg {

namespace {

const char* const kMonitorUtf8[256] = {
    "\x2E", "\xE2\x98\xBA", "\xE2\x98\xBB", "\xE2\x99\xA5", "\xE2\x99\xA6", "\xE2\x99\xA3", "\xE2\x99\xA0", "\xE2\x80\xA2",
    "\xE2\x97\x98", "\xE2\x97\x8B", "\xE2\x97\x99", "\xE2\x99\x82", "\xE2\x99\x80", "\xE2\x99\xAA", "\xE2\x99\xAB", "\xE2\x98\xBC",
    "\xE2\x96\xBA", "\xE2\x97\x84", "\xE2\x86\x95", "\xE2\x80\xBC", "\xC2\xB6", "\xC2\xA7", "\xE2\x96\xAC", "\xE2\x86\xB5",
    "\xE2\x86\x91", "\xE2\x86\x93", "\xE2\x86\x92", "\xE2\x86\x90", "\xE2\x88\x9F", "\xE2\x86\x94", "\xE2\x96\xB2", "\xE2\x96\xBC",
    "\x20", "\x21", "\x22", "\x23", "\x24", "\x25", "\x26", "\x27",
    "\x28", "\x29", "\x2A", "\x2B", "\x2C", "\x2D", "\x2E", "\x2F",
    "\x30", "\x31", "\x32", "\x33", "\x34", "\x35", "\x36", "\x37",
    "\x38", "\x39", "\x3A", "\x3B", "\x3C", "\x3D", "\x3E", "\x3F",
    "\x40", "\x41", "\x42", "\x43", "\x44", "\x45", "\x46", "\x47",
    "\x48", "\x49", "\x4A", "\x4B", "\x4C", "\x4D", "\x4E", "\x4F",
    "\x50", "\x51", "\x52", "\x53", "\x54", "\x55", "\x56", "\x57",
    "\x58", "\x59", "\x5A", "\x5B", "\x5C", "\x5D", "\x5E", "\x5F",
    "\x60", "\x61", "\x62", "\x63", "\x64", "\x65", "\x66", "\x67",
    "\x68", "\x69", "\x6A", "\x6B", "\x6C", "\x6D", "\x6E", "\x6F",
    "\x70", "\x71", "\x72", "\x73", "\x74", "\x75", "\x76", "\x77",
    "\x78", "\x79", "\x7A", "\x7B", "\x7C", "\x7D", "\x7E", "\x7F",
    "\xD0\x90", "\xD0\x91", "\xD0\x92", "\xD0\x93", "\xD0\x94", "\xD0\x95", "\xD0\x96", "\xD0\x97",
    "\xD0\x98", "\xD0\x99", "\xD0\x9A", "\xD0\x9B", "\xD0\x9C", "\xD0\x9D", "\xD0\x9E", "\xD0\x9F",
    "\xD0\xA0", "\xD0\xA1", "\xD0\xA2", "\xD0\xA3", "\xD0\xA4", "\xD0\xA5", "\xD0\xA6", "\xD0\xA7",
    "\xD0\xA8", "\xD0\xA9", "\xD0\xAA", "\xD0\xAB", "\xD0\xAC", "\xD0\xAD", "\xD0\xAE", "\xD0\xAF",
    "\xD0\xB0", "\xD0\xB1", "\xD0\xB2", "\xD0\xB3", "\xD0\xB4", "\xD0\xB5", "\xD0\xB6", "\xD0\xB7",
    "\xD0\xB8", "\xD0\xB9", "\xD0\xBA", "\xD0\xBB", "\xD0\xBC", "\xD0\xBD", "\xD0\xBE", "\xD0\xBF",
    "\xE2\x96\x91", "\xE2\x96\x92", "\xE2\x96\x93", "\xE2\x94\x82", "\xE2\x94\xA4", "\xE2\x95\xA1", "\xE2\x95\xA2", "\xE2\x95\x96",
    "\xE2\x95\x95", "\xE2\x95\xA3", "\xE2\x95\x91", "\xE2\x95\x97", "\xE2\x95\x9D", "\xE2\x95\x9C", "\xE2\x95\x9B", "\xE2\x94\x90",
    "\xE2\x94\x94", "\xE2\x94\xB4", "\xE2\x94\xAC", "\xE2\x94\x9C", "\xE2\x94\x80", "\xE2\x94\xBC", "\xE2\x95\x9E", "\xE2\x95\x9F",
    "\xE2\x95\x9A", "\xE2\x95\x94", "\xE2\x95\xA9", "\xE2\x95\xA6", "\xE2\x95\xA0", "\xE2\x95\x90", "\xE2\x95\xAC", "\xE2\x95\xA7",
    "\xE2\x95\xA8", "\xE2\x95\xA4", "\xE2\x95\xA5", "\xE2\x95\x99", "\xE2\x95\x98", "\xE2\x95\x92", "\xE2\x95\x93", "\xE2\x95\xAB",
    "\xE2\x95\xAA", "\xE2\x94\x98", "\xE2\x94\x8C", "\xE2\x96\x88", "\xE2\x96\x84", "\xE2\x96\x8C", "\xE2\x96\x90", "\xE2\x96\x80",
    "\xD1\x80", "\xD1\x81", "\xD1\x82", "\xD1\x83", "\xD1\x84", "\xD1\x85", "\xD1\x86", "\xD1\x87",
    "\xD1\x88", "\xD1\x89", "\xD1\x8A", "\xD1\x8B", "\xD1\x8C", "\xD1\x8D", "\xD1\x8E", "\xD1\x8F",
    "\xD0\x81", "\xD1\x91", "\xD0\x84", "\xD1\x94", "\xD0\x87", "\xD1\x97", "\xD0\x8E", "\xD1\x9E",
    "\xC2\xB0", "\xE2\x88\x99", "\xC2\xB7", "\xE2\x88\x9A", "\xE2\x84\x96", "\xC2\xA4", "\xE2\x96\xA0", "\xC2\xA0",
};

ftxui::Color ToColor(const Rgb& rgb) {
    return ftxui::Color(rgb.r, rgb.g, rgb.b);
}

// One row = one hbox of attribute runs. Grouping matters: one text node per
// run keeps the element tree two orders of magnitude smaller than per-cell.
ftxui::Element RenderRow(const TextScreen& screen, const Palette& palette, int y) {
    std::vector<ftxui::Element> runs;
    const int width = screen.Width();
    int x = 0;
    while (x < width) {
        const uint8_t attr = screen.AttrAt(x, y);
        const bool transparent = attr == kAttrTransparent;
        std::string text;
        while (x < width && screen.AttrAt(x, y) == attr) {
            text += Utf8OfCode(screen.CharAt(x, y));
            ++x;
        }
        if (transparent) {
            runs.push_back(ftxui::text(std::move(text)));
        } else {
            const Rgb ink = palette.ColorOf(InkIndex(attr));
            const Rgb paper = palette.ColorOf(PaperIndex(attr));
            runs.push_back(ftxui::text(std::move(text)) | ftxui::color(ToColor(ink)) |
                           ftxui::bgcolor(ToColor(paper)));
        }
    }
    return ftxui::hbox(std::move(runs));
}

}  // namespace

std::string Utf8OfCode(uint8_t code) { return kMonitorUtf8[code]; }

ftxui::Element RenderFrame(const TextScreen& screen, const Palette& palette) {
    std::vector<ftxui::Element> rows;
    rows.reserve(static_cast<size_t>(screen.Height()));
    for (int y = 0; y < screen.Height(); ++y) {
        rows.push_back(RenderRow(screen, palette, y));
    }
    return ftxui::vbox(std::move(rows));
}

}  // namespace dbg
