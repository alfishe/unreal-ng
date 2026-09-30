#pragma once

#include "emulator/video/map/videomapper.h"

/// ATM Turbo 2+ / ATM3 / ZX-Evo extended modes (design §5), as ScreenAtm draws
/// them, from atmgeometry.h:
///   atm16: 320x200, four linear planes {videoPage - 4, videoPage} x {+0, +0x2000}, #FF palette;
///   atmhr: 640x200, pixel byte + 8x1 attribute, even / odd byte groups from +0 / +0x2000;
///   atmtx: 80x25 text, char code in videoPage, attribute in videoPage - 4, built-in font;
///   atmtl: 80x25 text in the dedicated page 8 / 10 (ZX-Evo)
class AtmVideoMapper final : public videomap::IVideoMapper
{
public:
    const char* Family() const override { return "atm"; }
    videomap::VideoLayout Layout(const videomap::VideoState& s) const override;
    bool SourcesAt(const videomap::VideoState& s, const videomap::MemView& m, size_t layerIndex, uint32_t x, uint32_t y,
                   videomap::LayerContribution& out) const override;
    void BorderSources(const videomap::VideoState& s, videomap::LayerContribution& out) const override;
    void PixelsFor(const videomap::VideoState& s, const videomap::SourceRef& ref,
                   std::vector<videomap::SurfaceArea>& out) const override;
    bool TextAt(const videomap::VideoState& s, const videomap::MemView& m, size_t layerIndex, uint32_t col, uint32_t row,
                videomap::TextCell& out) const override;

private:
    /// Code and attribute sources of text cell (n, r) in ATMTX / ATMTL
    static void TextCellSources(const videomap::VideoState& s, uint32_t n, uint32_t r, videomap::SourceRef& code,
                                videomap::SourceRef& attr);
};
