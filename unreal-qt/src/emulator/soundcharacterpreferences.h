#pragma once

#include <string>

class SoundManager;

/// The GUI user's saved sound-character preference (ay_voicing, ay_punch,
/// ay_room, beeper_punch) in the application QSettings, group "Sound", one
/// key per setting name. Values are the same text IDs the automation surfaces
/// use (SoundCharacterSettings), so one parser validates them everywhere.
///
/// Applied only to emulator instances the GUI creates itself: an instance
/// created through automation and then adopted keeps its own values (the
/// creator owns them). Design: docs/inprogress/2026-09-25-ay-tone-voicing/ay-tone-voicing-tdd.md §8.1
namespace SoundCharacterPreferences
{
/// Remember one setting (called when the user changes it in the GUI)
void Save(const std::string& name, const std::string& value);

/// Apply every saved setting to @p sound. Missing keys keep the instance's
/// value (config / built-in default); invalid or hidden values are skipped
/// with a warning
void ApplySaved(SoundManager& sound);
}  // namespace SoundCharacterPreferences
