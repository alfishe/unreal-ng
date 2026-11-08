#pragma once

/// @file programcontrol.h
/// @brief unreal-qt's own commands on the GUI control plane (`unrealqt.*`, design §4.2): the on-screen keyboard,
/// the Controllers window and its learn wizard. The declarations (DeclareUnrealQtInterface) need no widgets, so a
/// catalogue writer links them without the program.

#include <QObject>
#include <QPointer>
#include <QString>

#include <functional>
#include <string>

namespace guicontrol
{
class Host;
}

/// Adds the `unrealqt` interface to the plane's declarations (guicontrol::AddProgramInterface); once per process
void DeclareUnrealQtInterface();

class ProgramControl : public QObject
{
    Q_OBJECT

public:
    struct Hooks
    {
        std::function<bool()> keyboardShown;
        std::function<void(bool)> showKeyboard;
        std::function<void()> showControllers;
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
