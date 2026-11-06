#pragma once

#include <string>
#include <vector>

#include "emulator/state/statenode.h"

class EmulatorContext;

/// @file devicecatalog.h
/// @brief The device state reports a machine has, and where every automation plane reads each one.
///
/// A front end (a debugger's device list, a companion, a script) asks the catalog instead of knowing which reports
/// exist and probing them: the catalog builds every report of the table below and lists the ones whose
/// `available` is true, each with a one-line summary taken from the report itself. The same names reach every plane:
/// CLI `state devices`, Lua / Python `devices_state()`, MCP `inspect_state` aspect `devices`, REST
/// `GET /api/v1/emulator/{id}/state/devices` (and a WebSocket `call` of it).
///
/// The set changes only with the machine's devices; a slot change makes a new emulator instance, which the
/// WebSocket `instances` topic announces, so the catalog has no topic of its own.
namespace DeviceState
{
/// One report of the catalog
struct ReportEntry
{
    const char* id;      ///< "fdc"
    const char* title;   ///< "Disk controller"
    const char* route;   ///< the REST path under /api/v1/emulator/{id}/: "state/fdc"
    const char* cli;     ///< "state fdc"
    const char* script;  ///< the Lua / Python function: "fdc_state"
    const char* mcp;     ///< the MCP inspect_state aspect: "fdc"
    StateNode (*build)(EmulatorContext* context);
    /// The report says the device is there; null: its `available` alone (the AY report is available with no chip)
    bool (*present)(const StateNode& report);
    /// The one-line summary of the report; null: ReportSummary
    std::string (*summary)(const StateNode& report);
};

/// Every report the catalog knows, in the order it lists them
const std::vector<ReportEntry>& CatalogEntries();

/// The one-line summary of a built report: its `description`, else its first naming field (`device`, `card`,
/// `synthesizer`, `controller`, `chip`, `board`, `display_mode`, `scheme`, `machine`, `model`, `type`, `name`); empty
/// when it has none
std::string ReportSummary(const StateNode& report);

/// The reports this machine has: `{available, reports: [{id, title, available, summary, route, cli, script, mcp}]}`;
/// with @p all every report of the table, the absent ones with `available` false and the report's `description`
/// (why: "General Sound card not fitted (plug one: ...)"). `available` false (with `description`) without a machine
StateNode Catalog(EmulatorContext* context, bool all = false);

/// The catalog as text, one report a line: id, title, summary, then the CLI command that shows it; an absent report
/// (the @p all form) says "absent:" and why instead of the command
std::string CatalogText(const StateNode& catalog, const std::string& newline = "\n");
}  // namespace DeviceState
