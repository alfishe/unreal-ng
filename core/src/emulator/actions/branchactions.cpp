#include "emulator/actions/branchactions.h"

#include <algorithm>
#include <chrono>
#include <map>
#include <mutex>
#include <utility>

#include "debugger/ttd/timetravelhooks.h"
#include "debugger/ttd/ttdcontrol.h"
#include "debugger/ttd/ttdsessiontypes.h"
#include "emulator/actions/actionparams.h"
#include "emulator/actions/actionregistry.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/execution/automationgate.h"

namespace actions
{
namespace
{
using automation_gate::ScopeRewind;
using ttd::kTTDNoBranch;
using ttd::kTTDTrunk;
using ttd::TTDBranchesPublished;
using ttd::TTDBranchesView;
using ttd::TTDBranchInfo;

/// The most items ttd.branch_list carries (a surface shows a page of them; the full list is the branches verb)
constexpr size_t kListMax = 100;

uint64_t NowHostMicros()
{
    return static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::system_clock::now().time_since_epoch())
            .count());
}

ActionResult FromTtd(ttd::TTDReply reply)
{
    if (reply.Ok())
        return ActionResult::Success(std::move(reply.body), std::move(reply.message));
    return ActionResult::Failure(ttd::TTDControlErrorPhrase(reply.error), std::move(reply.message),
                                 std::move(reply.body));
}

ttd::TTDReply Ttd(const ActionCall& call, const std::string& verb, std::map<std::string, std::string> options)
{
    return ttd::TTDControl(call.emulator->GetContext()).Execute({verb, std::move(options)});
}

/// The published branch table; false without time travel
bool Published(Emulator& emulator, TTDBranchesPublished& out)
{
    EmulatorContext* context = emulator.GetContext();
    if (context == nullptr || context->pTimeTravelHooks == nullptr)
        return false;
    out = context->pTimeTravelHooks->PublishedBranches();
    return true;
}

std::string NoTtd(Emulator& emulator, const ActionValue&)
{
    EmulatorContext* context = emulator.GetContext();
    return context != nullptr && context->pTimeTravelHooks != nullptr ? std::string()
                                                                       : std::string("time travel is not available");
}

std::string NoBranches(Emulator& emulator, const ActionValue& value)
{
    if (std::string why = NoTtd(emulator, value); !why.empty())
        return why;
    TTDBranchesPublished published;
    Published(emulator, published);
    return published.view != nullptr && published.view->branches.size() > 1 ? std::string()
                                                                             : std::string("the history has one branch");
}

std::string NoPrevious(Emulator& emulator, const ActionValue& value)
{
    if (std::string why = NoTtd(emulator, value); !why.empty())
        return why;
    TTDBranchesPublished published;
    Published(emulator, published);
    return published.previous != kTTDNoBranch ? std::string() : std::string("no branch was left before this one");
}

/// The timeline's last frame for the fork points: the published cursor's end
uint64_t TimelineEnd(Emulator& emulator)
{
    const ttd::TTDCursor cursor = emulator.GetContext()->pTimeTravelHooks->PublishedCursor();
    return std::max(cursor.end, cursor.first + 1);
}

bool OnLineage(const TTDBranchesView& view, uint32_t id)
{
    return std::any_of(view.lineage.begin(), view.lineage.end(),
                       [id](const ttd::TTDLineageSpan& span) { return span.branch == id; });
}

/// The fork ticks a readout carries at most (a strip is a few hundred pixels wide)
constexpr size_t kMaxForkTicks = 64;

StateNode Ref(const TTDBranchesView* view, uint32_t id)
{
    if (id == kTTDNoBranch)
        return {};
    StateNode node = StateNode::Object();
    node["id"] = static_cast<uint64_t>(id);
    const TTDBranchInfo* b = view != nullptr ? view->Find(id) : nullptr;
    node["name"] = b != nullptr ? b->meta.name : std::string(id == kTTDTrunk ? "trunk" : "");
    return node;
}

