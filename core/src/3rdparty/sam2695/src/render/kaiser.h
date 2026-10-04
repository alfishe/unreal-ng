// libsam2695 - Kaiser window, shared by the interpolation and resampling kernels.
#pragma once

#include <cmath>

namespace sam2695
{

// Zeroth-order modified Bessel function of the first kind (series; converges fast for beta < 20)
inline double BesselI0(double x)
{
    double sum = 1.0, term = 1.0;
    const double q = x * x / 4.0;
    for (int k = 1; k < 64; k++)
    {
        term *= q / (static_cast<double>(k) * k);
        sum += term;
        if (term < sum * 1e-17)
            break;
    }
    return sum;
}

// w(x) for x in [-1, 1], 0 outside
inline double KaiserWindow(double x, double beta)
{
    if (x <= -1.0 || x >= 1.0)
        return 0.0;
    return BesselI0(beta * std::sqrt(1.0 - x * x)) / BesselI0(beta);
}

} // namespace sam2695
