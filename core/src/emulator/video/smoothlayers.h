#pragma once

#include "emulator/video/smoothmotion.h"

#include <cstdint>
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

    /// The common vectors of the two fields: zero, then the moving vectors at least two matched blocks share, most
    /// shared first (equals in the order the blocks are met: `next` row by row, then `prev`)
    static std::vector<V> CommonVectors(const MotionField& prev, const MotionField& next);

    /// The picture between `a` and `b` at the weight `w` (0..kOne) into `out` (a's size). `p` is the frame before
    /// `a`; `prev` the field p -> a, `next` the field a -> b, both estimated over MotionField::kRangeFrames
    void Compose(const Picture& p, const Picture& a, const Picture& b, const MotionField& prev, const MotionField& next,
                 int w, uint32_t* out);

private:
    // kept from call to call: a refresh allocates nothing once the sizes settle
    std::vector<uint32_t> _paddedP, _paddedA, _paddedB;
    std::vector<uint8_t> _fits, _score, _usable, _fitting, _changes, _changed, _explained, _calm;
    std::vector<uint16_t> _support;
    std::vector<int> _column, _sums, _unexplained;
};

}  // namespace smoothmotion
