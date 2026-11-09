#pragma once

/// @file debuggercontrol.h
/// @brief unreal-debugger on the GUI control plane (docs/inprogress/2026-11-07-gui-control-plane, phase 5): the
/// `debugger` interface on the host's root, the views `debugger/disassembly` and `debugger/memory` under the window's
/// node, the same interfaces unreal-qt's debugger implements (R3). The views are the listing and the dump of the
/// processor in front, each naming the machine it shows. The commands run on the GUI thread and touch ecat's widgets;
/// the composed ones press the window's own buttons (F4, F2) or run a core action with the dump's address. The
/// views half only: no core header here, the machine's id and its core actions come through the hooks.

#include <QJsonObject>
#include <QObject>
#include <QPointer>
#include <QString>

#include <functional>

class DebuggerWindow;

namespace guicontrol
{
class Host;
class Node;
struct Result;
}  // namespace guicontrol

class DebuggerControl : public QObject
{
    Q_OBJECT

public:
    struct Hooks
    {
        std::function<QString()> instance;  ///< the machine's id ("" none: offline)
        std::function<QString()> endpoint;  ///< the WebAPI the machine is reached at ("" in process)
        /// A core action on the machine (ttd.find_last_write {addr}); the answer comes back on the GUI thread
        std::function<void(const QString& action, const QJsonObject& params,
                           std::function<void(const guicontrol::Result&)> done)>
            coreAction;
    };

    /// Binds on `host`; the window node `debugger` exists (its WindowBinding). The object goes with the window
    DebuggerControl(guicontrol::Host& host, DebuggerWindow* debugger, Hooks hooks);
    ~DebuggerControl() override;

    /// The machine each view shows, again (the remote emulator came or went)
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
    Hooks _hooks;
    QString _lastPane;
};
