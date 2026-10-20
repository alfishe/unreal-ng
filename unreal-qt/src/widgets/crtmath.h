#pragma once

// The elementary functions the CRT tables are built with, the same bits on every compiler and C library. The libm
// functions are not: Apple's, glibc's and the Windows CRT's pow, exp2, sin, cos and acos differ in the last bit, and
// a table entry rounded from such a value can differ by one. These use only +, -, *, / and exact scaling by powers of
// two, which IEEE-754 rounds the same everywhere; the files building the tables compile without floating-point
// contraction (unreal-qt/CMakeLists.txt), so no compiler fuses a multiply and an add. Double precision inside: the
// float results are the correctly rounded ones except within about 1e-15 of a rounding boundary.

#include <cmath>

namespace CRTMath
{
constexpr double kLn2 = 0.69314718055994530942;
constexpr double kLog2E = 1.44269504088896340736;
constexpr double kPi = 3.14159265358979323846;

/// 2^x
inline double Exp2(double x)
{
    if (x < -1000.0)
        return 0.0;
    if (x > 1000.0)
        return HUGE_VAL;
    // x = k + f, |f| <= 0.5; e^(f ln 2) by its Taylor series (the 15th term is under 2^-60)
    const double k = std::floor(x + 0.5);
    const double t = (x - k) * kLn2;
    double sum = 1.0;
    for (int n = 15; n >= 1; --n)
        sum = 1.0 + sum * t / n;
    return std::ldexp(sum, static_cast<int>(k));
}

/// e^x
inline double Exp(double x)
{
    return Exp2(x * kLog2E);
}

/// log2(x), x > 0
inline double Log2(double x)
{
    // x = m 2^e, m in [sqrt(1/2), sqrt(2)); ln m = 2 atanh(s), s = (m - 1) / (m + 1), |s| < 0.172
    int e = 0;
    double m = std::frexp(x, &e);
    if (m < 0.70710678118654752440)
    {
        m *= 2.0;
        --e;
    }
    const double s = (m - 1.0) / (m + 1.0);
    const double s2 = s * s;
    double sum = 0.0;
    for (int n = 12; n >= 0; --n)
        sum = 1.0 / (2 * n + 1) + sum * s2;
    return static_cast<double>(e) + 2.0 * s * sum * kLog2E;
}

/// x^y for x >= 0 (0 for x = 0, as pow for y > 0)
inline float Pow(float x, float y)
{
    if (x <= 0.0f)
        return y == 0.0f ? 1.0f : 0.0f;
    return static_cast<float>(Exp2(static_cast<double>(y) * Log2(x)));
}

namespace Detail
{
/// x = k pi/2 + r, |r| <= pi/4 (pi/2 in three parts, the first two exact in a product with k < 2^20)
inline double Reduce(double x, int& quadrant)
{
    constexpr double kTwoOverPi = 0.63661977236758134308;
    constexpr double kPiOver2A = 1.57079632673412561417e+00;
    constexpr double kPiOver2B = 6.07710050630396597660e-11;
    constexpr double kPiOver2C = 2.02226624879595063154e-21;
    const double k = std::floor(x * kTwoOverPi + 0.5);
    quadrant = static_cast<int>(k - 4.0 * std::floor(k * 0.25));
    return ((x - k * kPiOver2A) - k * kPiOver2B) - k * kPiOver2C;
}

/// sin r and cos r for |r| <= pi/4 by their Taylor series (the last terms are under 2^-60)
inline double SinSeries(double r)
{
    const double r2 = r * r;
    double sum = 1.0;
    for (int n = 19; n >= 3; n -= 2)
        sum = 1.0 - sum * r2 / (n * (n - 1));
    return r * sum;
}

inline double CosSeries(double r)
{
    const double r2 = r * r;
    double sum = 1.0;
    for (int n = 20; n >= 2; n -= 2)
        sum = 1.0 - sum * r2 / (n * (n - 1));
    return sum;
}

/// atan t for t >= 0
inline double Atan(double t)
{
    constexpr double kSqrt3 = 1.73205080756887729353;
    constexpr double kTanPiOver12 = 0.26794919243112270647;
    if (t > 1.0)
        return kPi / 2.0 - Atan(1.0 / t);
    double base = 0.0;
    if (t > kTanPiOver12)
    {
        // atan t = pi/6 + atan((t sqrt 3 - 1) / (t + sqrt 3)), the argument within tan(pi/12)
        base = kPi / 6.0;
        t = (t * kSqrt3 - 1.0) / (t + kSqrt3);
    }
    const double t2 = t * t;
    double sum = 0.0;
    for (int n = 15; n >= 0; --n)
        sum = (n % 2 == 0 ? 1.0 : -1.0) / (2 * n + 1) + sum * t2;
    return base + t * sum;
}
}  // namespace Detail

inline float Sin(float x)
{
    int q = 0;
    const double r = Detail::Reduce(x, q);
    const double v = (q & 1) ? Detail::CosSeries(r) : Detail::SinSeries(r);
    return static_cast<float>(q & 2 ? -v : v);
}

inline float Cos(float x)
{
    int q = 0;
    const double r = Detail::Reduce(x, q);
    const double v = (q & 1) ? Detail::SinSeries(r) : Detail::CosSeries(r);
    return static_cast<float>((q + 1) & 2 ? -v : v);
}

/// acos x for x in [-1, 1]: 2 atan(sqrt((1 - x) / (1 + x)))
inline float Acos(float x)
{
    if (x <= -1.0f)
        return static_cast<float>(kPi);
    if (x >= 1.0f)
        return 0.0f;
    const double d = static_cast<double>(x);
    return static_cast<float>(2.0 * Detail::Atan(std::sqrt((1.0 - d) / (1.0 + d))));
}

inline float Exp2(float x)
{
    return static_cast<float>(Exp2(static_cast<double>(x)));
}

inline float Exp(float x)
{
    return static_cast<float>(Exp(static_cast<double>(x)));
}
}  // namespace CRTMath
