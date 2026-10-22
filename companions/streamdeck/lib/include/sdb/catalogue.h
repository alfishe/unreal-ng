// The host's action catalogue (control surfaces actions.md §6): the ids an `action` step calls, with their value
// kind and params. Live from GET /api/v1/controllers/actions, offline from $data/streamdeck/catalogue.json (the same
// rows without the availability of the moment). Used by the studio's palette and action picker and to check a
// scenario against what the host offers (scenarios.md §6).
#pragma once
#include <filesystem>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <vector>

#include "sdb/scenario.h"

namespace sdb::cat
{

using json = nlohmann::json;

inline constexpr const char* kFormat = "unreal-ng-action-catalogue";

struct Entry
{
    std::string id, label, group;
    std::string kind = "press";  // press | delta | level | text
    bool held = false;           // the release matters (send it: "hold": true)
    bool gui = false;            // only with the Qt client
    std::string params;          // one line, e.g. "model, ram_kb?, replace?"
};

struct Catalogue
{
    std::vector<Entry> actions;  // in the host's registration order
    [[nodiscard]] const Entry* Find(const std::string& id) const;
};

// {actions: [...]} (the route's reply or the file) or a bare array; rows without an id are skipped. nullopt: not a
// catalogue.
[[nodiscard]] std::optional<Catalogue> Parse(const json& doc);
// The shipped form: {format, version, actions: [{id, label, group, kind, held, gui, params?}]}, stable order.
[[nodiscard]] json ToJson(const Catalogue& c);
[[nodiscard]] std::optional<Catalogue> Load(const std::filesystem::path& file, std::string* error = nullptr);
// GET {webapi}/api/v1/controllers/actions (a short timeout: the studio asks while the user waits).
[[nodiscard]] std::optional<Catalogue> Fetch(const std::string& webapi, std::string* error = nullptr,
                                             int timeoutMs = 1500);
// $data/streamdeck/catalogue.json
[[nodiscard]] std::filesystem::path DefaultFile();

// Warnings for a scenario's `action` steps against a catalogue: an unknown id, a held action without "hold", a
// value of the wrong kind (a dial's delta on a press action).
[[nodiscard]] std::vector<scn::Issue> Check(const scn::Scenario& s, const Catalogue& c);

}  // namespace sdb::cat
