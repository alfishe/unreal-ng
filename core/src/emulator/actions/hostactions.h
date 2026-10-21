#pragma once

/// @file hostactions.h
/// @brief The registry actions a Stream Deck scenario (or any surface) runs instead of REST steps (Stream Deck
/// integration SD6, docs/inprogress/2026-10-19-streamdeck-integration/migration.md §4; catalogue:
/// docs/inprogress/2026-10-18-control-surfaces/actions.md §1.1): run extras, the machine's lifecycle, input, BASIC,
/// features and settings, audio, media, snapshots, video, debugger and time travel. Each calls the core object the
/// route calls (RunControl, MediaControl, TTDControl, SnapshotLauncher, ModelSwitch, the keyboard / joystick managers,
/// FeatureManager, AudioMixer, Screenshotter), never HTTP, and takes the route's companion scope
/// (automation_gate::CompanionScopeOf of the route), so a surface's action and the REST step are gated alike.

#include <vector>

namespace actions
{

class ActionRegistry;
class RegistrationHandle;

/// Registers them on `registry`; they stay while the handles live (ActionRegistry::Instance keeps them for good)
std::vector<RegistrationHandle> RegisterHostActions(ActionRegistry& registry);

}  // namespace actions