StateNode BranchReadout(Emulator& emulator)
{
    StateNode node = StateNode::Object();
    TTDBranchesPublished published;
    if (!Published(emulator, published))
    {
        node["available"] = false;
        return node;
    }
    const TTDBranchesView* view = published.view.get();
    const TTDBranchInfo* active = view != nullptr ? view->Find(published.active) : nullptr;
    node["available"] = true;
    node["id"] = static_cast<uint64_t>(published.active);
    node["guid"] = active != nullptr ? ttd::TTDGuidToString(active->guid) : std::string();
    node["name"] = active != nullptr ? active->meta.name : std::string("trunk");
    node["count"] = static_cast<uint64_t>(view != nullptr ? view->branches.size() : 1);
    // Forks from the trunk to the active branch
    node["depth"] = static_cast<uint64_t>(view != nullptr && !view->lineage.empty() ? view->lineage.size() - 1 : 0);
    node["pinned"] = active != nullptr && active->meta.pinned;
    const uint64_t now = NowHostMicros();
    const uint64_t created = active != nullptr ? active->meta.createdHostMicros : 0;
    node["created_ago"] = created != 0 && now > created ? StateNode((now - created) / 1'000'000u) : StateNode();
    node["age"] = BranchAge(created, now);
    node["viewed"] = Ref(view, published.viewed);
    node["previous"] = Ref(view, published.previous);
    // The branches at the nearest fork, the active lineage's included (what the sibling keys cycle through)
    size_t self = 0;
    uint64_t at = 0;
    const uint64_t frame = emulator.GetContext()->pTimeTravelHooks->PublishedCursor().frame;
    node["siblings"] = static_cast<uint64_t>(
        ttd::TTDSiblingsAtNearestFork(view, TimelineEnd(emulator), frame, self, at).size());
    // The frames of the lineage where another future goes on (the deck strip's fork ticks), at most kMaxForkTicks
    StateNode forks = StateNode::Array();
    if (view != nullptr)
    {
        size_t n = 0;
        for (const auto& [forkFrame, leaving] : ttd::TTDForkPoints(*view, TimelineEnd(emulator)))
        {
            if (n++ == kMaxForkTicks)
                break;
            forks.push(StateNode(forkFrame));
        }
    }
    node["forks"] = std::move(forks);
    return node;
}

StateNode BranchListReadout(Emulator& emulator)
{
    StateNode node = StateNode::Object();
    TTDBranchesPublished published;
    if (!Published(emulator, published))
    {
        node["available"] = false;
        return node;
    }
    node["available"] = true;
    const TTDBranchesView* view = published.view.get();
    node["active"] = static_cast<uint64_t>(published.active);
    node["count"] = static_cast<uint64_t>(view != nullptr ? view->branches.size() : 1);
    StateNode items = StateNode::Array();
    const uint64_t now = NowHostMicros();
    if (view == nullptr)
    {
        StateNode trunk = StateNode::Object();
        trunk["id"] = static_cast<uint64_t>(kTTDTrunk);
        trunk["name"] = std::string("trunk");
        trunk["age"] = std::string();
        trunk["pinned"] = false;
        trunk["active"] = true;
        trunk["on_lineage"] = true;
        items.push(std::move(trunk));
        node["total"] = static_cast<uint64_t>(1);
        node["items"] = std::move(items);
        return node;
    }
    // The active lineage first, then the others; newest first in each
    std::vector<const TTDBranchInfo*> order;
    for (const TTDBranchInfo& b : view->branches)
        order.push_back(&b);
    std::stable_sort(order.begin(), order.end(), [view](const TTDBranchInfo* a, const TTDBranchInfo* b) {
        const bool la = OnLineage(*view, a->id);
        const bool lb = OnLineage(*view, b->id);
        if (la != lb)
            return la;
        if (a->meta.createdHostMicros != b->meta.createdHostMicros)
            return a->meta.createdHostMicros > b->meta.createdHostMicros;
        return a->id > b->id;
    });
    for (const TTDBranchInfo* b : order)
    {
        if (items.size() >= kListMax)
            break;
        StateNode item = StateNode::Object();
        item["id"] = static_cast<uint64_t>(b->id);
        item["name"] = b->meta.name.empty() && b->id == kTTDTrunk ? std::string("trunk") : b->meta.name;
        item["age"] = BranchAge(b->meta.createdHostMicros, now);
        item["pinned"] = b->meta.pinned;
        item["active"] = b->id == published.active;
        item["on_lineage"] = OnLineage(*view, b->id);
        item["length"] = b->Length();
        items.push(std::move(item));
    }
    node["total"] = static_cast<uint64_t>(view->branches.size());
    node["items"] = std::move(items);
    return node;
}

/// A held delete fires on its release after 1 s (as mach.reset): the press time per instance and holder
std::mutex& DeleteMutex()
{
    static std::mutex mutex;
    return mutex;
}

std::map<std::pair<std::string, std::string>, std::chrono::steady_clock::time_point>& DeletePresses()
{
    static std::map<std::pair<std::string, std::string>, std::chrono::steady_clock::time_point> presses;
    return presses;
}

ActionResult DeleteBranch(const ActionCall& call)
{
    const std::optional<std::string> branch = TextParam(call.value, "branch");
    if (!branch || branch->empty())
        return ActionResult::Failure("Bad Request", "branch: an id, a GUID or a name");
    std::string paramError;
    const bool atOnce = BoolParam(call.value, "now", paramError).value_or(false);
    if (!paramError.empty())
        return ActionResult::Failure("Bad Request", paramError);
    auto run = [&call, &branch]() {
        // Never force: a branch of the active lineage stays (the verb refuses it)
        return FromTtd(Ttd(call, "branch-delete", {{"branch", *branch}}));
    };
    if (atOnce)
        return call.value.pressed ? run() : ActionResult::Success();
    const auto key = std::make_pair(call.instanceId, call.origin.identity);
    const auto now = std::chrono::steady_clock::now();
    {
        std::lock_guard<std::mutex> lock(DeleteMutex());
        if (call.value.pressed)
        {
            DeletePresses()[key] = now;
            StateNode body = StateNode::Object();
            body["armed"] = true;
            return ActionResult::Success(std::move(body), "Hold for 1 s, then release to delete the branch");
        }
        auto it = DeletePresses().find(key);
        const bool longEnough = it != DeletePresses().end() && now - it->second >= std::chrono::seconds(1);
        if (it != DeletePresses().end())
            DeletePresses().erase(it);
        if (!longEnough)
            return ActionResult::Failure("not_now", "hold the delete for 1 s");
    }
    return run();
}

}  // namespace

