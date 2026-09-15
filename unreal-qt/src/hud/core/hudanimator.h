#pragma once

#include "hudsnapshot.h"
#include "hudtiming.h"

#include <cmath>

/// @brief Pure mathematical easing functions mapping normalized time t in [0, 1] to progress [0, 1]
namespace HudEasing
{
    inline double Clamp01(double t)
    {
        return t < 0.0 ? 0.0 : (t > 1.0 ? 1.0 : t);
    }

    inline double Linear(double t)
    {
        return Clamp01(t);
    }

    inline double EaseInQuad(double t)
    {
        t = Clamp01(t);
        return t * t;
    }

    inline double EaseOutQuad(double t)
    {
        t = Clamp01(t);
        return t * (2.0 - t);
    }

    inline double EaseInOutQuad(double t)
    {
        t = Clamp01(t);
        return t < 0.5 ? 2.0 * t * t : -1.0 + (4.0 - 2.0 * t) * t;
    }

    inline double EaseInCubic(double t)
    {
        t = Clamp01(t);
        return t * t * t;
    }

    inline double EaseOutCubic(double t)
    {
        t = Clamp01(t);
        double inv = 1.0 - t;
        return 1.0 - inv * inv * inv;
    }

    inline double EaseInOutCubic(double t)
    {
        t = Clamp01(t);
        return t < 0.5 ? 4.0 * t * t * t : 1.0 - std::pow(-2.0 * t + 2.0, 3.0) / 2.0;
    }

    inline double EaseInSine(double t)
    {
        t = Clamp01(t);
        constexpr double kPi = 3.14159265358979323846;
        return 1.0 - std::cos((t * kPi) / 2.0);
    }

    inline double EaseOutSine(double t)
    {
        t = Clamp01(t);
        constexpr double kPi = 3.14159265358979323846;
        return std::sin((t * kPi) / 2.0);
    }

    inline double EaseInOutSine(double t)
    {
        t = Clamp01(t);
        constexpr double kPi = 3.14159265358979323846;
        return -(std::cos(kPi * t) - 1.0) / 2.0;
    }

    inline double PulseSine(double t)
    {
        constexpr double kPi = 3.14159265358979323846;
        return (std::sin(t * 2.0 * kPi - kPi / 2.0) + 1.0) / 2.0;
    }

    inline double EaseOutBack(double t, double s = 1.70158)
    {
        t = Clamp01(t);
        double inv = t - 1.0;
        return inv * inv * ((s + 1.0) * inv + s) + 1.0;
    }
} // namespace HudEasing

/// @brief Evaluated animation properties for an element at a given time point
struct HudAnimationState
{
    float opacity = 1.0f;
    float scale = 1.0f;
    float slideOffsetEm = 0.0f;
    bool finished = false;
};

/// @brief Stateless evaluator for HUD animations based on wall-clock time
class HudAnimator
{
public:
    static HudAnimationState EvaluateEnter(
        HudClock::duration elapsed,
        HudAnimation anim = HudAnimation::SlideFade,
        bool reducedMotion = false,
        HudClock::duration duration = HudTiming::AnimEnter);

    static HudAnimationState EvaluateExit(
        HudClock::duration elapsed,
        HudAnimation anim = HudAnimation::Fade,
        bool reducedMotion = false,
        HudClock::duration duration = HudTiming::AnimExit);

    static float EvaluatePulse(
        HudClock::duration elapsed,
        HudClock::duration period = HudTiming::AnimPulse);
};
