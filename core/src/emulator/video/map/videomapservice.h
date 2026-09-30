#pragma once

/// @file videomapservice.h
/// @brief The one place debug consumers ask about the picture (PLAN #42 design
/// §4.10): beam -> where, pixel -> sources, byte -> pixels, text. Picks the
/// mapper of the renderer family drawing the current mode (FamilyOf) and
/// assembles the VideoState from the machine's latches. Stateless and cheap:
/// build one per query. Phase 1 reads the machine directly (paused machine);
/// the running-machine snapshot comes with phase 3 (design §4.9).

#include <string>
#include <vector>

#include "emulator/video/map/videomapper.h"
#include "emulator/video/videofamily.h"

class EmulatorContext;

namespace videomap
{
/// Beam position plus the layer point under it
struct BeamInfo
{
    BeamPosition beam;
    bool inLayer = false;
    std::string layer;
    uint32_t x = 0, xEnd = 0, y = 0;  ///< surface pixels covered by this T
};

class VideoMapService
{
public:
    explicit VideoMapService(EmulatorContext* context);

    VideoState State() const;
    VideoLayout Layout() const;
    BeamInfo BeamAt(uint32_t tInFrame) const;
    /// Sources of surface pixel (x, y) of a layer
    PixelSources SourcesAt(size_t layerIndex, uint32_t x, uint32_t y) const;
    /// Sources of what the beam draws at a frame T: a layer pixel or the border
    PixelSources SourcesAtBeam(uint32_t tInFrame) const;
    std::vector<SurfaceArea> PixelsFor(const SourceRef& ref) const;
    /// The byte at a Z80 address under current paging (ROM and unmapped: nothing)
    std::vector<SurfaceArea> PixelsForZ80(uint16_t address) const;
    /// Text grid of a text layer; false for bitmap layers
    bool Text(size_t layerIndex, uint16_t& columns, uint16_t& rows, std::vector<TextCell>& cells) const;

    /// Z80 addresses where a RAM page byte is visible now
    std::vector<uint16_t> Z80Aliases(const SourceRef& ref) const;

    static const IVideoMapper& MapperFor(VideoFamily family);

private:
    EmulatorContext* _context = nullptr;
};
} // namespace videomap
