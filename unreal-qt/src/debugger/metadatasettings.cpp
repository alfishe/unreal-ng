#include "metadatasettings.h"

#include <QSettings>

#include "emulator/readoutschanged.h"

namespace
{
struct Entry
{
    const char* key;     ///< in the settings
    const char* action;  ///< the catalogue's
};

constexpr Entry kEntries[MetadataSettings::kCount] = {
    {"Debugger/Metadata", "dbg.metadata_toggle"},
    {"Debugger/MetadataRegions", "dbg.metadata_regions_toggle"},
    {"Debugger/MetadataColours", "dbg.metadata_colours_toggle"},
    {"Debugger/MetadataNotes", "dbg.metadata_notes_toggle"},
    {"Debugger/Labels", "dbg.labels_toggle"},
};
}  // namespace

MetadataSettings& MetadataSettings::Instance()
{
    static MetadataSettings settings;
    return settings;
}

MetadataSettings::MetadataSettings()
{
    QSettings settings(QSettings::IniFormat, QSettings::UserScope, "Unreal", "Unreal-NG");
    for (size_t i = 0; i < kCount; i++)
        _on[i].store(settings.value(QLatin1String(kEntries[i].key), true).toBool());
}

bool MetadataSettings::IsOn(Switch which) const
{
    return _on[static_cast<size_t>(which)].load();
}

bool MetadataSettings::Uses(Switch which) const
{
    if (which == Switch::Labels || which == Switch::Metadata)
        return IsOn(which);
    return IsOn(Switch::Metadata) && IsOn(which);
}

void MetadataSettings::Set(Switch which, bool on)
{
    const size_t index = static_cast<size_t>(which);
    if (_on[index].exchange(on) == on)
        return;
    QSettings settings(QSettings::IniFormat, QSettings::UserScope, "Unreal", "Unreal-NG");
    settings.setValue(QLatin1String(kEntries[index].key), on);
    NotifyReadoutsChanged(std::string(), "debugger.metadata");  // the toggles' states on the control surfaces
    emit changed();
}

const char* MetadataSettings::ActionId(Switch which)
{
    return kEntries[static_cast<size_t>(which)].action;
}
