#pragma once

/// @file busreport.h
/// @brief The bus analyzer as a test sees it (docs/inprogress/2026-10-11-bus-analyzer/mutation-tests.md §5): switch a bus
/// on, then read its protocol commands and its findings through the same reports every automation plane renders.

#include <map>
#include <string>
#include <vector>

#include "emulator/bus/bustrace.h"
#include "emulator/emulatorcontext.h"
#include "emulator/state/devicestate.h"

namespace bustest
{
    /// Trace (protocol level: a whole boot fits the ring) and checks on, everything earlier cleared; false when the machine
    /// has no such bus
    inline bool Enable(EmulatorContext* context, const std::string& bus)
    {
        const StateNode reply = DeviceState::BusControl(context, bus, 1, 1, true, 0, 1);
        const StateNode* available = reply.find("available");
        return available && available->b;
    }

    /// The findings as "rule@pc", oldest first
    inline std::vector<std::string> Findings(EmulatorContext* context, const std::string& bus)
    {
        std::vector<std::string> out;
        BusFindingQuery q;
        q.limit = 0;
        const StateNode report = DeviceState::BusFindings(context, bus, q);
        if (const StateNode* list = report.find("findings"))
            for (const StateNode& f : list->items)
                out.push_back(f.find("rule")->s + "@" + f.find("pc")->s);
        return out;
    }

    /// Findings per rule (also the ones the ring dropped)
    inline std::map<std::string, int64_t> FindingsByRule(EmulatorContext* context, const std::string& bus)
    {
        std::map<std::string, int64_t> out;
        const StateNode report = DeviceState::BusFindings(context, bus, BusFindingQuery{});
        if (const StateNode* checks = report.find("checks"))
            if (const StateNode* byRule = checks->find("by_rule"))
                for (const auto& [rule, count] : byRule->members)
                    out[rule] = count.i;
        return out;
    }

    /// The protocol commands in order: SD ("CMD17", "ACMD41") or ATA ("READ SECTORS (#20)")
    inline std::vector<std::string> Commands(EmulatorContext* context, const std::string& bus, const std::string& kind = "command")
    {
        std::vector<std::string> out;
        BusEventQuery q;
        std::string error;
        BusEventQueryFromStrings(kind, "", "", "", "", "0", q, error);
        const StateNode report = DeviceState::BusEvents(context, bus, q);
        if (const StateNode* list = report.find("events"))
            for (const StateNode& e : list->items)
                out.push_back(e.find("command")->s);
        return out;
    }

    inline bool Has(const std::vector<std::string>& list, const std::string& item)
    {
        for (const std::string& s : list)
            if (s == item)
                return true;
        return false;
    }

    /// "rule=count, ..." for a failure message
    inline std::string Describe(const std::map<std::string, int64_t>& byRule)
    {
        std::string text;
        for (const auto& [rule, count] : byRule)
            text += (text.empty() ? "" : ", ") + rule + "=" + std::to_string(count);
        return text.empty() ? "none" : text;
    }
}  // namespace bustest
