#pragma once

/// @file debuggercontrol.h
/// @brief unreal-qt's debugger on the GUI control plane (docs/inprogress/2026-11-07-gui-control-plane §4.3): the
/// `debugger` and `metadata` interfaces on the host's root, the views `debugger/disassembly` and `debugger/memory`
/// under the debugger window's node, each naming the machine it shows. The commands run on the GUI thread and touch
/// the widgets directly; the composed ones (run to the cursor, find the last write) call the core registry with the
/// view's own address, as the debugger's buttons do.

#include <QObject>
#include <QPointer>
#include <QString>

#include <functional>

class DebuggerWindow;
class EmulatorBinding;

namespace guicontrol
{
class Host;
class Node;
}  // namespace guicontrol

class DebuggerControl : public QObject
{
    Q_OBJECT

public:
    struct Hooks
    {
        std::function<void()> showDebugger;  ///< the Debug menu's Debugger: shown, the emulation's debug mode on
        std::function<QString()> endpoint;   ///< the WebAPI this process serves ("" none yet)
    };

    /// Binds `debugger` (a child of the window's binding: the window node exists) on `host`; the object goes with
    /// the debugger window
    DebuggerControl(guicontrol::Host& host, DebuggerWindow* debugger, EmulatorBinding* binding, Hooks hooks);
    ~DebuggerControl() override;

    /// The machine each view shows, again (bound, unbound, the WebAPI port known)
    void PublishInstance();

private:
    void BindHost();
    void BindDisassembly();
    void BindMemory();
    /// The state of every node from the widgets; a node pushes only what changed
    void Publish();
    void PublishFocus();
    guicontrol::Node* View(const char* id) const;

    QPointer<guicontrol::Host> _host;
    QPointer<DebuggerWindow> _debugger;
    QPointer<EmulatorBinding> _binding;
    Hooks _hooks;
    QString _lastPane;
};
