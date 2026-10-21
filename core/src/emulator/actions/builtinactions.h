#pragma once

/// @file builtinactions.h
/// @brief The core's actions (control surfaces actions.md §1, the catalogue of controller manager debug-and-ttd.md
/// §3): run control through execution::RunControl, time travel through ttd::TTDControl, the machine's reset, NMI and
/// turbo. They were the Qt client's (unreal-qt/src/controllers/defaultactions.cpp) and are now present headless; the
/// Qt client keeps the GUI ones (the debugger panes, the screenshot to the clipboard, the analog scrub, the OSD).

#include <cstdint>
#include <string>
#include <vector>

class EmulatorContext;

namespace actions
{
class ActionRegistry;
class RegistrationHandle;
struct ActionResult;

/// Register the core actions; the registry keeps the handles
std::vector<RegistrationHandle> RegisterBuiltinActions(ActionRegistry& registry);

/// Move `frames` through the time-travel history (back < 0), stopping at its ends: success when it moved, else
/// "Conflict" with why not ("the start of the history"). A seek to frame N stands at N's end, which the position names
/// as the start of N + 1: the last move's frame is kept per instance with the place it left, so the next move counts
/// from N (else a step back would not move). ttd.jog_frames, and the Qt client's analog scrub through it
ActionResult MoveFrames(EmulatorContext* context, int64_t frames);
}  // namespace actions
