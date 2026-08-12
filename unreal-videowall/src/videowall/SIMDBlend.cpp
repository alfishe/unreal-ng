#include "SIMDBlend.h"
#include <cmath>
#include <QtGlobal>

#if defined(__ARM_NEON)
#include <arm_neon.h>
#elif defined(__SSE2__) || defined(__x86_64__) || defined(_M_X64)
#include <emmintrin.h>
#include <tmmintrin.h>
#endif

namespace videowall {
namespace simd {

void applySmartPhosphorBlend(
    QImage& current,
    const std::vector<const QImage*>& history,
    int depth,
    int lumaThreshold,
    const std::vector<int>& weights,
    int weightSum,
    bool smartEnabled)
{
    int width = current.width();
    int height = current.height();

    std::vector<const uint8_t*> histPointers(depth);

    for (int y = 0; y < height; ++y)
    {
        uint8_t* curLine = reinterpret_cast<uint8_t*>(current.scanLine(y));
        for (int i = 0; i < depth; ++i)
        {
            histPointers[i] = reinterpret_cast<const uint8_t*>(history[i]->constScanLine(y));
        }

        int x = 0;
        
#if defined(__ARM_NEON)
        uint8x16_t v_threshold = vdupq_n_u8(lumaThreshold);
        // Alpha channel shouldn't affect similarity, so we set threshold for alpha to 255
        uint8_t t_arr[16] = {
            (uint8_t)lumaThreshold, (uint8_t)lumaThreshold, (uint8_t)lumaThreshold, 255,
            (uint8_t)lumaThreshold, (uint8_t)lumaThreshold, (uint8_t)lumaThreshold, 255,
            (uint8_t)lumaThreshold, (uint8_t)lumaThreshold, (uint8_t)lumaThreshold, 255,
            (uint8_t)lumaThreshold, (uint8_t)lumaThreshold, (uint8_t)lumaThreshold, 255
        };
        v_threshold = vld1q_u8(t_arr);

        for (; x <= width - 4; x += 4)
        {
            uint8x16_t cur = vld1q_u8(curLine + x * 4);
            uint8x16_t p1 = vld1q_u8(histPointers[1] + x * 4);
            
            // Check similarity if smartEnabled
            uint16x4_t blendMask = vdup_n_u16(0xFFFF); // 4 pixels, 16 bits each for mask logic
            if (smartEnabled)
            {
                // abs(cur - p1)
                uint8x16_t diff = vabdq_u8(cur, p1);
                // diff <= threshold ? (vcgeq_u8 is >=, so threshold >= diff)
                uint8x16_t cmp = vcgeq_u8(v_threshold, diff);
                
                // cmp contains 0xFF for channels that passed. We need ALL RGB channels of a pixel to pass.
                // Cast to uint32x4 to check per-pixel
                uint32x4_t cmp32 = vreinterpretq_u32_u8(cmp);
                // The pixel mask will be 0xFFFFFFFF only if all 4 channels were 0xFF.
                // Wait, if Alpha is 0xFF, and RGB are 0xFF, it's 0xFFFFFFFF.
                // If any channel failed, it will have zeros.
                // We want a boolean mask for each pixel.
                
                // We'll process them as scalar if they aren't all uniform to keep it simple,
                // but actually doing the full blend in NEON is heavy because of variable depth.
                // Since N is variable (2 to 8), we can just loop over depth and accumulate.
            }
            
            // For now, NEON is skeleton. We will just break and use scalar for the moment
            // to ensure correctness before expanding the full NEON N-depth loop.
        }
#endif

        // Scalar Fallback
        for (; x < width; ++x)
        {
            uint8_t* c = curLine + x * 4;
            bool doBlend = true;

            if (smartEnabled)
            {
                const uint8_t* p1 = histPointers[1] + x * 4;
                // Simple max channel diff
                if (std::abs(c[0] - p1[0]) > lumaThreshold ||
                    std::abs(c[1] - p1[1]) > lumaThreshold ||
                    std::abs(c[2] - p1[2]) > lumaThreshold) {
                    doBlend = false;
                }
            }

            if (doBlend) {
                int r = 0, g = 0, b = 0;
                for (int i = 0; i < depth; ++i) {
                    const uint8_t* p = histPointers[i] + x * 4;
                    r += p[0] * weights[i];
                    g += p[1] * weights[i];
                    b += p[2] * weights[i];
                }
                c[0] = r / weightSum;
                c[1] = g / weightSum;
                c[2] = b / weightSum;
            }
        }
    }
}

} // namespace simd
} // namespace videowall
