#pragma once

/// @file programcontrol.h
/// @brief unreal-qt's own commands on the GUI control plane (`unrealqt.*`, design §4.2): the on-screen keyboard,
/// the Controllers window and its learn wizard. Their declaration is in unrealqtinterface.h.

#include <QJsonObject>
#include <QObject>
#include <QPointer>
#include <QString>

#include <functional>
#include <string>

namespace guicontrol
{
class Host;
}

#include "unrealqtinterface.h"

class ProgramControl : public QObject
{
    Q_OBJECT

public:
    struct Hooks
    {
        std::function<bool()> keyboardShown;
        std::function<void(bool)> showKeyboard;
        std::function<void()> showControllers;
        /// The pad's cursor on the keyboard (the handheld pad, the automation): move it, tap the key under it (false: none)
        std::function<void(int dx, int dy)> keyboardNav;
        std::function<bool()> keyboardTap;
        /// The keyboard shown whatever the model ("" the machine's own, "minimal", "steam", a picture id); the reason
        /// when refused
        std::function<QString(const std::string& id)> forceKeyboard;
        /// The keyboard's state: shown, the cursor's key, the forced keyboard
        std::function<QJsonObject()> keyboardState;
        /// The learn wizard for a host device; the reason when it cannot start
        std::function<QString(const std::string& device)> learn;
    };

    ProgramControl(guicontrol::Host& host, Hooks hooks, QObject* parent);

    /// The keyboard's state again (it was shown or hidden)
    void Publish();

private:
    QPointer<guicontrol::Host> _host;
    Hooks _hooks;
};
