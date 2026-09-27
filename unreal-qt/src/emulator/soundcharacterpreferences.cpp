#include "soundcharacterpreferences.h"

#include <QDebug>
#include <QSettings>

#include "emulator/sound/soundcharactersettings.h"
#include "emulator/sound/soundmanager.h"

namespace
{
QString KeyFor(const std::string& name)
{
    return QString("Sound/") + QString::fromStdString(name);
}
}  // namespace

void SoundCharacterPreferences::Save(const std::string& name, const std::string& value)
{
    QSettings settings(QSettings::IniFormat, QSettings::UserScope, "Unreal", "Unreal-NG");
    settings.setValue(KeyFor(name), QString::fromStdString(value));
}

void SoundCharacterPreferences::ApplySaved(SoundManager& sound)
{
    QSettings settings(QSettings::IniFormat, QSettings::UserScope, "Unreal", "Unreal-NG");
    for (const SoundCharacterSettings::Descriptor& d : SoundCharacterSettings::Descriptors())
    {
        const QString key = KeyFor(d.name);
        if (!settings.contains(key))
            continue;

        std::string error;
        const std::string value = settings.value(key).toString().toStdString();
        if (!SoundCharacterSettings::Set(sound, d.name, value, error))
            qWarning() << "SoundCharacterPreferences: ignoring saved" << key << "-" << QString::fromStdString(error);
    }
}
