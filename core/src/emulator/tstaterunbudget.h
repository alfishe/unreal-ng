#pragma once

#include <cstdint>

/// The T-state accounting of a bounded direct run (Emulator::RunNFrames,
/// RunUntilInterrupt, RunUntilCondition): how far the run has come and how far
/// it may go, in CPU T-states of the frame length now in force.
///
/// Both counters are 64-bit. A budget is a frame count times the CPU frame
/// length, and the CPU frame grows with the clock multiplier: the Sprinter at
/// 21 MHz has 430 080 T-states per frame, so 32 bits overflowed above 9 986
/// frames and RunNFrames(10000) ran 14 frames (the WebAPI and CLI accept up
/// to 10 000).
///
/// A hardware clock switch (a turbo machine's) changes the frame length, mid
/// frame too, and rescales the CPU's t with it; the elapsed count and the
/// target are rescaled the same way, so a budget of "N frames" stays N frames
/// of emulated time.
class TStateRunBudget
{
public:
    explicit TStateRunBudget(uint64_t targetTStates) : _target(targetTStates) {}

    /// The budget of `frames` whole frames of `frameLimit` CPU T-states each
    static TStateRunBudget Frames(uint32_t frameLimit, uint64_t frames)
    {
        return TStateRunBudget(static_cast<uint64_t>(frameLimit) * frames);
    }

    /// Account one executed step.
    /// @param prevT          the CPU's t before the step
    /// @param t              the CPU's t after it (rebased by limitBefore when the step closed a frame)
    /// @param limitBefore    the frame limit before the step
    /// @param limitAfter     the frame limit after it (differs on a clock switch)
    /// @param frameCompleted the step closed a frame
    void Step(uint32_t prevT, uint32_t t, uint32_t limitBefore, uint32_t limitAfter, bool frameCompleted)
    {
        if (limitAfter == limitBefore)
        {
            _elapsed += frameCompleted ? (static_cast<uint64_t>(t) + limitBefore - prevT)
                                       : static_cast<uint64_t>(t - prevT);
        }
        else if (limitBefore != 0 && limitAfter != 0)
        {
            // The frame length changed during the step and t was rescaled with it: only the position inside the
            // frame, as a fraction of it, means the same before and after
            const double fractionBefore = static_cast<double>(prevT) / limitBefore;
            const double fractionAfter = static_cast<double>(t) / limitAfter;
            const double deltaFrames = (frameCompleted ? 1.0 : 0.0) + fractionAfter - fractionBefore;
            const double scale = static_cast<double>(limitAfter) / limitBefore;
            const double elapsedAfter = static_cast<double>(_elapsed) * scale + deltaFrames * limitAfter;
            _elapsed = elapsedAfter > 0 ? static_cast<uint64_t>(elapsedAfter) : 0;
            _target = static_cast<uint64_t>(static_cast<double>(_target) * scale);
        }
    }

    bool Reached() const { return _elapsed >= _target; }
    uint64_t Elapsed() const { return _elapsed; }
    uint64_t Target() const { return _target; }

private:
    uint64_t _target = 0;
    uint64_t _elapsed = 0;
};
