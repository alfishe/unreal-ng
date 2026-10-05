#pragma once

/// @file snapshotreport.h
/// @brief SnapshotReport - what a load did, the same record for every format and every surface (snapshot pipeline P1,
/// PLAN #84). Generalizes szx::Report: the commit that ran, the chain of verdicts that led to it, one entry per item
/// (applied / approximated / ignored / refused / unknown) and the warnings.

#include <string>
#include <vector>

#include "emulator/state/statenode.h"

namespace snapshot
{
enum class Outcome : uint8_t
{
    Applied,        ///< its state is in the machine
    Approximated,   ///< applied as far as this machine can hold it
    Ignored,        ///< understood, not applied (hardware we do not emulate, ...)
    Refused,        ///< stopped the load
    Unknown,        ///< an id we do not know: skipped, as the format asks
};

const char* ToText(Outcome outcome);

struct ReportItem
{
    std::string item;     ///< "Z80R", "RAMP 5", "bank 3", "7FFD", ...
    Outcome outcome = Outcome::Applied;
    std::string note;
};

struct Report
{
    std::string format;               ///< "sna", "z80", ...
    std::string machineHint;          ///< what the file says it was made on
    std::string commit = "legacy";    ///< who wrote the machine: "legacy", "sprinter-zx", ...
    bool refused = false;
    std::string reason;               ///< why, when refused
    std::string needs;                ///< machine-readable hint with a refusal ("zx_mode", "model:PENTAGON-512")
    std::vector<std::string> verdicts;    ///< who was asked and what they said, in order
    std::vector<ReportItem> items;
    std::vector<std::string> warnings;

    void Add(std::string item, Outcome outcome, std::string note = {});
    void Refuse(std::string why, std::string needsHint = {});
    std::string ToText() const;
    StateNode ToStateNode() const;
};
}  // namespace snapshot
