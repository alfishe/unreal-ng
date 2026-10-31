#pragma once

/// @file l2gallery.h
/// @brief The gallery of the analysis layers (docs/emulator/analysis/analysis-layers.md, "The gallery"): PNG
/// previews of the graphics L2 found (fonts, sprites, tiles, screens and back buffers, level maps), drawn from the
/// memory at the end of the replay (the start's when the page was not mapped then) with the claim's geometry, so a
/// reader sees at once whether the width, the height, the frames and the mask are right. Deterministic: the same
/// claims and memory give the same PNG bytes on every compiler (integer drawing, lodepng's default encoder).
///
/// Drawing per kind:
/// - sprites, tiles, fonts ("rows"): the frames in a grid (`columns` a row, a 1-pixel grey gap), ink black on white
///   paper; a tile's attribute byte (geometry.prefix) colours its frame; masked frames show the transparent pixels
///   (mask bit set, graphic bit clear) light grey;
/// - screen pixels ("screen"): the display file's row order, 256 pixels wide, black on white;
/// - attributes ("attributes") and level maps ("cells"): each byte an 8 x 8 cell, its paper colour with a 4 x 4 ink
///   square in the middle (an attribute's colours; a map's cells are drawn as if they were attributes).

#include <cstdint>
#include <string>
#include <vector>

#include "debugger/analyzers/analysis/l1model.h"
#include "debugger/analyzers/analysis/l2model.h"

namespace analysis
{

struct GalleryOptions
{
    uint16_t scale = 2;        ///< integer zoom, 1..8
    uint16_t columns = 8;      ///< frames a row (fonts: 16)
    uint16_t maxFrames = 128;  ///< the most frames drawn
};

struct GalleryImage
{
    uint32_t width = 0;   ///< PNG pixels
    uint32_t height = 0;
    uint16_t frames = 0;  ///< frames drawn
    std::vector<uint8_t> png;
};

/// The claims the gallery can draw (sprites, tiles, fonts, screens, level maps with a geometry it can lay out),
/// in the store's order
std::vector<const Region*> GalleryRegions(const L2Result& l2);

/// Draw @p region; false (and @p error) when its geometry cannot be drawn or the encoder failed
bool RenderGallery(const L1Result& l1, const Region& region, const GalleryOptions& options, GalleryImage& image,
                   std::string& error);

}  // namespace analysis
