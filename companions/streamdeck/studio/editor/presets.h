// The editor's palette: ready-made nodes (a face + an action) grouped by category. The generic ones are built in;
// a user's own come from <config>/unreal-ng/streamdeck/presets/*.json ({"presets": [{category, name, description,
// node}]}). The unreal-ng actions join from the host's catalogue when the action kind exists (design: studio.md §4).
#pragma once
#include <QString>
#include <nlohmann/json.hpp>
#include <vector>

struct Preset
{
    QString category, name, description;
    nlohmann::json node;  // a node without id / at (the editor assigns both)
};

[[nodiscard]] const std::vector<Preset>& Presets();
[[nodiscard]] const Preset* FindPreset(const QString& name);
[[nodiscard]] QString UserPresetDir();

inline constexpr const char* kPresetMime = "application/x-unreal-streamdeck-preset";
inline constexpr const char* kNodeMime = "application/x-unreal-streamdeck-node";
