#pragma once

#include "emulator/video/smoothmotion.h"

#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

namespace smoothmotion
{

/// Layers (POC 039, `layers`): the in-between picture of a pair A, B at the weight w, every pixel drawn by the layer that
/// best explains its neighbourhood over the frames k and k + 1 (k - 1 for the still and steady checks).
///
/// A layer is a vector: zero (a static layer) or one of the block fields' vectors k -> k + 1 and k - 1 -> k (the
/// common vectors of both fields everywhere, the 3 x 3 blocks' own nearby). At a pixel x at the weight w, a vector v
/// samples a(x - wv) and b(x - wv + v):
/// - it fits when the samples agree and v carries the pixel's own colour in one frame (a(x) to b(x + v) or b(x) from
///   a(x - v)): a static layer over motion keeps its pixels, a recoloured or blinking one fits nothing;
/// - a moving v needs support: a pixel in the support window where it fits and explains a change both ways (zero fails
///   there), else uniform paper "moves" anywhere and carries a blinking sprite's paper over it.
/// The window score of a vector is the count of fitting pixels round the pixel. A block's pixels take the vector most of
/// them fit (no a/b mosaic); the rules in Compose settle ties, look-alikes and holes. Nothing fits: the nearer frame.
///
/// Known limit: zero fits only a pixel the same in k - 1, k and k + 1, so the background a sprite left between k - 1
/// and k is not still; over random background of few colours a moving vector fits it by chance (the block composition
/// does better there; over paper or drawn graphics both are exact).
///
/// The CPU reference: integer arithmetic, the same bits on every compiler. Measured on the synthetic suite of
/// POC 039 (tools/poc/039-smoothing-filters): 24.5 % wrong over the 10-scene sum, MotionField::Compose 118 %.
class LayerFlow
{
public:
    using V = MotionField::Vector;

    static constexpr int kRadius = 5;          ///< the score's window
    static constexpr int kSupportRadius = 16;  ///< the support's window: sparse content has its changes far apart
    static constexpr size_t kCommon = 8;       ///< the common vectors, zero included
    static constexpr size_t kPalette = 32;     ///< the colours of A the cover rule tells apart
    static constexpr int kNear = 8;            ///< nearTies: the main motion within 1/kNear of a block's best fit
    static constexpr int kPeriodic = 20;       ///< nearStill: a block's pixels unlike itself half the main step on
    static constexpr int kDominant = 3, kDominantOf = 4;  ///< nearStill: the main motion's share of the moving blocks
    static constexpr int kInkTile = 24;        ///< keepInk's tile: a few dots' worth of a dot cloud
    static constexpr int kInkOff = 4;          ///< keepInk: a tile off by more than 1/kInkOff of its ink falls back

    /// The common vectors of the two fields: zero, then the moving vectors at least two matched blocks share, most
    /// shared first (equals in the order the blocks are met: `next` row by row, then `prev`)
    static std::vector<V> CommonVectors(const MotionField& prev, const MotionField& next);

    /// The picture between `a` and `b` at the weight `w` (0..kOne) into `out` (a's size). `p` is the frame before
    /// `a`; `prev` the field p -> a, `next` the field a -> b, both estimated over MotionField::kRangeFrames
    void Compose(const Picture& p, const Picture& a, const Picture& b, const MotionField& prev, const MotionField& next,
                 int w, uint32_t* out);
    /// Compose's per-vector and per-pixel passes split over the pool's threads (null: the caller's alone); the same
    /// picture either way
    void SetPool(ThreadPool* pool) { _pool = pool; }

    // Compose's steps a device shares: the CPU prepares the vectors and takes the block decisions from the counts
    // and answers of the per-pixel passes, wherever they ran

    /// The vectors of a refresh
    struct Layers
    {
        std::vector<V> all;  ///< every vector some pixel may take: the common ones, then the blocks' (next, prev)
        size_t common = 0;   ///< the common ones at the front of `all`
        int reach = 0;       ///< the longest component of a vector
        int blocksWide = 0, blocksHigh = 0;
        std::vector<std::vector<int>> allowed;  ///< per block: the common vectors, then the 3 x 3 blocks' (next, prev)
    };
    static void Prepare(const MotionField& prev, const MotionField& next, int width, int height, Layers& layers);

    /// The colours the cover rule tells apart: a's first kPalette colours row by row. `colourAt` (may be null): per
    /// pixel its index, 255 past the palette
    static void Palette(const Picture& a, std::vector<uint32_t>& palette, uint8_t* colourAt);

    /// A block of a tied region and its neighbour at (di, dj), decided alone
    struct Edge
    {
        int bi, bj, di, dj;
    };

    /// The per-pixel answers the block decisions ask (the CPU's buffers, a device's passes): every question announced
    /// first, so a device answers all at once
    class Evidence
    {
    public:
        virtual ~Evidence() = default;
        /// The questions: the blocks the aliases rule asks about, each with its vector; the regions' edges
        virtual void Ask(const std::vector<std::pair<size_t, int>>& /*blocks*/, const std::vector<Edge>& /*edges*/) {}
        /// The aliases rule's own(c, s) in `block` (s < 0: none): the pixels c fits and s does not, less those another
        /// vector fitting them explains with more support
        virtual int Own(size_t block, int c, int s) = 0;
        /// Whether the neighbour's layer, its vector `vector`, continues into the block: edges[index]
        virtual bool Continues(size_t index, int vector) = 0;
    };

    /// The block decisions: per block the vector its pixels take (-1: none) and whether others tie with it.
    /// `fitCounts`, `usableCounts`: per vector c and block, the pixels c fits and may take, at c * blocks + block
    /// `a`: the frame the field starts from (nearStill's periodic test)
    static void Decide(const Layers& layers, const Picture& a, const uint8_t* fitCounts, const uint8_t* usableCounts,
                       Evidence& evidence, std::vector<int>& blockVector, std::vector<uint8_t>& tied);

    /// carryFirst's test: `v` explains at least half of the block's changes from a to b
    static bool Explains(const Picture& a, const Picture& b, int bi, int bj, V v);

    /// keepInk: per kInkTile tile, ink is a tile's pixels other than its paper (a's most frequent colour there). A
    /// tile of `out` with less ink than both a and b, or more than both, by over 1/kInkOff (at least a pixel) shows
    /// no motion of what is there (a cloud of dots, each its own motion: the layers' vectors wipe half of them) and
    /// takes `nearer` instead. The CPU's and the GPU's pictures both go through it
    static void KeepInk(const Picture& a, const Picture& b, const Picture& nearer, uint32_t* out);

private:
    // kept from call to call: a refresh allocates nothing once the sizes settle
    std::vector<uint32_t> _paddedP, _paddedA, _paddedB;
    std::vector<uint8_t> _fits, _score, _usable, _changed, _explained, _calm;
    std::vector<uint16_t> _support;
    std::vector<int> _column, _sums, _unexplained;
    Layers _layers;
    ThreadPool* _pool = nullptr;
    static constexpr int kVectorParts = 8;  ///< the per-vector pass's split over a pool
    static constexpr int kBandParts = 12;   ///< the pixels' split, in block rows
    /// A split part's own buffers in the per-vector pass
    struct Part
    {
        std::vector<uint8_t> fitting, changes, explained;
        std::vector<int> column;
    };
    std::vector<Part> _parts;
};

}  // namespace smoothmotion
