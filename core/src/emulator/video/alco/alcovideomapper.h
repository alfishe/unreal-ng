#pragma once

#include "emulator/video/map/videomapper.h"

/// Pentagon 1024 / ZX-Evo AlCo modes (design §5), as ScreenAlco draws them:
///   p16: four planes at {videoPage ^ 1, videoPage} x {+0, +0x2000} in the ZX
///        layout, one byte per pixel pair, #FF palette;
///   pmc: bitmap at the ZX pixel address, one attribute per 8x1 cell at +0x2000
class AlcoVideoMapper final : public videomap::IVideoMapper
{
public:
    const char* Family() const override { return "alco"; }
    videomap::VideoLayout Layout(const videomap::VideoState& s) const override;
    bool SourcesAt(const videomap::VideoState& s, const videomap::MemView& m, size_t layerIndex, uint32_t x, uint32_t y,
                   videomap::LayerContribution& out) const override;
    void BorderSources(const videomap::VideoState& s, videomap::LayerContribution& out) const override;
    void PixelsFor(const videomap::VideoState& s, const videomap::SourceRef& ref,
                   std::vector<videomap::SurfaceArea>& out) const override;
};
