#include "multisoundanalog.h"

#include <algorithm>
#include <cmath>
#include <complex>

namespace
{
constexpr double kPi = 3.14159265358979323846;

double AnalogMagnitude(const double (&b)[3], const double (&a)[3], double hz)
{
    const std::complex<double> s(0.0, 2.0 * kPi * hz);
    const std::complex<double> num = b[0] + s * (b[1] + s * b[2]);
    const std::complex<double> den = a[0] + s * (a[1] + s * a[2]);
    return std::abs(num / den);
}
} // namespace

namespace MultiSoundBoard
{
double DacCornerHz()
{
    const double thevenin = kDacSeriesOhms * kDacInputOhms / (kDacSeriesOhms + kDacInputOhms);
    return 1.0 / (2.0 * kPi * thevenin * kDacShuntFarads);
}

void SaaLadder(double (&b)[3], double (&a)[3])
{
    // Node equations with G1 = 1 / R46, G2 = 1 / R48, GL = 1 / R4 (the summing node is a virtual ground):
    //   (Vth - V1) G1 = s C1 V1 + (V1 - V2) G2
    //   (V1 - V2) G2 = s C2 V2 + V2 GL
    // give V2 / Vth = G1 / ((1 + R2 GL + s R2 C2)(G1 + G2 + s C1) - G2)
    const double g1 = 1.0 / kSaaSourceOhms;
    const double g2 = 1.0 / kSaaSeriesOhms;
    const double gl = 1.0 / kSaaInputOhms;
    const double r2 = kSaaSeriesOhms;
    const double c1 = kSaaFirstShuntFarads;
    const double c2 = kSaaSecondShuntFarads;

    a[0] = (1.0 + r2 * gl) * (g1 + g2) - g2;
    a[1] = (1.0 + r2 * gl) * c1 + r2 * c2 * (g1 + g2);
    a[2] = r2 * c2 * c1;
    // Unit DC gain: the DC level is kWeightSaa's business
    b[0] = a[0];
    b[1] = 0.0;
    b[2] = 0.0;
}

double SaaCornerHz()
{
    double b[3];
    double a[3];
    SaaLadder(b, a);
    const double target = 1.0 / std::sqrt(2.0);
    double low = 10.0;
    double high = 100000.0;
    for (int i = 0; i < 60; i++)
    {
        const double mid = 0.5 * (low + high);
        if (AnalogMagnitude(b, a, mid) > target)
            low = mid;
        else
            high = mid;
    }
    return 0.5 * (low + high);
}

double CouplingCornerHz(double inputOhms)
{
    return 1.0 / (2.0 * kPi * inputOhms * kCouplingFarads);
}
} // namespace MultiSoundBoard

void MultiSoundRcFilter::Design(const double (&b)[3], const double (&a)[3], double prewarpHz, double sampleRate)
{
    // s = k (1 - z^-1) / (1 + z^-1), k = w0 / tan(w0 T / 2): exact at w0
    const double fs = std::max(sampleRate, 1.0);
    const double f0 = std::clamp(prewarpHz, 1e-6, 0.45 * fs);
    const double w0 = 2.0 * kPi * f0;
    const double k = w0 / std::tan(w0 / (2.0 * fs));
    const double k2 = k * k;

    const double n0 = b[2] * k2 + b[1] * k + b[0];
    const double n1 = 2.0 * (b[0] - b[2] * k2);
    const double n2 = b[2] * k2 - b[1] * k + b[0];
    const double d0 = a[2] * k2 + a[1] * k + a[0];
    const double d1 = 2.0 * (a[0] - a[2] * k2);
    const double d2 = a[2] * k2 - a[1] * k + a[0];

    _b0 = n0 / d0;
    _b1 = n1 / d0;
    _b2 = n2 / d0;
    _a1 = d1 / d0;
    _a2 = d2 / d0;
    Reset();
}

MultiSoundRcFilter MultiSoundRcFilter::LowPass1(double cornerHz, double sampleRate)
{
    const double tau = 1.0 / (2.0 * kPi * cornerHz);
    const double b[3] = { 1.0, 0.0, 0.0 };
    const double a[3] = { 1.0, tau, 0.0 };
    MultiSoundRcFilter filter;
    filter.Design(b, a, cornerHz, sampleRate);
    return filter;
}

MultiSoundRcFilter MultiSoundRcFilter::HighPass1(double cornerHz, double sampleRate)
{
    const double tau = 1.0 / (2.0 * kPi * cornerHz);
    const double b[3] = { 0.0, tau, 0.0 };
    const double a[3] = { 1.0, tau, 0.0 };
    MultiSoundRcFilter filter;
    filter.Design(b, a, cornerHz, sampleRate);
    return filter;
}

double MultiSoundRcFilter::Magnitude(double hz, double sampleRate) const
{
    const double w = 2.0 * kPi * hz / sampleRate;
    const std::complex<double> z1 = std::polar(1.0, -w);
    const std::complex<double> z2 = z1 * z1;
    const std::complex<double> num = _b0 + _b1 * z1 + _b2 * z2;
    const std::complex<double> den = 1.0 + _a1 * z1 + _a2 * z2;
    return std::abs(num / den);
}
