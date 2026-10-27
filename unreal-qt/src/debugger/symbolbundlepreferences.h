#pragma once

#include <string>

#include <QStringList>

class LabelManager;

/// The GUI user's switched-off symbol bundles (the sets "bundle:<id>" the core makes for known ROMs, system variables
/// and BIOS pages) in the application QSettings, key "Symbols/DisabledBundles". The core keeps a switched-off bundle
/// off only while the instance lives; remembering it between sessions is the GUI's business, the core knows nothing
/// of it.
///
/// Applied, like SoundCharacterPreferences, only to emulator instances the GUI creates itself: when it adopts one and
/// after each of its resets (a ROM selected at runtime brings new bundles). An instance created through automation
/// keeps every bundle on. Recipe: .recipe/analysis/symbols-import-export.md "Bundles"
namespace SymbolBundlePreferences
{
/// Remember that the user switched the set @p setId on or off (the Sets tab); other than bundle sets are ignored
void Save(const std::string& setId, bool enabled);

/// The remembered switched-off bundle sets
QStringList Disabled();

/// Switch off every remembered bundle set @p labels has on now; returns how many were switched off
int ApplySaved(LabelManager* labels);
}  // namespace SymbolBundlePreferences
