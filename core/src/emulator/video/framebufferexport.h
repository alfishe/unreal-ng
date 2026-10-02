#pragma once

#include <cstdint>
#include <string>
#include <vector>

class EmulatorContext;

/// @file framebufferexport.h
/// @brief The picture as raw pixels for automation (automation audit G14, PLAN #26): tests and
/// tools compare pictures without decoding a PNG. One function every interface calls (WebAPI
/// /capture/framebuffer, CLI `framebuffer save`, Lua / Python framebuffer(), MCP capture_media
/// framebuffer).
///
///   rgba:  the presented (tear-free) frame, width x height x 4 bytes R, G, B, A - the framebuffer
///          the screenshot encodes, border included;
///   index: the pen / palette index of every pixel, width x height little-endian uint16, from a
///          machine whose picture is drawn from a palette it can name (Screen::IndexedFrame: the
///          Sprinter's 2 048 pens, the state now); other machines answer an error.
///
/// Worked example (Sprinter): index of pixel (x, y) = bytes [2 (y x 736 + x)], [+1]; a value
/// below #400 is graphics palette value >> 8 entry value & #FF, #400-#7FF the text palettes.
namespace FramebufferExport
{
struct Frame
{
    uint16_t width = 0;
    uint16_t height = 0;
    std::string format;    ///< "rgba" / "index"
    std::string encoding;  ///< "u8 R,G,B,A" / "u16le pen"
    std::vector<uint8_t> bytes;
};

/// False with `error`: unknown format, no frame yet, or no indexed form on this machine
bool Capture(EmulatorContext* context, const std::string& format, Frame& frame, std::string& error);
}  // namespace FramebufferExport
