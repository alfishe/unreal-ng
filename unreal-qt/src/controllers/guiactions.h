#pragma once

/// @file guiactions.h
/// @brief A device's action events in the Qt client (control surfaces architecture §2, §3): a binding naming a core
/// action becomes a call of the core actions::ActionRegistry (core/src/emulator/actions; it runs on the instance's
/// actions worker) with the device's origin; a binding naming a command of the GUI control plane (the debugger's
/// panes, the windows, the on-screen keyboard: design §4.7) runs on the program's host in process. The GUI's verbs are
/// not core actions.
///
/// Lifetime: results come back on the worker and go to the status bar through the GUI thread; one still on its way
/// when this object goes finds the alive token false and does nothing.

#include <QObject>
#include <QPointer>
#include <QString>

#include <atomic>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "emulator/actions/actionregistry.h"

class EmulatorBinding;
class QTimer;

namespace guicontrol
{
class Host;
}

struct GuiActionHooks
{
    std::function<void(const QString&)> notice;  ///< the status bar
    /// A host device's action came back with timeline marks (`ttd.seek_by`): the device feels them (GUI thread)
    std::function<void(const std::string& device, const std::vector<std::string>& marks)> marks;
};

struct HostActionEvent;

class GuiActions : public QObject
{
    Q_OBJECT

public:
    GuiActions(EmulatorBinding* binding, GuiActionHooks hooks, QObject* parent = nullptr);
    ~GuiActions() override;

    /// The registry every action lives in (the core's)
    const actions::ActionRegistry& Registry() const;

    /// A device's action event (HostControllers::actionTriggered): `value` 1 for a press, a scrub level, an encoder's
    /// steps (with its rate and the binding's parameters); `device` the host device id (`kbd:system` = the window
    /// keyboard: the GUI's own origin). An encoder turn bound to a Press action presses once per step, at most 16
    void Trigger(const HostActionEvent& event);

    /// The program's GUI control plane host: a binding naming one of its commands (`memory.cursor_move`,
    /// `unrealqt.osd_keyboard`) goes there in process instead of the core registry (design §4.7)
    void SetGuiHost(guicontrol::Host* host);

private:
    /// Show a result's error on the GUI thread
    void Notice(const QString& text);
    /// A binding of a GUI plane command: run on the host, a held one of a repeating command repeats; false when the
    /// id is not one
    bool TriggerGui(const HostActionEvent& event);

    EmulatorBinding* _binding = nullptr;
    GuiActionHooks _hooks;
    std::shared_ptr<std::atomic<bool>> _alive;
    QPointer<guicontrol::Host> _guiHost;
    std::map<QString, QTimer*> _guiRepeats;  ///< per binding (device + command): the repeat while held
};
