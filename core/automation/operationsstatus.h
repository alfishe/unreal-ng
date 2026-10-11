#pragma once

/// @file operationsstatus.h
/// @brief The machine's operation log (the machine state coordinator's, always on: who reset, loaded, inserted or
/// ejected what, at which frame, with which result - refusals included), shared by every automation surface: CLI
/// `ops`, WebAPI GET /api/v1/emulator/{id}/operations, Lua / Python operations(), MCP emulator_manage operations
/// (through the WebAPI). The report is one StateNode; each surface converts it with its own generic converter.
///
/// Design: docs/inprogress/2026-10-09-machine-state-coordinator/architecture.md §7.

#include <emulator/coordinator/operationlog.h>
#include <emulator/coordinator/ports.h>
#include <emulator/emulator.h>
#include <emulator/state/statenode.h>

#include <cstddef>
#include <cstdint>
#include <sstream>
#include <string>
#include <vector>

namespace OperationsStatus
{
/// One entry as a node: seq, frame, t_in_frame, wall_ms, kind, source, mode, status, effect, summary, detail,
/// repeats
inline StateNode EntryNode(const coordinator::OperationLogEntry& e)
{
    StateNode node = StateNode::Object();
    node["seq"] = e.seq;
    node["frame"] = e.time.frame;
    node["t_in_frame"] = static_cast<unsigned>(e.time.tInFrame);
    node["wall_ms"] = e.time.wallMs;
    node["kind"] = coordinator::OpKindName(e.kind);
    node["source"] = coordinator::OpSourceName(e.source);
    node["mode"] = coordinator::MachineModeName(e.mode);
    node["status"] = coordinator::OpStatusName(e.status);
    node["effect"] = coordinator::SessionEffectName(e.effect);
    node["summary"] = e.summary;
    node["detail"] = e.detail;
    node["repeats"] = static_cast<uint64_t>(e.repeats);
    return node;
}

/// The report: mode (what the machine is doing now), last_seq, capacity, entries[] oldest first. @p since: only
/// entries with a larger seq (poll with the last_seq of the previous answer); @p limit: only the newest N (0: all)
inline StateNode Report(Emulator* emulator, uint64_t since = 0, size_t limit = 0)
{
    StateNode node = StateNode::Object();
    if (!emulator)
        return node;
    const coordinator::OperationLog& log = emulator->GetOperationLog();
    std::vector<coordinator::OperationLogEntry> entries = log.Entries(since);
    if (limit > 0 && entries.size() > limit)
        entries.erase(entries.begin(), entries.end() - static_cast<std::ptrdiff_t>(limit));
    node["mode"] = coordinator::MachineModeName(emulator->MachineOps().Mode());
    node["last_seq"] = log.LastSeq();
    node["capacity"] = static_cast<uint64_t>(coordinator::OperationLog::kCapacity);
    StateNode list = StateNode::Array();
    for (const coordinator::OperationLogEntry& e : entries)
        list.items.push_back(EntryNode(e));
    node["entries"] = std::move(list);
    return node;
}

/// The report as text, one operation per line (the CLI's answer)
inline std::string Text(Emulator* emulator, uint64_t since = 0, size_t limit = 0)
{
    if (!emulator)
        return "No emulator";
    const coordinator::OperationLog& log = emulator->GetOperationLog();
    std::vector<coordinator::OperationLogEntry> entries = log.Entries(since);
    if (limit > 0 && entries.size() > limit)
        entries.erase(entries.begin(), entries.end() - static_cast<std::ptrdiff_t>(limit));
    std::ostringstream out;
    out << "Operations (mode " << coordinator::MachineModeName(emulator->MachineOps().Mode()) << ", last #"
        << log.LastSeq() << ", " << entries.size() << " shown, ring of " << coordinator::OperationLog::kCapacity
        << "):";
    if (entries.empty())
        out << "\n  (none)";
    for (const coordinator::OperationLogEntry& e : entries)
    {
        out << "\n  #" << e.seq << "  frame " << e.time.frame << " t " << e.time.tInFrame << "  "
            << coordinator::OpKindName(e.kind);
        if (!e.summary.empty())
            out << " " << e.summary;
        if (e.repeats > 1)
            out << " x" << e.repeats;
        out << "  " << coordinator::OpStatusName(e.status) << "  [" << coordinator::MachineModeName(e.mode) << ", "
            << coordinator::OpSourceName(e.source) << "]";
        if (e.effect != coordinator::SessionEffect::None)
            out << "  ttd: " << coordinator::SessionEffectName(e.effect);
        if (!e.detail.empty())
            out << "  - " << e.detail;
    }
    return out.str();
}
}  // namespace OperationsStatus
