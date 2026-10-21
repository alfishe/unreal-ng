#pragma once

/// @file hostactions_debugtools.h
/// @brief The catalogue actions of the debugging tools (Stream Deck use cases D4 breakpoints, D7 machine state, D8.2
/// labels, D10 profilers and traces; docs/inprogress/2026-10-19-streamdeck-integration/use-cases.md): breakpoints by
/// kind, address, port preset, id, kind and group; the profilers, traces and analyzers on / off, paused, cleared;
/// a register, memory and a port written; the symbol sets loaded, saved, dropped and switched. Each calls the core
/// object the WebAPI route calls (BreakpointManager with BreakpointSelection, the profilers, PortWrite, SymbolControl),
/// never HTTP, with the route's companion scope.

#include <vector>

class Emulator;

namespace actions
{

class ActionRegistry;
class RegistrationHandle;

/// Registers them on `registry`; they stay while the handles live (ActionRegistry::Instance keeps them for good)
std::vector<RegistrationHandle> RegisterDebugToolsActions(ActionRegistry& registry);

}  // namespace actions
