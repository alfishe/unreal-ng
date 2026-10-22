#include "debugger/breakpoints/breakpointselection.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>

namespace
{
struct KindName
{
    const char* name;
    BreakpointSelection::Kind kind;
};

constexpr KindName kKindNames[] = {
    {"exec", BreakpointSelection::Kind::Execute},
    {"execution", BreakpointSelection::Kind::Execute},
    {"execute", BreakpointSelection::Kind::Execute},
    {"read", BreakpointSelection::Kind::Read},
    {"write", BreakpointSelection::Kind::Write},
    {"mem", BreakpointSelection::Kind::Memory},
    {"memory", BreakpointSelection::Kind::Memory},
    {"port", BreakpointSelection::Kind::Port},
    {"io", BreakpointSelection::Kind::Port},
    {"in", BreakpointSelection::Kind::PortIn},
    {"port_in", BreakpointSelection::Kind::PortIn},
    {"out", BreakpointSelection::Kind::PortOut},
    {"port_out", BreakpointSelection::Kind::PortOut},
    {"nextreg", BreakpointSelection::Kind::NextReg},
    {"nextreg_read", BreakpointSelection::Kind::NextRegRead},
    {"nextreg_write", BreakpointSelection::Kind::NextRegWrite},
    {"beam", BreakpointSelection::Kind::Beam},
    {"copper", BreakpointSelection::Kind::Copper},
    {"magic", BreakpointSelection::Kind::Magic},
    {"keyboard", BreakpointSelection::Kind::Keyboard},
    {"key", BreakpointSelection::Kind::Keyboard},
};

std::string Lower(std::string text)
{
    std::transform(text.begin(), text.end(), text.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return text;
}

/// An id: decimal, 0x.., #.. or $..
bool ParseId(const std::string& text, uint16_t& id)
{
    std::string digits = text;
    int base = 10;
    if (digits.size() > 2 && digits[0] == '0' && (digits[1] == 'x' || digits[1] == 'X'))
        digits = digits.substr(2), base = 16;
    else if (digits.size() > 1 && (digits[0] == '#' || digits[0] == '$'))
        digits = digits.substr(1), base = 16;
    if (digits.empty())
        return false;
    char* end = nullptr;
    const unsigned long value = std::strtoul(digits.c_str(), &end, base);
    if (end == nullptr || *end != '\0' || value >= BRK_INVALID)
        return false;
    id = static_cast<uint16_t>(value);
    return true;
}
}  // namespace

BreakpointSelection BreakpointSelection::All()
{
    return {};
}

BreakpointSelection BreakpointSelection::Id(uint16_t id)
{
    BreakpointSelection selection;
    selection._kind = Kind::Id;
    selection._id = id;
    return selection;
}

BreakpointSelection BreakpointSelection::Group(std::string name)
{
    BreakpointSelection selection;
    selection._kind = Kind::Group;
    selection._group = std::move(name);
    return selection;
}

BreakpointSelection BreakpointSelection::OfKind(Kind kind)
{
    BreakpointSelection selection;
    selection._kind = kind;
    return selection;
}

const char* BreakpointSelection::KindNames()
{
    return "exec, read, write, mem, port, in, out, nextreg, nextreg_read, nextreg_write, beam, copper, magic, keyboard";
}

bool BreakpointSelection::Parse(const std::string& text, BreakpointSelection& selection, std::string& error)
{
    const std::string lower = Lower(text);
    if (lower.empty())
    {
        error = std::string("select: all, a breakpoint id, a kind (") + KindNames() + ") or group:<name>";
        return false;
    }
    if (lower == "all" || lower == "*")
    {
        selection = All();
        return true;
    }
    if (lower.rfind("group:", 0) == 0)
    {
        const std::string name = text.substr(6);
        if (name.empty())
        {
            error = "group:<name> needs a group name";
            return false;
        }
        selection = Group(name);
        return true;
    }
    uint16_t id = 0;
    if (ParseId(lower, id))
    {
        selection = Id(id);
        return true;
    }
    for (const KindName& entry : kKindNames)
        if (lower == entry.name)
        {
            selection = OfKind(entry.kind);
            return true;
        }
    error = "Unknown selection '" + text + "': all, a breakpoint id, a kind (" + KindNames() + ") or group:<name>";
    return false;
}

std::string BreakpointSelection::Name() const
{
    switch (_kind)
    {
        case Kind::All: return "all";
        case Kind::Id: return std::to_string(_id);
        case Kind::Group: return "group:" + _group;
        default: break;
    }
    for (const KindName& entry : kKindNames)
        if (entry.kind == _kind)
            return entry.name;
    return {};
}

bool BreakpointSelection::Matches(const BreakpointDescriptor& bp) const
{
    if (!BreakpointManager::IsUserBreakpoint(bp))
        return false;
    switch (_kind)
    {
        case Kind::All: return true;
        case Kind::Id: return bp.breakpointID == _id;
        case Kind::Group: return bp.group == _group;
        case Kind::Execute: return bp.type == BRK_MEMORY && (bp.memoryType & BRK_MEM_EXECUTE) != 0;
        case Kind::Read: return bp.type == BRK_MEMORY && (bp.memoryType & BRK_MEM_READ) != 0;
        case Kind::Write: return bp.type == BRK_MEMORY && (bp.memoryType & BRK_MEM_WRITE) != 0;
        case Kind::Memory: return bp.type == BRK_MEMORY;
        case Kind::Port: return bp.type == BRK_IO;
        case Kind::PortIn: return bp.type == BRK_IO && (bp.ioType & BRK_IO_IN) != 0;
        case Kind::PortOut: return bp.type == BRK_IO && (bp.ioType & BRK_IO_OUT) != 0;
        case Kind::NextReg: return bp.type == BRK_NEXTREG;
        case Kind::NextRegRead: return bp.type == BRK_NEXTREG && (bp.ioType & BRK_IO_IN) != 0;
        case Kind::NextRegWrite: return bp.type == BRK_NEXTREG && (bp.ioType & BRK_IO_OUT) != 0;
        case Kind::Beam: return bp.type == BRK_BEAM;
        case Kind::Copper: return bp.type == BRK_COPPER;
        case Kind::Magic: return bp.type == BRK_MAGIC;
        case Kind::Keyboard: return bp.type == BRK_KEYBOARD;
    }
    return false;
}

std::vector<uint16_t> BreakpointSelection::Select(const BreakpointManager& manager) const
{
    std::vector<uint16_t> ids;
    for (const auto& [id, breakpoint] : manager.GetAllBreakpoints())
        if (breakpoint && Matches(*breakpoint))
            ids.push_back(id);
    return ids;
}

BreakpointSelection::Outcome BreakpointSelection::SetActive(BreakpointManager& manager, bool active) const
{
    Outcome outcome;
    BreakpointManager::Batch batch(manager);  // one repaint, one breakpoints_changed
    for (const auto& [id, breakpoint] : manager.GetAllBreakpoints())
    {
        if (!breakpoint || !Matches(*breakpoint))
            continue;
        outcome.selected++;
        if (breakpoint->active == active)
            continue;
        if (active ? manager.ActivateBreakpoint(id) : manager.DeactivateBreakpoint(id))
            outcome.changed++;
    }
    return outcome;
}

BreakpointSelection::Outcome BreakpointSelection::Toggle(BreakpointManager& manager, bool& active) const
{
    active = !IsActive(manager).value_or(true);
    return SetActive(manager, active);
}

BreakpointSelection::Outcome BreakpointSelection::Remove(BreakpointManager& manager) const
{
    Outcome outcome;
    BreakpointManager::Batch batch(manager);
    for (const uint16_t id : Select(manager))
    {
        outcome.selected++;
        if (manager.RemoveBreakpointByID(id))
            outcome.changed++;
    }
    return outcome;
}

std::optional<bool> BreakpointSelection::IsActive(const BreakpointManager& manager) const
{
    bool any = false;
    for (const auto& [id, breakpoint] : manager.GetAllBreakpoints())
    {
        if (!breakpoint || !Matches(*breakpoint))
            continue;
        any = true;
        if (!breakpoint->active)
            return false;
    }
    if (!any)
        return std::nullopt;
    return true;
}

bool BreakpointSelection::Run(BreakpointManager& manager, const std::string& select, const std::string& op,
                              StateNode& out, std::string& error)
{
    BreakpointSelection selection;
    if (!Parse(select.empty() ? std::string("all") : select, selection, error))
        return false;
    Outcome outcome;
    if (op == "enable" || op == "on")
        outcome = selection.SetActive(manager, true);
    else if (op == "disable" || op == "off")
        outcome = selection.SetActive(manager, false);
    else if (op == "toggle")
    {
        bool active = false;
        outcome = selection.Toggle(manager, active);
    }
    else if (op == "remove")
        outcome = selection.Remove(manager);
    else
    {
        error = "op: enable, disable, toggle or remove";
        return false;
    }
    out = StateNode::Object();
    out["select"] = selection.Name();
    out["selected"] = static_cast<int64_t>(outcome.selected);
    out["changed"] = static_cast<int64_t>(outcome.changed);
    const std::optional<bool> active = selection.IsActive(manager);
    if (active)
        out["active"] = *active;
    else
        out["active"] = StateNode();
    return true;
}

StateNode BreakpointSelection::Groups(const BreakpointManager& manager)
{
    std::vector<std::string> names;
    std::vector<std::pair<int64_t, int64_t>> counts;  // count, enabled
    for (const auto& [id, bp] : manager.GetAllBreakpoints())
    {
        if (!bp || !BreakpointManager::IsUserBreakpoint(*bp))
            continue;
        const auto at = std::find(names.begin(), names.end(), bp->group);
        size_t index = static_cast<size_t>(at - names.begin());
        if (at == names.end())
        {
            names.push_back(bp->group);
            counts.emplace_back(0, 0);
        }
        counts[index].first++;
        counts[index].second += bp->active ? 1 : 0;
    }
    StateNode list = StateNode::Array();
    for (size_t i = 0; i < names.size(); i++)
    {
        StateNode group = StateNode::Object();
        group["name"] = names[i];
        group["count"] = counts[i].first;
        group["enabled"] = counts[i].second;
        group["active"] = counts[i].first == counts[i].second;
        list.push(std::move(group));
    }
    return list;
}
