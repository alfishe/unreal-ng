// libsam2695 - the windowed-sinc interpolation table.
#include "voice/interpolator.h"

#include "render/kaiser.h"

#include <array>
#include <cmath>

namespace sam2695
{

namespace
{

struct SincKernel
{
    std::array<float, (kSincPhases + 1) * kSincTaps> table{};

    SincKernel()
    {
        // Full-band sinc under a Kaiser window (beta 6), every phase normalized to unity DC gain. Against
        // an ideal fractional delay its worst error is 6.5e-4 up to 0.05 fs, 1.1e-3 up to 0.2 fs and
        // 0.11 at 0.35 fs (Catmull-Rom: 5.1e-4, 5.1e-2, 0.37); a narrower cutoff only adds droop.
        constexpr double kCutoff = 1.0;
        constexpr double kBeta = 6.0;
        const double half = kSincTaps / 2.0; // taps -3-f .. 4-f lie inside the window [-4, 4]
        for (int p = 0; p <= kSincPhases; p++)
        {
            const double frac = static_cast<double>(p) / kSincPhases;
            double sum = 0.0;
            double row[kSincTaps];
            for (int k = 0; k < kSincTaps; k++)
            {
                const double x = static_cast<double>(k - 3) - frac; // tap position relative to the output
                const double sinc = std::fabs(x) < 1e-12 ? 1.0 : std::sin(3.14159265358979323846 * kCutoff * x) /
                                                                     (3.14159265358979323846 * kCutoff * x);
                row[k] = kCutoff * sinc * KaiserWindow(x / half, kBeta);
                sum += row[k];
            }
            for (int k = 0; k < kSincTaps; k++)
                table[p * kSincTaps + k] = static_cast<float>(row[k] / sum); // unity DC gain per phase
        }
    }
};

} // namespace

const float* SincTable()
{
    static const SincKernel kKernel;
    return kKernel.table.data();
}

} // namespace sam2695
