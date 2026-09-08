#include "hudanimator.h"

#include <algorithm>

HudAnimationState HudAnimator::EvaluateEnter(
    HudClock::duration elapsed,
    HudAnimation anim,
    bool reducedMotion,
    HudClock::duration duration)
{
    if (anim == HudAnimation::None)
    {
        return {1.0f, 1.0f, 0.0f, true};
    }

    if (reducedMotion)
    {
        duration = HudTiming::AnimReducedMotion;
        if (duration <= HudClock::duration::zero() || elapsed >= duration)
        {
            return {1.0f, 1.0f, 0.0f, true};
        }
        double t = static_cast<double>(elapsed.count()) / static_cast<double>(duration.count());
        return {static_cast<float>(HudEasing::Linear(t)), 1.0f, 0.0f, false};
    }

    if (duration <= HudClock::duration::zero() || elapsed >= duration)
    {
        return {1.0f, 1.0f, 0.0f, true};
    }

    double t = static_cast<double>(elapsed.count()) / static_cast<double>(duration.count());
    double p = HudEasing::EaseOutCubic(t);

    HudAnimationState state;
    state.finished = false;

    switch (anim)
    {
    case HudAnimation::SlideFade:
        state.opacity = static_cast<float>(p);
        state.scale = static_cast<float>(0.96 + 0.04 * p);
        state.slideOffsetEm = static_cast<float>(0.75 * (1.0 - p));
        break;
    case HudAnimation::ScaleFade:
        state.opacity = static_cast<float>(p);
        state.scale = static_cast<float>(0.90 + 0.10 * p);
        state.slideOffsetEm = 0.0f;
        break;
    case HudAnimation::Fade:
    default:
        state.opacity = static_cast<float>(p);
        state.scale = 1.0f;
        state.slideOffsetEm = 0.0f;
        break;
    }

    return state;
}

HudAnimationState HudAnimator::EvaluateExit(
    HudClock::duration elapsed,
    HudAnimation anim,
    bool reducedMotion,
    HudClock::duration duration)
{
    if (anim == HudAnimation::None)
    {
        return {0.0f, 1.0f, 0.0f, true};
    }

    if (reducedMotion)
    {
        duration = HudTiming::AnimReducedMotion;
        if (duration <= HudClock::duration::zero() || elapsed >= duration)
        {
            return {0.0f, 1.0f, 0.0f, true};
        }
        double t = static_cast<double>(elapsed.count()) / static_cast<double>(duration.count());
        return {static_cast<float>(1.0 - HudEasing::Linear(t)), 1.0f, 0.0f, false};
    }

    if (duration <= HudClock::duration::zero() || elapsed >= duration)
    {
        return {0.0f, 1.0f, 0.0f, true};
    }

    double t = static_cast<double>(elapsed.count()) / static_cast<double>(duration.count());
    double p = HudEasing::EaseInCubic(t);

    HudAnimationState state;
    state.finished = false;

    switch (anim)
    {
    case HudAnimation::SlideFade:
        state.opacity = static_cast<float>(1.0 - p);
        state.scale = 1.0f;
        state.slideOffsetEm = static_cast<float>(0.50 * p);
        break;
    case HudAnimation::ScaleFade:
        state.opacity = static_cast<float>(1.0 - p);
        state.scale = static_cast<float>(1.0 - 0.08 * p);
        state.slideOffsetEm = 0.0f;
        break;
    case HudAnimation::Fade:
    default:
        state.opacity = static_cast<float>(1.0 - p);
        state.scale = 1.0f;
        state.slideOffsetEm = 0.0f;
        break;
    }

    return state;
}

float HudAnimator::EvaluatePulse(HudClock::duration elapsed, HudClock::duration period)
{
    if (period <= HudClock::duration::zero())
    {
        return 1.0f;
    }

    auto count = elapsed.count();
    auto periodCount = period.count();
    double norm = static_cast<double>(count % periodCount) / static_cast<double>(periodCount);
    if (norm < 0.0) norm += 1.0;

    return static_cast<float>(HudEasing::PulseSine(norm));
}
