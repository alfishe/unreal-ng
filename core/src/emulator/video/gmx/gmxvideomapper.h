#pragma once

#include "emulator/video/map/videomapper.h"

/// GMX 640x200 extended mode, as ScreenGmx draws it (gmxgeometry.h): pixel page #39 / #3B, attribute page #79 / #7B
/// at the same offset, 80 linear bytes per line after the scroller's start address, one standard attribute per 8x1
/// pixels, the fixed ZX colours
class GmxVideoMapper final : public videomap::IVideoMapper
{
public:
    const char* Family() const override { return "gmx"; }
    videomap::VideoLayout Layout(const videomap::VideoState& s) const override;
    bool SourcesAt(const videomap::VideoState& s, const videomap::MemView& m, size_t layerIndex, uint32_t x, uint32_t y,
                   videomap::LayerContribution& out) const override;
    void BorderSources(const videomap::VideoState& s, videomap::LayerContribution& out) const override;
    void PixelsFor(const videomap::VideoState& s, const videomap::SourceRef& ref,
                   std::vector<videomap::SurfaceArea>& out) const override;
};
