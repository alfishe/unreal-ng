#pragma once

/// @file defaultactions.h
/// @brief The action catalogue of debug-and-ttd.md §3 over the bound emulator: run control through the debugger
/// window's own slots, time travel through TTDControl (the verbs the MCP time_travel tool calls), capture and machine
/// actions through the main window. The analog scrub and the encoder's jog move through the history with `seek`. What
/// has no verb yet (OSD keyboard) is registered with its reason, so a binding to it says why instead of doing nothing.

#include <functional>

class ActionRegistry;
class EmulatorBinding;
class QObject;
class QString;

struct DefaultActionHooks
{
    QObject* debugger = nullptr;           ///< DebuggerWindow: continueExecution, stepIn, ... invoked by name
    std::function<void()> showDebugger;
    std::function<void()> screenshot;
    std::function<void(const QString&)> notice;  ///< the status bar: why a scrub stopped
};

void RegisterDefaultActions(ActionRegistry& registry, EmulatorBinding* binding, DefaultActionHooks hooks);
