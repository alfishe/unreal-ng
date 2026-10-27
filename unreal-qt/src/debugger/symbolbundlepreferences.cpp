#include "symbolbundlepreferences.h"

#include <QDebug>
#include <QSettings>

#include "debugger/labels/symbolcontrol.h"

namespace
{
const char* const kKey = "Symbols/DisabledBundles";

bool IsBundle(const std::string& setId)
{
    return setId.rfind("bundle:", 0) == 0;
}
}  // namespace

void SymbolBundlePreferences::Save(const std::string& setId, bool enabled)
{
    if (!IsBundle(setId))
        return;
    QSettings settings(QSettings::IniFormat, QSettings::UserScope, "Unreal", "Unreal-NG");
    QStringList disabled = settings.value(kKey).toStringList();
    const QString id = QString::fromStdString(setId);
    disabled.removeAll(id);
    if (!enabled)
        disabled.append(id);
    if (disabled.isEmpty())
        settings.remove(kKey);
    else
        settings.setValue(kKey, disabled);
}

QStringList SymbolBundlePreferences::Disabled()
{
    QSettings settings(QSettings::IniFormat, QSettings::UserScope, "Unreal", "Unreal-NG");
    return settings.value(kKey).toStringList();
}

int SymbolBundlePreferences::ApplySaved(LabelManager* labels)
{
    const QStringList disabled = Disabled();
    if (!labels || disabled.isEmpty())
        return 0;
    SymbolControl control(labels);
    const SymbolReply reply = control.Execute({"sets", {}});
    if (!reply.Ok())
        return 0;
    int count = 0;
    for (const StateNode& set : reply.body.find("sets")->items)
    {
        const std::string id = set.find("id")->s;
        if (!IsBundle(id) || !set.find("enabled")->b || !disabled.contains(QString::fromStdString(id)))
            continue;
        const SymbolReply off = control.Execute({"set", {{"id", id}, {"enabled", "false"}}});
        if (off.Ok())
            count++;
        else
            qWarning() << "SymbolBundlePreferences: cannot switch off" << QString::fromStdString(id) << "-"
                       << QString::fromStdString(off.message);
    }
    return count;
}
