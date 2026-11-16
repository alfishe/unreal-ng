/// @file branchbindings_test.cpp
/// @brief T-A6 (TTD branches test-strategy.md §2): the shipped profiles' branch bindings (surfaces.md §3) reach the
/// branch actions: the debugger keyboard's Ctrl+Alt+B (previous branch) and Ctrl+Alt+Shift+B (the branch tree, a GUI
/// command), the ShuttlePRO's K14 held + K12 and the pad's view (back) + east, each routed to an action the core
/// registers.

#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "emulator/actions/actionregistry.h"
#include "profiles/profilestore.h"
#include "router/inputrouter.h"

using namespace controllers;

namespace
{
/// The binding of @p sources in @p layer of any scope (a chord's members in their order)
const Binding* FindBinding(const ResolvedProfiles& resolved, const std::string& layer,
                           const std::vector<std::string>& sources)
{
    for (const auto& [scope, bindings] : resolved.scopes)
        for (const Binding& binding : bindings)
            if (binding.layer == layer && binding.sources == sources)
                return &binding;
    return nullptr;
}
}  // namespace

TEST(BranchBindings_Test, TheKeyboardShortcutsRouteToTheBranchVerbs)
{
    ProfileStore store;
    ProfileContext context;
    context.machine = "pentagon";
    context.debugLayer = false;
    context.devices = {{"kbd:system", "Keyboard", "keyboard", "keyboard", "qt"}};
    InputRouter router;
    std::vector<std::string> fired;
    router.SetActionSink([&fired](const ActionEvent& event) {
        if (event.pressed)
            fired.push_back(event.action);
    });
    router.OnDeviceAdded(context.devices[0]);
    router.SetRole("kbd:system", DeviceRole::DebuggerRemote);
    const ResolvedProfiles resolved = store.Apply(router, context);
    ASSERT_TRUE(resolved.warnings.empty()) << resolved.warnings.front();

    router.OnControl({"kbd:system", "key.ctrl.alt.b", 1, 1});
    router.OnControl({"kbd:system", "key.ctrl.alt.b", 0, 2});
    router.OnControl({"kbd:system", "key.ctrl.alt.shift.b", 1, 3});
    router.OnControl({"kbd:system", "key.ctrl.alt.shift.b", 0, 4});
    router.OnControl({"kbd:system", "key.ctrl.b", 1, 5});
    EXPECT_EQ(fired, (std::vector<std::string>{"ttd.branch_prev", "unrealqt.ttd_branches", "ttd.bookmark_add"}))
        << "Ctrl+B stays the bookmark";
    EXPECT_TRUE(actions::ActionRegistry::Instance().Find("ttd.branch_prev").has_value());
}

TEST(BranchBindings_Test, TheShuttleProChordKeepsK12ReverseContinue)
{
    ProfileStore store;
    const HostDeviceInfo shuttle = {"usb:0b33:0030:0100@1", "ShuttlePRO v2", "shuttle-pro-v2", "generic", "fake"};
    InputRouter router;
    uint64_t now = 1'000'000'000;
    std::vector<std::string> fired;
    router.SetClock([&now] { return now; });
    router.SetActionSink([&fired](const ActionEvent& event) {
        if (event.pressed)
            fired.push_back(event.action);
    });
    router.SetDefaultRole(shuttle.id, DeviceRole::DebuggerRemote);
    router.OnDeviceAdded(shuttle);
    router.SetFocus("emu-1");
    ProfileContext context;
    context.debugLayer = false;
    context.devices = {shuttle};
    const ResolvedProfiles resolved = store.Apply(router, context);
    ASSERT_TRUE(resolved.warnings.empty()) << resolved.warnings.front();
    auto control = [&](const char* name, float value) {
        now += 25'000'000;
        router.OnControl({shuttle.id, name, value, now});
    };

    control("button.14", 1);
    control("button.12", 1);
    control("button.12", 0);
    control("button.14", 0);
    ASSERT_EQ(fired, (std::vector<std::string>{"ttd.branch_prev"})) << "K14 (follow PC) does not fire in the chord";
    fired.clear();
    control("button.12", 1);
    control("button.12", 0);
    EXPECT_EQ(fired, (std::vector<std::string>{"ttd.reverse_continue"}));
}

TEST(BranchBindings_Test, ThePadsBackAndEastIsThePreviousBranch)
{
    ProfileStore store;
    ProfileContext context;
    context.machine = "pentagon";
    context.debugLayer = true;
    context.devices = {{"usb:045e:0b00:1", "Elite", "xbox-elite-2", "xbox", "fake"}};
    const ResolvedProfiles resolved = store.Resolve(context);
    const Binding* back = FindBinding(resolved, "debug", {"view", "east"});
    ASSERT_NE(back, nullptr);
    EXPECT_TRUE(back->chord);
    EXPECT_EQ(back->target.name, "ttd.branch_prev");
    const Binding* east = FindBinding(resolved, "debug", {"east"});
    ASSERT_NE(east, nullptr);
    EXPECT_EQ(east->target.name, "ttd.resume_live") << "east alone stays resume";
}
