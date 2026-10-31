#pragma once

#include <QByteArray>
#include <QGuiApplication>
#include <QList>

/// A session without windows: the offscreen or minimal platform (`--headless`, agents' and tests' instances). It
/// touches nothing on the host a person uses: the sound is silent (the device still runs, it is the emulation's
/// clock), no host controllers are opened, no Stream Deck daemon is started.
inline bool IsWindowlessSession()
{
    const QString platform = QGuiApplication::platformName();
    return platform == QLatin1String("offscreen") || platform == QLatin1String("minimal");
}

/// What a windowless session may use all the same: UNREAL_WINDOWLESS_DEVICES lists them (`sound`, `controllers`,
/// `streamdeck`, comma separated; `all`). A session with windows uses everything
inline bool WindowlessSessionUses(const char* device)
{
    if (!IsWindowlessSession())
        return true;
    const QList<QByteArray> allowed = qgetenv("UNREAL_WINDOWLESS_DEVICES").split(',');
    for (const QByteArray& name : allowed)
        if (name.trimmed() == device || name.trimmed() == "all")
            return true;
    return false;
}
