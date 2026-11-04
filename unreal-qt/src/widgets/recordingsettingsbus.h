#pragma once

/// @file recordingsettingsbus.h
/// @brief Tells every recording UI (the recording panel, the full Video Recording dialog) that the shared
/// RecordingSettings changed, so they all show the same options at once, whichever one the user changed them in.

#include <QObject>
#include <QPointer>

#include <functional>
#include <utility>
#include <vector>

namespace recordingsettingsbus
{
struct Listener
{
    QPointer<QObject> owner;
    std::function<void()> callback;
};

inline std::vector<Listener>& Listeners()
{
    static std::vector<Listener> listeners;
    return listeners;
}

/// @p callback runs on the GUI thread after any UI saved the shared settings; it ends with @p owner
inline void Subscribe(QObject* owner, std::function<void()> callback)
{
    Listeners().push_back({owner, std::move(callback)});
}

/// Called by RecordingSettings::save(): the listeners reload and show the new values (they must not save back)
inline void Notify()
{
    auto& listeners = Listeners();
    std::erase_if(listeners, [](const Listener& listener) { return listener.owner.isNull(); });
    const std::vector<Listener> copy = listeners;  // a callback may subscribe
    for (const Listener& listener : copy)
        if (listener.owner)
            listener.callback();
}
}  // namespace recordingsettingsbus
