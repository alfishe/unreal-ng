#pragma once

/// @file guiactions.h
/// @brief The Qt client's part of the host actions (control surfaces architecture §2, §3): the core
/// actions::ActionRegistry (core/src/emulator/actions) holds the catalogue and runs every action on the instance's
/// actions worker; this registers the GUI ones there (the debugger window and its panes, the screenshot to the
/// clipboard, the analog scrub, the OSD stubs) and turns a device's action events into registry calls with the origin
/// of the device. GUI executors run on the GUI thread: the worker posts them and answers at once.
///
/// Lifetime: the registrations end with this object; a GUI call still queued then finds the alive token false and does
/// nothing. Results come back on the worker and go to the status bar through the GUI thread.

#include <QObject>
#include <QString>

#include <atomic>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "emulator/actions/actionregistry.h"

class EmulatorBinding;

struct GuiActionHooks
{
    QObject* debugger = nullptr;  ///< DebuggerWindow: focusNextPane, paneKey, ... invoked by name
    std::function<void()> showDebugger;
    std::function<void()> screenshot;
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

private:
    class Scrub;

    using GuiRun = std::function<QString(const std::string& instanceId, const actions::ActionValue& value,
                                         const actions::ActionOrigin& origin)>;
    /// An executor that runs `run` on the GUI thread and answers at once; its reason goes to the status bar
    actions::Executor OnGui(GuiRun run);
    void Add(const char* id, const QString& label, const QString& group, actions::ValueKind kind, GuiRun run,
             actions::ActionRules rules = {}, bool held = false);
    /// Show a result's error on the GUI thread
    void Notice(const QString& text);

    EmulatorBinding* _binding = nullptr;
    GuiActionHooks _hooks;
    std::shared_ptr<std::atomic<bool>> _alive;
    std::shared_ptr<Scrub> _scrub;
    std::vector<actions::RegistrationHandle> _handles;
};
