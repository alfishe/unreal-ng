// Edits on the scenario JSON document: what the editor's UI and its WebAPI do. Each op keeps the document
// parseable when it was before (unique ids, valid slots); the editor wraps every op in one undo step.
#pragma once
#include <optional>
#include <string>
#include <string_view>

#include "sdb/scenario.h"

namespace sdb::scn::ops
{

struct Result
{
    bool ok = true;
    std::string error;
    std::string id;  // the node created / moved / renamed
    static Result Fail(std::string e)
    {
        return {false, std::move(e), {}};
    }
};

[[nodiscard]] json NewScenario(const Model& m, const std::string& name);

[[nodiscard]] json* FindNode(json& doc, std::string_view id);
[[nodiscard]] const json* FindNode(const json& doc, std::string_view id);
[[nodiscard]] json* ParentOf(json& doc, std::string_view id);
[[nodiscard]] std::string UniqueId(const json& doc, const std::string& base);
[[nodiscard]] std::string Slug(const std::string& title);

// The model of the document ("model" field), the slot a back key reserves in `menu` (from the last good parse).
[[nodiscard]] const Model* ModelOf(const json& doc);
[[nodiscard]] std::optional<Slot> ReservedSlot(const json& doc, std::string_view menu);
[[nodiscard]] const json* NodeAt(const json& doc, std::string_view menu, Slot at);
[[nodiscard]] std::optional<Slot> FreeSlot(const json& doc, std::string_view menu);

// `node` gets unique ids (its own and its subtree's); `at` defaults to the first free slot.
Result InsertNode(json& doc, std::string_view parent, std::optional<Slot> at, json node);
Result RemoveNode(json& doc, std::string_view id);
// Into another menu or another slot; an occupied target is an error unless `swap` (the occupant takes the old place).
Result MoveNode(json& doc, std::string_view id, std::string_view parent, Slot at, bool swap);
// JSON merge patch (RFC 7386); a new "id" is made unique and nav targets pointing at the old id follow it.
Result UpdateNode(json& doc, std::string_view id, const json& patch);
Result DuplicateNode(json& doc, std::string_view id, std::optional<Slot> at);

// ---- subtrees between files
// A node as a scenario document of its own: a menu becomes the root; an action is wrapped in a root menu (marked
// "fragment": true) that keeps its slot. `outside` gets nav targets that point out of the subtree.
[[nodiscard]] json ExtractSubtree(const json& doc, std::string_view id, std::vector<std::string>* outside = nullptr);
// Copies a subtree from another document (its root, or `node` in it) into `parent`:
//   menu  - as one submenu at `at` (default: first free key)
//   items - the root's items go into `parent` itself, each at its own slot when free, else the next free one
// Ids are made unique; nav targets inside the copy follow the renames.
enum class ImportMode
{
    Menu,
    Items
};
Result ImportSubtree(json& doc, std::string_view parent, std::optional<Slot> at, const json& fragment, ImportMode mode,
                     std::string_view node = {});
// Turns a node into a link to a subtree in another file ({"type": "include", "file", "node"?}), keeping id and slot.
Result LinkNode(json& doc, std::string_view id, const std::string& file, const std::string& node = {});
// Replaces a node (e.g. an include) with `replacement` (its resolved subtree): ids made unique, slot kept.
Result InlineNode(json& doc, std::string_view id, json replacement);

}  // namespace sdb::scn::ops
