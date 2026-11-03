#pragma once

/// @file catalogue.h
/// @brief The catalogue fields the core knows for the action ids (Stream Deck integration protocol.md §5): a one
/// sentence description of every core and GUI action, and the state of the toggles (the readout `toggle.<id>`). The
/// registry applies them at Register when the registration brings none, so the executors stay where they are and
/// the GUI's actions get their text even though the Qt client registers them. The GUI's actions are declared here too
/// (id, label, group, kind, params; unreal-qt's GuiActions binds the executors), so the shipped catalogue
/// (data/streamdeck/catalogue.json) is written at build time without the GUI (tools/actioncatalogue).

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

/// One of the Qt client's actions as the catalogue shows it (the label and group untranslated)
struct GuiActionDeclaration
{
    const char* id;
    const char* label;
    const char* group;
    ValueKind kind = ValueKind::Press;
    bool held = false;
    const char* params = "";
};
/// The Qt client's actions in its registration order
const std::vector<GuiActionDeclaration>& GuiActionDeclarations();
/// The declaration of a GUI action; null for any other id
const GuiActionDeclaration* GuiActionDeclarationOf(std::string_view id);

/// The shipped catalogue (`unreal-ng-action-catalogue` v1: id, label, group, kind, held, gui, params?; no
/// availability) of `actions` and then the GUI's declared ones they lack, as JSON text indented by one space
std::string ShippedCatalogueJson(const std::vector<ActionInfo>& actions);

}  // namespace actions
