#include "metadatasettings.h"

#include <QSettings>

namespace
{
struct Entry
{
    const char* key;  ///< in the settings
};

constexpr Entry kEntries[MetadataSettings::kCount] = {
    {"Debugger/Metadata"},
    {"Debugger/MetadataRegions"},
    {"Debugger/MetadataColours"},
    {"Debugger/MetadataNotes"},
    {"Debugger/Labels"},
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
    emit changed();
}
