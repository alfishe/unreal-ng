#pragma once

#include "emulator/video/map/videomapper.h"

/// The Sinclair / Pentagon screen (design §5): one 256x192 layer "zx", pixel
/// byte in the ZX interleave plus its 8x8 attribute, both in the page the ZX
/// renderer draws from. Also every mode that has no renderer of its own yet
/// (FamilyOf: drawn as ZX, described as ZX)
class ZxVideoMapper final : public videomap::IVideoMapper
{
public:
    const char* Family() const override { return "zx"; }
    videomap::VideoLayout Layout(const videomap::VideoState& s) const override;
    bool SourcesAt(const videomap::VideoState& s, const videomap::MemView& m, size_t layerIndex, uint32_t x, uint32_t y,
                   videomap::LayerContribution& out) const override;
    void BorderSources(const videomap::VideoState& s, videomap::LayerContribution& out) const override;
    void PixelsFor(const videomap::VideoState& s, const videomap::SourceRef& ref,
                   std::vector<videomap::SurfaceArea>& out) const override;
};
