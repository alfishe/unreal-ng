#pragma once

/// @file hostactions.h
/// @brief The registry actions a Stream Deck scenario (or any surface) runs instead of REST steps (Stream Deck
/// integration SD6, docs/inprogress/2026-10-19-streamdeck-integration/migration.md §4; catalogue:
/// docs/inprogress/2026-10-18-control-surfaces/actions.md §1.1): run extras, the machine's lifecycle, input, BASIC,
/// features and settings, audio, media, snapshots, video, debugger and time travel. Each calls the core object the
/// route calls (RunControl, MediaControl, TTDControl, SnapshotLauncher, ModelSwitch, the keyboard / joystick managers,
/// FeatureManager, AudioMixer, Screenshotter), never HTTP, and takes the route's companion scope
/// (automation_gate::CompanionScopeOf of the route), so a surface's action and the REST step are gated alike.

#include <optional>
#include <string>
#include <vector>

class Emulator;

namespace actions
{

class ActionRegistry;
class RegistrationHandle;

/// Registers them on `registry`; they stay while the handles live (ActionRegistry::Instance keeps them for good)
std::vector<RegistrationHandle> RegisterHostActions(ActionRegistry& registry);

/// The machine area's actions (hostactions_machine.cpp): mach.switch, mach.previous, the media verbs (insert, swap,
/// export, discard, rescan), media.open_recent, rzx.*, input.joystick_type, the mouse buttons, cap.record_toggle,
/// cap.record_pause, cap.clip_last, cap.audio_capture, ttd.export_clip. RegisterHostActions registers them
std::vector<RegistrationHandle> RegisterMachineAreaActions(ActionRegistry& registry);

struct ActionCall;
struct ActionResult;
/// mach.start's executor (mach.previous starts the previous model through it)
ActionResult HostStartMachine(const ActionCall& call);
/// media.open's executor (media.open_recent opens through it)
ActionResult HostOpenMedia(const ActionCall& call);

/// The current value of a bool setting `set.toggle` flips (turbo_mode, fast_tape, turbo_tape, fast_disk,
/// turbo_audio, trdos_traps, the bool sound-character settings); nullopt for a name it does not know. Reads only
/// (the readout `toggle.set.<name>`)
std::optional<bool> HostSettingIsOn(Emulator& emulator, const std::string& name);

}  // namespace actions
