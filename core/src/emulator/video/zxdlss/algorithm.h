#pragma once

/// @file algorithm.h
/// @brief ZX DLSS de-flicker algorithms: the interface every algorithm
/// implements and the registry that creates them by name.
///
/// Specification of the first algorithm, mod-tpgw:
/// docs/inprogress/2026-09-27-zxdlss-gigascreen/algorithm-mod-tpgw.md

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace zxdlss
{

/// One emulated frame as the renderer describes it (plane B, see p0a-plane-b.md).
/// All arrays are width x height, row-major.
struct FrameInput
{
    int width = 0;
    int height = 0;
    const uint8_t* plane = nullptr;    ///< color index 0..15 (plane B bits 8..11)
    const uint8_t* attr = nullptr;     ///< attribute byte the beam used (bits 0..7), 0 on the border
    const uint8_t* ink = nullptr;      ///< ink bit 0 / 1 (bit 12)
    /// top-left pixel of the 256 x 192 paper: (48, 48) in the standard 352 x 288
    /// frame, (48, 56) in Pentagon overscan cropped to 352 x 304
    int paperX = 48;
    int paperY = 48;
    /// The 16 ZX colors as the emulator draws them now (required): RGBA8888,
    /// little-endian uint32 0xAABBGGRR - Screen::GetRGBAPalette16. The algorithm
    /// mixes in these colors and rebuilds its tables when they change.
    const uint32_t* palette = nullptr;
};

/// Output picture: width x height x 3 (RGB8).
using RGBImage = std::vector<uint8_t>;

class Algorithm
{
public:
    virtual ~Algorithm() = default;

    /// Frames of look-ahead: process() of frame f returns the output for frame f - delay().
    virtual int delay() const = 0;

    /// Push the next frame; write the output for frame (pushed - delay()) to `out`
    /// (resized to width x height x 3). During the first delay() calls the output
    /// is the oldest frame held (callers discard it).
    virtual void process(const FrameInput& frame, RGBImage& out) = 0;

    /// Registry name.
    virtual std::string name() const = 0;

    /// Optional per-stage timing since creation (for zxdlss-bench), one line per stage.
    virtual std::string stats() const { return {}; }
};

/// Create a registered algorithm by name ("raw", "mod-tpgw"); nullptr if unknown.
std::unique_ptr<Algorithm> createAlgorithm(const std::string& name);

/// Names of all registered algorithms.
std::vector<std::string> algorithmNames();

/// Split a plane B frame (uint16 per pixel: attr[0:7] color[8:11] ink[12]) into
/// the three input planes.
void decodePlaneB(const uint16_t* planeB, size_t pixels, std::vector<uint8_t>& plane, std::vector<uint8_t>& attr,
                  std::vector<uint8_t>& ink);

}  // namespace zxdlss
