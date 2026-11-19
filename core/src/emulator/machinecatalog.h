#pragma once

/// @file machinecatalog.h
/// @brief The machines a user can pick, grouped: family -> machine -> RAM size
/// (docs/inprogress/2026-11-16-machine-families). One tree built from the model table
/// (Config::mem_model), the machine variants (MachineVariants) and the ZX-Poly
/// configurations (ZXPolyGroup): the Qt Machine menu, the handheld Model picker, WebAPI
/// GET /emulator/machines (remote clients), MCP, CLI, Python and Lua all read it, so they
/// offer the same machines in the same order.
///
/// Names are unchanged: a machine is what a create request names ("ATM710", "TSL-VDAC2",
/// "ZXPOLY-128K"); only the grouping is new.

#include <cstdint>
#include <string>
#include <vector>

#include "emulator/state/statenode.h"

class Emulator;

enum class MachineKind : uint8_t
{
    Model,    ///< a model of the model table
    Variant,  ///< a machine variant: a base model with a fixed board
    ZXPoly,   ///< a ZX-Poly configuration: four synchronized instances of a base model
};

struct MachineEntry
{
    std::string name;                ///< what a create request names ("ATM710", "TSL-VDAC2")
    std::string title;               ///< the menu text ("ATM-Turbo 2+ v7.10")
    std::string description;         ///< one line for status tips (variants), may be empty
    MachineKind kind = MachineKind::Model;
    std::string baseModel;           ///< the model a variant / ZX-Poly configuration is built on; the name for a model
    std::vector<uint32_t> ramSizesKb;  ///< ascending; one size for a variant or a ZX-Poly configuration
    uint32_t defaultRamKb = 0;
    bool creatable = false;          ///< this build can create it (port decoder, config folder, optional libraries)
};

struct MachineFamily
{
    std::string id;     ///< stable key for clients ("atm", "zx-evo")
    std::string title;  ///< the menu text ("ATM Turbo")
    std::vector<MachineEntry> machines;  ///< in menu order
};

/// Where a machine sits in the tree
struct MachinePlace
{
    int family = -1;   ///< index into the families, -1 when not found
    int machine = -1;  ///< index into the family's machines
};

namespace MachineCatalog
{

/// Every family with every machine, creatable or not, in menu order. Built once
const std::vector<MachineFamily>& All();

/// The menus' tree: only creatable machines, families left empty dropped
std::vector<MachineFamily> Creatable();

/// The family a machine name (or alias) belongs to, case-insensitive, in the tree given
MachinePlace Locate(const std::vector<MachineFamily>& families, const std::string& name);

/// The family id of a machine name ("atm" for "ATM710"); empty when unknown
std::string FamilyOf(const std::string& name);

/// The catalog name of a running machine: its variant, its ZX-Poly configuration or its model short name
std::string NameOf(Emulator& emulator);

/// The tree as the automation planes give it (WebAPI GET /emulator/machines, MCP, Python, Lua): {"families": [{"id",
/// "title", "machines": [{"name", "title", "description"?, "kind" (model|variant|zxpoly), "base_model",
/// "ram_sizes_kb", "default_ram_kb", "creatable"}]}], "current"?: {"family", "machine", "ram_kb"}}. The current
/// machine is given when `current` names one (NameOf) and `currentRamKb` its RAM size
StateNode ToState(const std::vector<MachineFamily>& families, const std::string& current = {}, uint32_t currentRamKb = 0);

/// "ATM-Turbo 2+ v7.10 1024K": the menu text of a machine with a RAM size (the size only when it has several)
std::string Title(const MachineEntry& machine, uint32_t ramKb);

/// The menu text of a running machine ("ATM-Turbo 2+ v7.10 512K", "TS-Conf + VDAC2 (FT812)"): what the Machine menu,
/// the window title and the handheld menu show as the current configuration; empty when the machine is not known
std::string TitleOf(Emulator& emulator);

} // namespace MachineCatalog
