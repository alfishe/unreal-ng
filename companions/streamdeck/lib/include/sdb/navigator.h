// Stack navigation through a scenario and what the device shows for the current menu.
#pragma once
#include <map>
#include <string>
#include <string_view>
#include <vector>

#include "sdb/scenario.h"

namespace sdb::scn
{

class Navigator
{
public:
    explicit Navigator(const Scenario& s);

    [[nodiscard]] const Node& Current() const
    {
        return *_stack.back();
    }
    [[nodiscard]] const std::vector<const Node*>& Stack() const
    {
        return _stack;
    }
    [[nodiscard]] size_t Depth() const
    {
        return _stack.size() - 1;
    }
    [[nodiscard]] std::vector<std::string> Ids() const;

    bool Enter(const Node& menu);  // stack on: push; off: replace
    bool Back();                   // false at the root
    void Home();
    bool PopTo(size_t depth);        // 0 = root
    bool GoTo(std::string_view id);  // a menu: its path; an action: its menu's path
    // After a reload: keep the longest prefix of ids that still exist (by id) in the new scenario. The one-argument
    // form reads the ids from the current stack, so the old scenario must still be alive; when it has been replaced
    // in place (the runtime's reload), take Ids() before and pass them.
    void Rebind(const Scenario& s);
    void Rebind(const Scenario& s, const std::vector<std::string>& old);

private:
    const Scenario* _s;
    std::vector<const Node*> _stack;
};

// What the device shows for the navigator's current menu.
struct Page
{
    std::vector<const Node*> keys;   // one per model key, nullptr = empty
    int backKey = -1;                // key index of the back key, -1 none
    Crumbs crumbs = Crumbs::None;    // Strip, Key or None
    std::vector<const Dial*> dials;  // one per model dial (inherited bindings resolved)
    const Strip* strip = nullptr;
    std::vector<std::string> path;  // titles, root first
    int autoExit = 0;
};
[[nodiscard]] Page Layout(const Scenario& s, const Navigator& nav, const Model& m);

// What the page means, as compact JSON for agents (no image analysis needed):
//   {menu, path:[{id,title}], breadcrumbs, back_key, can_back,
//    keys:[{key,row,col,role:item|back,id,type,title,label,tags,kind,then,on}], dials:[{index,label,turn,press}],
//    strip}
// Empty keys are left out. `toggles` = node id -> on.
[[nodiscard]] json DescribePage(const Scenario& s, const Navigator& nav, const Model& m, const Page& p,
                                const std::map<std::string, bool>& toggles = {});
// One line per node of a document, filtered: tag, type (menu|action), kind, menu (direct parent id), text (in id /
// title).
[[nodiscard]] json ListNodes(const json& doc, const json& filter = json::object());

}  // namespace sdb::scn