std::string BranchAge(uint64_t createdHostMicros, uint64_t nowHostMicros)
{
    if (createdHostMicros == 0)
        return {};
    const uint64_t s = nowHostMicros > createdHostMicros ? (nowHostMicros - createdHostMicros) / 1'000'000u : 0;
    if (s < 60)
        return std::to_string(s) + "s";
    if (s < 3600)
        return std::to_string(s / 60) + "m";
    if (s < 86400)
        return std::to_string(s / 3600) + "h";
    return std::to_string(s / 86400) + "d";
}

const std::vector<std::string>& BranchReadoutNames()
{
    static const std::vector<std::string> names = {"ttd.branch", "ttd.branch_list"};
    return names;
}

StateNode ReadBranchReadout(Emulator& emulator, const std::string& name)
{
    if (name == "ttd.branch")
        return BranchReadout(emulator);
    if (name == "ttd.branch_list")
        return BranchListReadout(emulator);
    return {};
}

std::vector<RegistrationHandle> RegisterBranchActions(ActionRegistry& registry)
{
    std::vector<RegistrationHandle> handles;
    auto add = [&](const char* id, const char* label, ValueKind kind, const char* params, Executor run,
                   ActionRules rules, bool held = false) {
        handles.push_back(registry.Register({id, label, "Time travel", kind, held, params, {}, false}, std::move(run),
                                            std::move(rules)));
    };
    auto rules = [](uint32_t scope, std::function<std::string(Emulator&, const ActionValue&)> unavailable) {
        ActionRules r;
        r.scope = ActionRules::Fixed(scope);
        r.unavailable = std::move(unavailable);
        return r;
    };

    add("ttd.branch_prev", "Previous branch", ValueKind::Press, "",
        [](const ActionCall& call) { return FromTtd(Ttd(call, "branch-switch", {{"to", "previous"}})); },
        rules(ScopeRewind, NoPrevious));
    for (const bool next : {true, false})
        add(next ? "ttd.branch_next_sibling" : "ttd.branch_prev_sibling",
            next ? "Next branch at the fork" : "Previous branch at the fork", ValueKind::Press, "",
            [next](const ActionCall& call) {
                return FromTtd(Ttd(call, "branch-switch", {{"to", next ? "next_sibling" : "prev_sibling"}}));
            },
            rules(ScopeRewind, NoBranches));
    add("ttd.branch_switch", "Switch to a branch", ValueKind::Press, "branch, frame? + tinframe?",
        [](const ActionCall& call) {
            if (!TextParam(call.value, "branch"))
                return ActionResult::Failure("Bad Request", "branch: an id, a GUID or a name");
            return FromTtd(Ttd(call, "branch-switch", ParamOptions(call.value.params)));
        },
        rules(ScopeRewind, NoBranches));
    add("ttd.branch_menu", "Branches", ValueKind::Press, "",
        [](const ActionCall& call) { return ActionResult::Success(BranchListReadout(*call.emulator)); },
        rules(0, NoTtd));
    {
        ActionRules r = rules(ScopeRewind, NoBranches);
        r.state = [](Emulator& emulator, const ActionValue&) -> std::optional<bool> {
            TTDBranchesPublished published;
            if (!Published(emulator, published) || published.view == nullptr)
                return false;
            const TTDBranchInfo* active = published.view->Find(published.active);
            return active != nullptr && active->meta.pinned;
        };
        add("ttd.branch_pin", "Pin the branch", ValueKind::Press, "",
            [](const ActionCall& call) {
                TTDBranchesPublished published;
                Published(*call.emulator, published);
                const TTDBranchInfo* active =
                    published.view != nullptr ? published.view->Find(published.active) : nullptr;
                if (active == nullptr)
                    return ActionResult::Failure("not_now", "the history has one branch");
                return FromTtd(Ttd(call, "branch-update",
                                   {{"branch", std::to_string(active->id)},
                                    {"pinned", active->meta.pinned ? "false" : "true"}}));
            },
            std::move(r));
    }
    add("ttd.branch_delete", "Delete a branch (hold 1 s)", ValueKind::Press,
        "branch, now? (false: hold the control 1 s)", DeleteBranch, rules(ScopeRewind, NoBranches), true);
    return handles;
}

}  // namespace actions
