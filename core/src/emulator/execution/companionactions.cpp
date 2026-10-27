#include "stdafx.h"

#include "companionactions.h"

#include "debugger/ttd/timetravelhooks.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/execution/automationgate.h"
#include "emulator/execution/companionregistry.h"

namespace companion
{
namespace
{
/// What an action needs of the machine's state besides a scope
enum class Needs
{
    Nothing,
    Running,
    Paused,
    History,
    Session,
    Cooldown,
    SpeedFree,  ///< the host speed may change (no TTD history recorded at 1x holds it)
};

struct Entry
{
    ActionInfo info;
    Needs needs;
};

const std::vector<Entry>& Catalogue()
{
    using namespace automation_gate;
    static const std::vector<Entry> catalogue = {
        {{"pause", 0, "POST /pause"}, Needs::Running},
        {{"resume", ScopeRun, "POST /resume"}, Needs::Paused},
        {{"reset", ScopeRun, "POST /reset"}, Needs::Nothing},
        {{"nmi", ScopeRun, "POST /nmi"}, Needs::Nothing},
        {{"turbo", ScopeRun, "POST /settings/turbo_mode"}, Needs::Nothing},
        {{"speed", ScopeRun, "POST /settings/speed"}, Needs::SpeedFree},
        {{"keyboard", ScopeInput, "POST /keyboard/*"}, Needs::Nothing},
        {{"joystick", ScopeInput, "POST /joystick/*"}, Needs::Nothing},
        {{"mouse", ScopeInput, "POST /mouse/*"}, Needs::Nothing},
        {{"tape", ScopeMedia, "POST /tape/*"}, Needs::Nothing},
        {{"disk", ScopeMedia, "POST /disk/{drive}/insert | eject | create"}, Needs::Nothing},
        {{"snapshot_load", ScopeMedia, "POST /snapshot/load"}, Needs::Nothing},
        {{"snapshot_save", 0, "POST /snapshot/save"}, Needs::Nothing},
        {{"rzx", ScopeMedia, "POST /rzx/play | stop | seek"}, Needs::Nothing},
        {{"rewind", ScopeRewind, "POST /ttd/seek | step-back | step-forward | resume"}, Needs::History},
        {{"model", ScopeMachine, "POST /model"}, Needs::Nothing},
        {{"configuration", ScopeMachine, "POST /settings/*, /feature/*, /slots/*, /switches, /network/config"},
         Needs::Nothing},
        {{"step", ScopeDebug, "POST /step | steps"}, Needs::Paused},
        {{"step_over", ScopeDebug, "POST /stepover"}, Needs::Paused},
        {{"step_out", ScopeDebug, "POST /stepout"}, Needs::Paused},
        {{"run_to", ScopeDebug, "POST /skip_until, /run_*"}, Needs::Paused},
        {{"registers", ScopeDebug, "PUT /registers/{name}"}, Needs::Paused},
        {{"memory", ScopeDebug, "POST /memory/write, PUT /memory/*"}, Needs::Nothing},
        {{"breakpoints", ScopeDebug, "POST | PUT | DELETE /breakpoints*"}, Needs::Nothing},
        {{"debug_mode", ScopeDebug, "PUT /debugmode"}, Needs::Nothing},
        {{"take_back", ScopeTakeBack, "POST /control/take-back"}, Needs::Session},
        {{"allow_again", ScopeTakeBack, "POST /control/allow-again"}, Needs::Cooldown},
    };
    return catalogue;
}
}  // namespace

const std::vector<ActionInfo>& Actions()
{
    static const std::vector<ActionInfo> actions = [] {
        std::vector<ActionInfo> list;
        for (const Entry& e : Catalogue())
            list.push_back(e.info);
        return list;
    }();
    return actions;
}

MachineView ViewOf(Emulator& emulator)
{
    MachineView view;
    const EmulatorStateEnum state = emulator.GetState();
    view.paused = state == StatePaused;
    view.running = state == StateRun || state == StateResumed;
    if (EmulatorContext* context = emulator.GetContext())
    {
        const EmulatorContext::RunControlState control = context->GetRunControlState();
        view.claimed = control.claimed;
        view.holder = control.client.empty() ? control.surfaceLabel : control.client;
        view.cooldowns = !control.cooldowns.empty();
        view.history = context->pTimeTravelHooks && context->pTimeTravelHooks->HasHistory();
        view.speedLock = emulator.SpeedLockReason();
    }
    return view;
}

StateNode ActionsReport(const CompanionInfo& companion, const MachineView& view)
{
    StateNode report = StateNode::Object();
    for (const Entry& e : Catalogue())
    {
        std::string reason;
        if (!view.running && !view.paused)
            reason = "the machine is stopped";
        else if (e.info.scope && !(companion.scopes & e.info.scope))
            reason = "the user did not grant '" + automation_gate::CompanionScopeName(e.info.scope) + "'";
        else if (view.claimed && e.info.scope && e.info.scope != automation_gate::ScopeTakeBack)
            reason = view.holder + " controls the emulator";
        else
            switch (e.needs)
            {
                case Needs::Running:
                    if (!view.running)
                        reason = "the machine is paused";
                    break;
                case Needs::Paused:
                    if (!view.paused)
                        reason = std::string(e.info.name) == "resume" ? "the machine runs already"
                                                                      : "the machine runs: pause first";
                    break;
                case Needs::History:
                    if (!view.history)
                        reason = "no recorded history: start time travel first";
                    break;
                case Needs::Session:
                    if (!view.claimed)
                        reason = "no session holds the machine";
                    break;
                case Needs::Cooldown:
                    if (!view.cooldowns)
                        reason = "no client waits in a re-take cooldown";
                    break;
                case Needs::SpeedFree:
                    reason = view.speedLock;
                    break;
                case Needs::Nothing:
                    break;
            }
        StateNode node = StateNode::Object();
        node["available"] = reason.empty();
        node["scope"] = e.info.scope ? StateNode(automation_gate::CompanionScopeName(e.info.scope)) : StateNode();
        if (!reason.empty())
            node["reason"] = reason;
        report[e.info.name] = std::move(node);
    }
    return report;
}
}  // namespace companion
