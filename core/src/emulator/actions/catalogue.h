#pragma once

/// @file catalogue.h
/// @brief The catalogue fields the core knows for the action ids (Stream Deck integration protocol.md §5): a one
/// sentence description of every core action, and the state of the toggles (the readout `toggle.<id>`). The registry
/// applies them at Register when the registration brings none, so the executors stay where they are. The shipped
/// catalogue (data/streamdeck/catalogue.json) is written at build time (tools/actioncatalogue). The GUI's verbs are
/// not actions: they are commands of the GUI control plane (docs/emulator/design/gui-control).

#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "emulator/actions/actionregistry.h"

namespace actions
{

/// The description of a known id; "" for an unknown one
std::string DefaultDescription(std::string_view id);

struct DefaultToggle
{
    std::function<std::optional<bool>(Emulator& emulator, const ActionValue& value)> state;
    const char* stem = "";   ///< "" = the id
    const char* param = "";  ///< the param naming one of several (feature.toggle: name)
};
/// The toggle of a known id; null when the id is not a toggle
const DefaultToggle* DefaultToggleOf(std::string_view id);

/// The shipped catalogue (`unreal-ng-action-catalogue` v1: id, label, group, kind, held, params?; no availability) of
/// `actions`, as JSON text indented by one space
std::string ShippedCatalogueJson(const std::vector<ActionInfo>& actions);

}  // namespace actions
