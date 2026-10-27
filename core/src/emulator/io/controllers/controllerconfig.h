#pragma once

/// @file controllerconfig.h
/// @brief The connector plugs a per-model INI states (controller manager C1b, retro-devices.md §1, architecture.md
/// §12): `[CONTROLLERS] <connector> = <retro device>[, option=value ...]`, the place `[MEDIA]` has for media.
///
/// @code
///   [CONTROLLERS]
///   keyjoy.1 = sinclair-joystick, port=2
///   keyjoy.2 = keys-joystick, keys=up:q,down:a,left:o,right:p,fire:m
///   ; none leaves a socket empty
/// @endcode
///
/// A value is split at commas: the first item is the device, an item with `=` starts an option, an item without one
/// continues the option before it (so `keys=` keeps its own commas). Legacy `[INPUT] Joystick=SINCLAIR1 | SINCLAIR2 |
/// CURSOR | QAOP` (Unreal Speccy's choices) is read as the default of `keyjoy.1` when [CONTROLLERS] does not name it;
/// Kempston's fitting stays the Joystick device's (`Joystick=NONE`).

#include <map>
#include <string>
#include <vector>

class IniFile;
class EmulatorContext;

namespace controllers
{

struct ControllerSetEntry
{
    std::string connector;  ///< as written: an id, alias or kind:index
    std::string device;     ///< a retro device kind, or "none"
    std::map<std::string, std::string> options;
    std::string where;  ///< "[CONTROLLERS] keyjoy.1", for the report
};

namespace ControllerConfig
{

/// [CONTROLLERS] and the legacy [INPUT] Joystick= choices; problems go to @p report
std::vector<ControllerSetEntry> FromIni(const IniFile& ini, std::vector<std::string>* report = nullptr);

/// Plug the configured set through ControllerControl (the same checks as every surface); returns the problems
std::vector<std::string> Apply(EmulatorContext* context, const std::vector<ControllerSetEntry>& entries);

}  // namespace ControllerConfig

}  // namespace controllers
