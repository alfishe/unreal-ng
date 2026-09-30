#pragma once

#include "emulator/video/map/videomapper.h"

/// Profi 512x240 hi-res (design §5), as ScreenProfi draws it (profigeometry.h):
/// pixel page 4 / 6, attribute page 0x38 / 0x3A at the same offset, one
/// attribute per 8x1 pixels, 16-entry palette; the monochrome option takes the
/// colours from the #FE border latch instead of the attribute page
class ProfiVideoMapper final : public videomap::IVideoMapper
{
public:
    const char* Family() const override { return "profi"; }
    videomap::VideoLayout Layout(const videomap::VideoState& s) const override;
    bool SourcesAt(const videomap::VideoState& s, const videomap::MemView& m, size_t layerIndex, uint32_t x, uint32_t y,
                   videomap::LayerContribution& out) const override;
    void BorderSources(const videomap::VideoState& s, videomap::LayerContribution& out) const override;
    void PixelsFor(const videomap::VideoState& s, const videomap::SourceRef& ref,
                   std::vector<videomap::SurfaceArea>& out) const override;
};
