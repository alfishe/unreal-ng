#pragma once

/// @file nextreportquery.h
/// @brief The arguments of the Next's debugger reports (docs/inprogress/2026-10-07-zx-next/design-automation-coverage.md): the
/// query structs of DeviceState::NextPalette / NextPorts / NextRegRead (and the copper and sprites pages) and the one parser of
/// their text form. The WebAPI query string, the CLI `key=value` words, the MCP parameters, Lua and Python all end in these
/// `...FromStrings` functions, so a bad argument is refused with the same sentence on every plane.

#include <cstdint>
#include <string>

/// `next_palette`: which palette (NR #43 order 0-7, a name, `all`, or the selected one) and which entries
struct NextPaletteQuery
{
    static constexpr int kSelected = -1;  ///< the palette NR #43 bits 6:4 select for reading and writing
    static constexpr int kAll = 8;
    int palette = kSelected;
    unsigned first = 0;
    unsigned last = 255;
};

/// `palette`: 0-7, ula_1 layer2_1 sprites_1 tilemap_1 ula_2 layer2_2 sprites_2 tilemap_2, all, selected (or empty);
/// `range`: `N` or `A-B` (decimal, or hex with 0x / #), empty = 0-255
bool NextPaletteQueryFromStrings(const std::string& palette, const std::string& range, NextPaletteQuery& query, std::string& error);
/// The palette's name in NR #43 bits 6:4 order
const char* NextPaletteName(unsigned palette);

/// `next_ports`: the whole enable word, plus the description of one port access when `describe`
struct NextPortsQuery
{
    bool describe = false;
    uint16_t port = 0;
    bool write = false;
};

/// `port`: hex (6B, 0x253B, #E3, 253Bh), empty = only the enable word; `access`: r, read, w, write (empty = read)
bool NextPortsQueryFromStrings(const std::string& port, const std::string& access, NextPortsQuery& query, std::string& error);

/// `next_nextreg`: one register, or all of them (only the ones that differ from their reset with `changed`)
struct NextRegReadQuery
{
    int reg = -1;  ///< -1 = all
    bool changed = false;
};

/// `reg`: hex register number (07, 0x07, #07), empty = all; `changed`: true / false / 1 / 0 / on / off
bool NextRegReadQueryFromStrings(const std::string& reg, const std::string& changed, NextRegReadQuery& query, std::string& error);

/// Shared by the parsers and the write control: a hex number with an optional `#`, `0x` or `h` marker; false when it is not a number or does not fit `maxValue`
bool ParseNextHex(const std::string& text, uint32_t maxValue, uint32_t& value);
bool ParseNextBool(const std::string& text, bool& value);
