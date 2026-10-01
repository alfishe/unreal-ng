#pragma once

/// @file videomapper.h
/// @brief One mapper per video family (design §4.2): the debug description of
/// what that family's renderer draws. A mapper reads only the VideoState it is
/// given and memory through MemView - never live registers, never the framebuffer.

#include <vector>

#include "emulator/video/map/videomap.h"

class Memory;

namespace videomap
{
/// Read-only view of the machine's memory for the mappers
class MemView
{
public:
    /// @param atmFontRam the text font RAM (EmulatorState::atmFontRam, code * 8 + row); its table offsets are the
    /// font's own layout, row * 256 + code
    explicit MemView(Memory* memory, const uint8_t* atmFontRam = nullptr) : _memory(memory), _atmFontRam(atmFontRam) {}
    /// Byte of a memory source (Ram, InternalTable); 0 for spaces without storage here
    uint8_t Read(const SourceRef& ref) const;

private:
    Memory* _memory = nullptr;
    const uint8_t* _atmFontRam = nullptr;
};

class IVideoMapper
{
public:
    virtual ~IVideoMapper() = default;

    virtual const char* Family() const = 0;
    virtual VideoLayout Layout(const VideoState& s) const = 0;
    /// Sources of pixel (x, y) of layer `layerIndex` (surface pixels); false outside the surface
    virtual bool SourcesAt(const VideoState& s, const MemView& m, size_t layerIndex, uint32_t x, uint32_t y,
                           LayerContribution& out) const = 0;
    /// Border colour: its register / palette sources
    virtual void BorderSources(const VideoState& s, LayerContribution& out) const = 0;
    /// Every area of every layer the byte feeds (empty: it feeds nothing)
    virtual void PixelsFor(const VideoState& s, const SourceRef& ref, std::vector<SurfaceArea>& out) const = 0;
    /// Text cell (col, row) of a text layer; false for bitmap layers
    virtual bool TextAt(const VideoState& s, const MemView& m, size_t layerIndex, uint32_t col, uint32_t row,
                        TextCell& out) const
    {
        (void)s; (void)m; (void)layerIndex; (void)col; (void)row; (void)out;
        return false;
    }
};

/// Frame geometry only: a mode no family describes (design §8: never ZX rules for a non-ZX mode)
class NullVideoMapper final : public IVideoMapper
{
public:
    const char* Family() const override { return "none"; }
    VideoLayout Layout(const VideoState& s) const override;
    bool SourcesAt(const VideoState&, const MemView&, size_t, uint32_t, uint32_t, LayerContribution&) const override
    {
        return false;
    }
    void BorderSources(const VideoState&, LayerContribution&) const override {}
    void PixelsFor(const VideoState&, const SourceRef&, std::vector<SurfaceArea>&) const override {}
};
} // namespace videomap
