// Scenarios: a tree of menus (inner nodes: a layout of keys, dials and the strip) and actions (leaves).
// File format, rules and rationale: docs/inprogress/2026-10-19-streamdeck-integration/scenarios.md
#pragma once
#include <nlohmann/json.hpp>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "sdb/draw.h"
#include "sdb/models.h"

namespace sdb::scn
{

using json = nlohmann::json;

inline constexpr int kFormatVersion = 1;
inline constexpr int kMaxMultiDepth = 8;

// Where a menu shows the path: inherit from the parent, auto (root only: strip on touch windows, else a key).
enum class Crumbs
{
    Inherit,
    Auto,
    Strip,
    Key,
    None
};
[[nodiscard]] const char* ToString(Crumbs c);
[[nodiscard]] std::optional<Crumbs> CrumbsByName(std::string_view s);

struct Slot
{
    int row = -1, col = -1;
    [[nodiscard]] bool Valid() const
    {
        return row >= 0 && col >= 0;
    }
    bool operator==(const Slot&) const = default;
};

struct Face
{
    std::string label, icon, svg, image, fit = "contain";
    draw::Color bg{16, 16, 28}, fg{255, 255, 255};
};

struct Action
{
    std::string kind;  // one of ActionKinds()
    json spec;         // the whole "do" object
};
// Every action kind Validate accepts and Execute runs, in the order the editor offers them:
// webapi http multi toggle first dir delay ttd_seek run_toggle shell open keys nav brightness
[[nodiscard]] std::span<const std::string_view> ActionKinds();

struct Dial
{
    int index = 0;
    std::string label;
    std::optional<Action> turn, press;
};

struct Strip
{
    std::string content = "title";  // breadcrumbs title text image none
    std::string text, image;
    std::string status;  // a live readout at the right: "ttd" = time travel (recording span / position)
    double fps = 50;     // frames a second for its time codes (Pentagon 48.83)
};

struct Back
{
    enum class Mode
    {
        Inherit,
        At,
        None
    } mode = Mode::Inherit;
    Slot at;
};

struct Nav
{
    Crumbs crumbs = Crumbs::Inherit;
    Back back;
    int autoExit = -1;              // seconds, 0 off, -1 inherit
    std::string dials = "inherit";  // inherit | own
};

struct Node
{
    enum class Type
    {
        Menu,
        Action
    } type = Type::Menu;
    std::string id, title;
    std::vector<std::string> tags;  // free labels for search / agents ("debug", "ttd", "danger")
    Slot at;                        // position in the parent menu
    Face face;
    // menu
    std::vector<Node> items;
    std::vector<Dial> dials;
    Strip strip;
    Nav nav;
    // action
    Action action;
    std::string then = "stay";   // stay | back | home
    std::optional<Face> faceOn;  // toggle: face while on
    [[nodiscard]] bool IsMenu() const
    {
        return type == Type::Menu;
    }
};

struct Settings
{
    int brightness = 50;
    std::string webapi = "http://127.0.0.1:8090", emulator = "auto", companionFile;
    bool stack = true;
    Crumbs crumbs = Crumbs::Auto;
    Back back{Back::Mode::At, {0, 0}};
    bool homeOnHold = true;
    int autoExit = 0;
    bool inheritDials = true;
};

struct Scenario
{
    std::string name;
    const Model* model = nullptr;
    Settings settings;
    Node root;
};

struct Issue
{
    std::string node, message;
    bool error = true;  // false = warning
};

// Parses and validates; nullopt when there are errors (issues says why). Warnings keep the scenario.
[[nodiscard]] std::optional<Scenario> Parse(const json& doc, std::vector<Issue>& issues);
[[nodiscard]] std::vector<Issue> Validate(const Scenario& s);
// Structure only (no validation): for tools that need the tree of a document that may have issues.
[[nodiscard]] std::optional<Scenario> ParseLoose(const json& doc);
[[nodiscard]] json ToJson(const Scenario& s);
[[nodiscard]] json ToJson(const Node& n);

[[nodiscard]] const Node* Find(const Node& root, std::string_view id);
// root .. node (empty when not found)
[[nodiscard]] std::vector<const Node*> PathTo(const Node& root, std::string_view id);

// Effective navigation for the last menu of `path` (root first).
struct ResolvedNav
{
    Crumbs crumbs = Crumbs::None;  // Strip, Key or None
    std::optional<Slot> backKey;   // reserved slot, when a back key is shown
    int autoExit = 0;
};
[[nodiscard]] ResolvedNav ResolveNav(const Scenario& s, const std::vector<const Node*>& path);

}  // namespace sdb::scn
