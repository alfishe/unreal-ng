#pragma once

/// @file statetransfercontrol.h
/// @brief The one place that implements the machine state transfer verb for every surface (model what-if W0b,
/// docs/inprogress/2026-09-29-model-what-if/design.md): the WebAPI (POST /api/v1/emulator/{id}/snapshot/transfer, and
/// MCP `transfer_state` and the WebSocket `call` through it), the CLI (`snapshot transfer`), Lua and Python
/// (`transfer_state`) and the Qt Machine > Transfer State menu turn their input into a StateTransferRequest and the
/// StateTransferReply into their output. The reply's body is the WebAPI answer (StateNode: JSON, sol::table, py::dict),
/// so the surfaces cannot drift apart. The mechanism itself is MachineStateTransfer (machinestatetransfer.h).
///
/// The request names its target in exactly one way:
///   - `to`: an existing instance. `check` only decides (MachineStateTransfer::Check) and changes nothing. Otherwise the
///     target's machine state coordinator is asked first (Emulator::AskModelSwitch): a refusal answers Conflict (409)
///     with the coordinator's reason, before anything is paused
///   - `model` (+ `ramSizeKB`, `symbolicId`): a new instance of that model, with the source's sound cards, started
///     when `start` says so (default: when the source is running and not paused)
///
/// Answers (httpStatus and body):
///   200 the report (check, or a transfer to an existing instance that went through); 201 the report of a new instance;
///   422 the report of a refused transfer (the target cannot hold the state; nothing changed, nothing left behind);
///   400 / 404 / 409 / 500 / 503 {"error", "message"} for a request that never reached a report
///
/// Example (what a surface does):
/// @code
///   StateTransferRequest request;
///   request.sourceId = id;  request.model = "PENTAGON";  request.ramSizeKB = 512;
///   const StateTransferReply reply = StateTransferControl::Execute(request);
///   http.status = reply.httpStatus;  http.body = StateNodeToJson(reply.body);   // reply.target: the new instance
/// @endcode

#include <cstdint>
#include <memory>
#include <optional>
#include <string>

#include "emulator/state/statenode.h"
#include "loaders/snapshot/machinestatetransfer.h"

class Emulator;

struct StateTransferRequest
{
    std::string sourceId;           ///< the instance whose state moves; "" = the selected one
    std::string to;                 ///< an existing target instance (id or symbolic id as the manager resolves it)
    std::string model;              ///< or a new target of this model (short name: 48K, 128K, PENTAGON, ...)
    uint32_t ramSizeKB = 0;         ///< new target: its RAM size; 0 = the model's default
    std::string symbolicId;         ///< new target: its symbolic id
    bool check = false;             ///< existing target only: decide, change nothing
    /// Keep the source's in-frame position when both models share the frame geometry; omitted = true
    std::optional<bool> keepFramePosition;
    /// New target: start it; omitted = when the source is running and not paused
    std::optional<bool> start;
};

struct StateTransferReply
{
    int httpStatus = 200;
    /// "" when the reply carries a report; else "Not Found", "Bad Request", "Conflict", "Service Unavailable",
    /// "Internal Error" (the WebAPI's error names)
    std::string error;
    std::string message;            ///< with `error`: one sentence a user can act on
    bool hasReport = false;
    MachineStateTransfer::Report report;
    /// The WebAPI answer: the report fields (ok, mode, reason, items, copied, dropped, refused, summary) plus check,
    /// source_id, target_id, created, state, model, ram_kb; or {"error", "message"}
    StateNode body = StateNode::Object();
    /// The target: the existing instance, or the new one (nullptr when none was created)
    std::shared_ptr<Emulator> target;

    /// The report says the transfer is possible (check) or went through
    bool Ok() const { return hasReport && report.ok; }
    /// For people (the CLI, the Qt report box): a headline, then the report's summary
    std::string ToText() const;
};

class StateTransferControl
{
public:
    static StateTransferReply Execute(const StateTransferRequest& request);

    /// The report as every surface returns it: ok, mode ("clone" / "cross-model"), reason, items [{name, status,
    /// detail}], copied, dropped, refused, summary
    static StateNode ReportValue(const MachineStateTransfer::Report& report);
    /// "copied", "dropped", "refused", "note"
    static const char* ItemStatusName(MachineStateTransfer::ItemStatus status);
};
