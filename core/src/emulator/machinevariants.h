#pragma once

/// @file machinevariants.h
/// @brief Machine variants: a model with a fixed board configuration, created by
/// name like any model ("TSL-VDAC2" = TS-Conf in its VDAC2 firmware build with the
/// card fitted). One table that every surface reads - the Qt Machine menu, the
/// model lists of WebAPI / MCP / CLI, and EmulatorManager when a create or a model
/// switch names one - so all of them offer and report the same machines.
///
/// A variant is its base model plus a config override applied after the model's
/// configs/<folder>/unreal.ini is loaded. A machine whose configuration matches a
/// variant (the same board set in an ini) is reported as that variant.

#include <cstdint>
#include <string>
#include <vector>

struct CONFIG;

struct MachineVariant
{
    const char* name;         ///< "TSL-VDAC2": what a create request / the menu uses
    const char* title;        ///< "TS-Conf + VDAC2 (FT812)": the menu text
    const char* description;  ///< one line for model lists and status tips
    const char* baseModel;    ///< short name of the base model ("TSL")
    uint32_t ramKb;           ///< the base model's RAM size the variant runs with
    void (*apply)(CONFIG& config);              ///< the board configuration
    bool (*matches)(const CONFIG& config);      ///< a loaded machine is this variant
    bool (*supported)(std::string* reason);     ///< this build can create it
};

namespace MachineVariants
{

const std::vector<MachineVariant>& All();
/// By name or alias, case-insensitive; nullptr when the name is not a variant
const MachineVariant* Find(const std::string& name);
/// The variant a loaded machine is, nullptr for a plain model
const MachineVariant* Of(const CONFIG& config);

} // namespace MachineVariants
