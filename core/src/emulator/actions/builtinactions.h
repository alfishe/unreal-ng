#pragma once

/// @file builtinactions.h
/// @brief The core's actions (control surfaces actions.md §1, the catalogue of controller manager debug-and-ttd.md
/// §3): run control through execution::RunControl, time travel through ttd::TTDControl, the machine's reset, NMI and
/// turbo. They were the Qt client's (unreal-qt/src/controllers/defaultactions.cpp) and are now present headless; the
/// Qt client keeps the GUI ones (the debugger panes, the screenshot to the clipboard, the analog scrub, the OSD).

#include <cstdint>
#include <string>
#include <vector>

#include "emulator/state/statenode.h"

class EmulatorContext;

namespace actions
{
class ActionRegistry;
class RegistrationHandle;
struct ActionResult;
struct ActionOrigin;

/// Register the core actions; the registry keeps the handles
std::vector<RegistrationHandle> RegisterBuiltinActions(ActionRegistry& registry);

/// Move `frames` through the time-travel history (back < 0), stopping at its ends: success when it moved, else
/// "Conflict" with why not ("the start of the history"). A seek to frame N stands at N's end, which the position names
/// as the start of N + 1: the last move's frame is kept per instance with the place it left, so the next move counts
/// from N (else a step back would not move). ttd.jog_frames, and the Qt client's analog scrub through it
ActionResult MoveFrames(EmulatorContext* context, int64_t frames);

/// The acceleration curve of ttd.seek_by (control surfaces actions.md §2): a step moves `min` units at a rate up to
/// `rateLo` steps/s, `max` at `rateHi` and more, geometrically between. Units are frames or seconds
struct SeekCurve
{
    bool seconds = false;
    double min = 1;
    double max = 100;
    double rateLo = 4;
    double rateHi = 40;
};

/// The curve from an action's params {unit: frames | seconds, min, max, rate_lo, rate_hi}; absent ones take the
/// defaults (frames 1 -> 100, seconds 0.02 -> 2, rates 4 -> 40). False with `error` for a value out of range
bool SeekCurveFromParams(const StateNode& params, SeekCurve& curve, std::string& error);
/// Frames to move for `delta` steps at `rate` steps/s: per = min * (max / min) ^ t, t = clamp((rate - rateLo) /
/// (rateHi - rateLo), 0, 1); seconds become frames (at `fps`) before rounding, so a step is at least one frame
int64_t SeekFrames(const SeekCurve& curve, double delta, double rate, double fps);

/// Who set a context, as NC_SURFACE_CONTEXT and the surface protocol's `ctx.by` name it: "companion:<id>",
/// "host-device:<id>", "webapi:<client>", "gui", ...
std::string SetterName(const ActionOrigin& origin);
}  // namespace actions
