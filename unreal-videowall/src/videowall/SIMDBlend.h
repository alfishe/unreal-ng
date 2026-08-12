#pragma once

#include <QImage>
#include <vector>

namespace videowall {
namespace simd {

/**
 * @brief Applies the smart phosphor temporal blur using SIMD intrinsics.
 * 
 * @param current The current frame to blend (will be modified in-place).
 * @param history The list of historical frames (including the just-pushed current frame).
 * @param depth The number of frames to blend (from history).
 * @param lumaThreshold The threshold for luma difference to consider pixels similar.
 * @param weights Precalculated weights for blending.
 * @param weightSum Sum of the weights for normalization.
 * @param smartEnabled If false, ignores the luma threshold mask and blends unconditionally.
 */
void applySmartPhosphorBlend(
    QImage& current,
    const std::vector<const QImage*>& history,
    int depth,
    int lumaThreshold,
    const std::vector<int>& weights,
    int weightSum,
    bool smartEnabled);

} // namespace simd
} // namespace videowall
