#pragma once

#include "smoothmotion.h"

#include <cstdint>
#include <memory>

namespace smoothmotion
{

/// The cheap interpolation of smooth motion (Mode::InterpolateLite): the picture between two frames for a machine
/// LayerFlow is too slow on. About 1 / 20 to 1 / 45 of LayerFlow's time on one thread, at about 1.3 times its error
/// (POC 055, tools/poc/055-interpolation-low-end: the measurements and what each rule is for).
///
/// How it differs from LayerFlow:
/// - a frame is bit planes of its colours' indices, 64 pixels a word: "these pixels are equal" for a whole shifted
///   row is a few word operations a plane. So at most 256 colours a picture (the palette is kept from frame to
///   frame and starts over when it fills);
/// - the block field is not searched anew for every pair: a pair scores zero and the vectors kept from the pairs
///   before, and a budget of the search order on the block rows still unexplained, each row from where it stopped.
///   A new motion is found over a few pairs. Of the vectors that fit a block equally the slowest wins; blocks tied
///   between the same vectors take the vote of the decided blocks round them;
/// - a pixel's layer is picked by masks: its block's vector where it fits and carries the pixel's own colour, else
///   still where the frames agree, else a vector of the blocks round it, else the nearer frame moved as the block
///   is, else the nearer frame. A moving vector takes pixels only where it explains changes near the block, and
///   outside the blocks of much detail only when confirmed, as in LayerFlow: a block matched to it is detailed or
///   carried exactly from the frame before a through a to b (or held still into a, then carried). Lines new every
///   frame match some vector within the field's tolerance by chance, never exactly over three frames. Where the
///   nearer frame's carried sample is of a block matched still, still over the three frames, and the other frame's
///   is not, the other frame's (a moving layer going under a still one).
///
/// One instance per stream of frames, pairs in the order they are shown: the kept vectors and the search's place
/// are state. A pair is estimated once, when first asked (by Picture::id); a picture with no id is taken as new at
/// every call. One thread; the same pictures on every platform (integers only).
class LiteFlow
{
public:
    LiteFlow();
    ~LiteFlow();
    LiteFlow(const LiteFlow&) = delete;
    LiteFlow& operator=(const LiteFlow&) = delete;

    static constexpr int kMaxWidth = 2048;
    static constexpr int kMaxColours = 256;
    static constexpr int kVectors = 8;      ///< the moving vectors of a pair at most, the most common
    static constexpr int kEverywhere = 2;   ///< the most common ones tried in every block
    static constexpr size_t kKept = 10;     ///< the moving vectors kept from pair to pair
    static constexpr int kBudget = 144;     ///< row scores of the search a pair
    static constexpr int kDropAfter = 25;   ///< a kept vector no block took for this many pairs goes
    static constexpr int kTied = 6;         ///< a block tied between this many vectors or more says nothing
    static constexpr int kDetail = 8;       ///< a block's pixels unlike their right and lower neighbours (each) that
                                            ///< no vector matches by chance: no confirmation asked

    /// A picture of this size can be drawn: whole 8 x 8 blocks, no wider than kMaxWidth
    static bool Fits(int width, int height);

    /// The picture at weight `w` (0..kOne) between `a` and `b` into `out` (width * height). false, `out` untouched:
    /// not a pair for it (Fits, or more than kMaxColours colours in the two frames): the caller draws another way
    bool Compose(const Picture& a, const Picture& b, int w, uint32_t* out);

    /// Forget the frames, the kept vectors and the palette (a seek, another stream)
    void Reset();

    struct Stats
    {
        uint64_t pairs = 0;      ///< fields estimated
        uint64_t rowScores = 0;  ///< block rows scored for a vector, all pairs
        uint64_t packed = 0;     ///< frames turned into planes
        uint64_t refused = 0;    ///< Compose calls that returned false
        uint64_t palettes = 0;   ///< times the palette started over
    };
    const Stats& GetStats() const;

private:
    struct Impl;
    std::unique_ptr<Impl> _impl;
};

}  // namespace smoothmotion
