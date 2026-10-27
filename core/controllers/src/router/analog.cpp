#include "router/analog.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <iterator>

#include "emulator/io/controllers/controllertypes.h"

namespace controllers
{

namespace
{
constexpr float kPi = 3.14159265358979f;

/// The direction bits of the 45-degree sector @p sector (0 = right, counter-clockwise, screen up = +90 degrees)
uint16_t SectorBits(int sector)
{
    constexpr uint16_t kSectors[8] = {kRight, kRight | kUp,  kUp,   kUp | kLeft,
                                      kLeft,  kLeft | kDown, kDown, kDown | kRight};
    return kSectors[((sector % 8) + 8) % 8];
}

/// The angle of an axis direction bit, in degrees (right 0, up 90, left 180, down 270); -1 for none or a diagonal
float AxisAngle(uint16_t bits)
{
    switch (bits)
    {
        case kRight:
            return 0.0f;
        case kUp:
            return 90.0f;
        case kLeft:
            return 180.0f;
        case kDown:
            return 270.0f;
        default:
            return -1.0f;
    }
}

float AngleBetween(float a, float b)
{
    return std::fabs(std::fmod(a - b + 540.0f, 360.0f) - 180.0f);
}
}  // namespace

float RadialDeflection(float x, float y, const AnalogOptions& options)
{
    const float r = std::sqrt(x * x + y * y);
    if (r <= options.deadzone)
        return 0.0f;
    if (r >= options.saturation || options.saturation <= options.deadzone)
        return 1.0f;
    return (r - options.deadzone) / (options.saturation - options.deadzone);
}

uint16_t StickDirections(float x, float y, uint16_t previous, const AnalogOptions& options)
{
    const float deflection = RadialDeflection(x, y, options);
    const bool held = previous != 0 ? deflection >= options.off : deflection >= options.on;
    if (!held)
        return 0;

    // Screen up is y = -1: the angle is measured with up at +90 degrees
    float angle = std::atan2(-y, x) * 180.0f / kPi;
    if (angle < 0)
        angle += 360.0f;

    if (options.ways == 4)
    {
        const float last = AxisAngle(previous);
        if (last >= 0 && AngleBetween(angle, last) < options.diagonalHold)
            return previous;
        return SectorBits(2 * static_cast<int>(std::lround(angle / 90.0f)));
    }
    return SectorBits(static_cast<int>(std::lround(angle / 45.0f)));
}

bool TriggerClick(float value, bool previous, const AnalogOptions& options)
{
    return previous ? value >= options.triggerRelease : value >= options.triggerClick;
}

float MouseCurve(float value, const AnalogOptions& options)
{
    const float magnitude = std::fabs(value);
    if (magnitude <= options.deadzone || options.deadzone >= 1.0f)
        return 0.0f;
    const float rescaled = std::min(1.0f, (magnitude - options.deadzone) / (1.0f - options.deadzone));
    const float curved = std::pow(rescaled, options.mouseGamma);
    return value < 0 ? -curved : curved;
}

float ScrubSpeed(float travel)
{
    struct Point
    {
        float travel;
        float speed;
    };
    static constexpr Point kPoints[] = {{0.08f, 1.0f}, {0.30f, 5.0f}, {0.60f, 50.0f}, {0.90f, 500.0f}, {1.0f, 5000.0f}};
    if (travel < kPoints[0].travel)
        return 0.0f;
    for (size_t i = 1; i < std::size(kPoints); i++)
    {
        const Point& from = kPoints[i - 1];
        const Point& to = kPoints[i];
        if (travel <= to.travel)
            return from.speed * std::pow(to.speed / from.speed, (travel - from.travel) / (to.travel - from.travel));
    }
    return kPoints[std::size(kPoints) - 1].speed;
}

}  // namespace controllers
