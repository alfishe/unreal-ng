// Companion clients (docs/inprogress/2026-10-16-companion-client): the registry of paired devices - pairing approved at
// the GUI, the token delivered once, revocation, the store (digests only) and UNREAL_COMPANION_TOKENS

#include "stdafx.h"
#include "pch.h"

#include <algorithm>
#include <fstream>
#include <mutex>
#include <sstream>
#include <string>
#include <vector>

#include "3rdparty/message-center/messagecenter.h"
#include "_helpers/scratchfolder.h"
#include "_helpers/testwaithelper.h"
#include "emulator/execution/automationgate.h"
#include "emulator/execution/companionactions.h"
#include "emulator/execution/companionregistry.h"
#include "emulator/notifications.h"
#include "emulator/platform.h"

using companion::CompanionRegistry;
using namespace automation_gate;

class CompanionRegistry_Test : public ::testing::Test
{
protected:
    void SetUp() override
    {
        _registry.SetClockForTesting([this]() { return _now; });
        _registry.SetApprovalAvailable(true);
    }

    std::string Pair(const std::string& name, uint32_t scopes = 0, const std::string& peer = "192.168.1.40")
    {
        const companion::PairingRequestResult request = _registry.RequestPairing(name, "panel", scopes, peer);
        EXPECT_EQ(request.error, "") << request.message;
        EXPECT_TRUE(_registry.ApprovePairing(request.pairing.id).has_value());
        const companion::PairingPoll poll = _registry.PollPairing(request.pairing.id);
        EXPECT_EQ(poll.state, "approved");
        return poll.token;
    }

    int64_t _now = 1792160000000;
    CompanionRegistry _registry;
};

TEST_F(CompanionRegistry_Test, APairingIsApprovedAndTheTokenIsDeliveredOnce)
{
    const companion::PairingRequestResult request =
        _registry.RequestPairing("Desk panel", "panel, 15 keys", ScopeRun | ScopeInput, "192.168.1.40");
    ASSERT_EQ(request.error, "");
    EXPECT_EQ(request.code.size(), 6u);
    EXPECT_EQ(_registry.CodeOf(request.pairing.id), request.code);
    EXPECT_EQ(_registry.PollPairing(request.pairing.id).state, "pending");
    ASSERT_EQ(_registry.Pending().size(), 1u);
    EXPECT_EQ(_registry.Pending()[0].peer, "192.168.1.40");

    ASSERT_TRUE(_registry.ApprovePairing(request.pairing.id).has_value());
    const companion::PairingPoll poll = _registry.PollPairing(request.pairing.id);
    ASSERT_EQ(poll.state, "approved");
    EXPECT_EQ(poll.token.rfind("unc_", 0), 0u);
    EXPECT_EQ(poll.token.size(), 4u + 43u);
    EXPECT_EQ(poll.scopes, ScopeRun | ScopeInput);
    EXPECT_EQ(_registry.PollPairing(request.pairing.id).state, "unknown") << "the token is handed out once";

    const std::optional<companion::CompanionInfo> me = _registry.Authenticate(poll.token);
    ASSERT_TRUE(me.has_value());
    EXPECT_EQ(me->name, "Desk panel");
    EXPECT_EQ(me->id, poll.companionId);
    EXPECT_EQ(me->source, "paired");
    EXPECT_EQ(me->lastSeenMs, _now);
    EXPECT_FALSE(_registry.Authenticate(poll.token + "x").has_value());
    EXPECT_FALSE(_registry.Authenticate("").has_value());
}

TEST_F(CompanionRegistry_Test, ADeniedPairingSaysSoAndIssuesNothing)
{
    const companion::PairingRequestResult request = _registry.RequestPairing("Phone", "", 0, "10.0.0.2");
    ASSERT_EQ(request.error, "");
    EXPECT_TRUE(_registry.DenyPairing(request.pairing.id));
    EXPECT_EQ(_registry.PollPairing(request.pairing.id).state, "denied");
    EXPECT_FALSE(_registry.ApprovePairing(request.pairing.id).has_value()) << "a denied pairing cannot be approved";
    EXPECT_TRUE(_registry.List().empty());
}

TEST_F(CompanionRegistry_Test, APairingExpiresAfterItsTime)
{
    const companion::PairingRequestResult request = _registry.RequestPairing("Phone", "", 0, "10.0.0.2");
    _now += 119 * 1000;
    EXPECT_EQ(_registry.PollPairing(request.pairing.id).state, "pending");
    EXPECT_EQ(_registry.PollPairing(request.pairing.id).expiresIn, 1);
    _now += 2 * 1000;
    EXPECT_EQ(_registry.PollPairing(request.pairing.id).state, "expired");
    EXPECT_FALSE(_registry.ApprovePairing(request.pairing.id).has_value());
    EXPECT_TRUE(_registry.Pending().empty());
    _now += 61 * 1000;
    EXPECT_EQ(_registry.PollPairing(request.pairing.id).state, "unknown") << "a finished pairing is forgotten";
}

TEST_F(CompanionRegistry_Test, PairingIsRefusedWithoutAnApprover)
{
    _registry.SetApprovalAvailable(false);
    EXPECT_EQ(_registry.RequestPairing("Phone", "", 0, "10.0.0.2").error, "pairing_unavailable");
    EXPECT_EQ(_registry.RequestPairing("", "", 0, "10.0.0.2").error, "bad_request");
}

TEST_F(CompanionRegistry_Test, PendingPairingsAreLimitedInTotalAndPerPeer)
{
    EXPECT_EQ(_registry.RequestPairing("a", "", 0, "10.0.0.1").error, "");
    EXPECT_EQ(_registry.RequestPairing("b", "", 0, "10.0.0.1").error, "too_many_pairings") << "one per peer";
    EXPECT_EQ(_registry.RequestPairing("b", "", 0, "10.0.0.2").error, "");
    EXPECT_EQ(_registry.RequestPairing("c", "", 0, "10.0.0.3").error, "");
    EXPECT_EQ(_registry.RequestPairing("d", "", 0, "10.0.0.4").error, "");
    EXPECT_EQ(_registry.RequestPairing("e", "", 0, "10.0.0.5").error, "too_many_pairings") << "four at most";
}

TEST_F(CompanionRegistry_Test, ApprovalMayNarrowTheScopes)
{
    const companion::PairingRequestResult request = _registry.RequestPairing("Phone", "", 0, "10.0.0.2");
    EXPECT_EQ(request.pairing.scopes, kCompanionAllScopes) << "no scopes asked: every one (the user may untick some)";
    ASSERT_TRUE(_registry.ApprovePairing(request.pairing.id, ScopeRun).has_value());
    EXPECT_EQ(_registry.PollPairing(request.pairing.id).scopes, uint32_t(ScopeRun));
    ASSERT_EQ(_registry.List().size(), 1u);
    EXPECT_EQ(_registry.List()[0].scopes, uint32_t(ScopeRun));
}

TEST_F(CompanionRegistry_Test, ARevokedTokenNoLongerAuthenticates)
{
    const std::string a = Pair("Desk panel", 0, "10.0.0.1");
    const std::string b = Pair("Phone", 0, "10.0.0.2");
    const std::string c = Pair("Tablet", 0, "10.0.0.3");
    const std::string idOfA = _registry.Authenticate(a)->id;
    EXPECT_EQ(_registry.Revoke(idOfA), 1u);
    EXPECT_FALSE(_registry.Authenticate(a).has_value());
    EXPECT_TRUE(_registry.Authenticate(b).has_value()) << "the others stay";
    EXPECT_EQ(_registry.Revoke("Phone"), 1u) << "by name";
    EXPECT_FALSE(_registry.Authenticate(b).has_value());
    EXPECT_TRUE(_registry.Authenticate(c).has_value());
    EXPECT_EQ(_registry.Revoke("nobody"), 0u);
    EXPECT_EQ(_registry.Revoke("all"), 1u);
    EXPECT_FALSE(_registry.Authenticate(c).has_value());
}

TEST_F(CompanionRegistry_Test, TheStoreKeepsDigestsAndSurvivesARestart)
{
    ScratchFolder folder("companion-store");
    const std::string path = FileHelper::FromFsPath(folder.Path() / "config" / "companions.json");
    _registry.SetStorePath(path);
    const std::string token = Pair("Desk \"panel\"", ScopeRun | ScopeMedia);
    _registry.Authenticate(token);
    _registry.Flush();

    std::stringstream text;
    {
        // Closed again: the stores below replace the file, which Windows refuses while it is open
        std::ifstream in(FileHelper::ToFsPath(path), std::ios::binary);
        text << in.rdbuf();
    }
    EXPECT_EQ(text.str().find(token), std::string::npos) << "only the digest is stored";
    EXPECT_NE(text.str().find(CompanionRegistry::HashToken(token)), std::string::npos);

    CompanionRegistry restarted;
    restarted.SetStorePath(path);
    const std::optional<companion::CompanionInfo> me = restarted.Authenticate(token);
    ASSERT_TRUE(me.has_value());
    EXPECT_EQ(me->name, "Desk \"panel\"");
    EXPECT_EQ(me->scopes, ScopeRun | ScopeMedia);
    EXPECT_EQ(me->source, "paired");
    EXPECT_EQ(me->pairedMs, _now);

    restarted.Revoke(me->id);
    CompanionRegistry again;
    again.SetStorePath(path);
    EXPECT_TRUE(again.List().empty()) << "a revoke is stored";
}

TEST_F(CompanionRegistry_Test, ABrokenStoreIsSetAsideAndTheRegistryStartsEmpty)
{
    ScratchFolder folder("companion-store-broken");
    const std::filesystem::path path = folder.File("companions.json", "{ \"version\": 1, \"companions\": [ {");
    _registry.SetStorePath(FileHelper::FromFsPath(path));
    EXPECT_TRUE(_registry.List().empty());
    std::filesystem::path bad = path;
    bad += ".bad";
    EXPECT_TRUE(std::filesystem::exists(bad));
}

TEST_F(CompanionRegistry_Test, TheEnvironmentProvisionsCompanionsWithScopes)
{
    const size_t added = _registry.AddFromSpec(
        " wall:unc_wall-secret-0123456789 ; panel:unc_panel-secret-0123456789:run,input;"
        "everything:unc_every-secret-0123456789:all;short:abc;bad:unc_bad-scope-0123456789:fly;"
        "wall:unc_other-secret-0123456789;dup:unc_wall-secret-0123456789");
    EXPECT_EQ(added, 3u) << "a short token, an unknown scope, a repeated name and a repeated token are skipped";
    const std::optional<companion::CompanionInfo> wall = _registry.Authenticate("unc_wall-secret-0123456789");
    ASSERT_TRUE(wall.has_value());
    EXPECT_EQ(wall->scopes, kCompanionDefaultScopes);
    EXPECT_EQ(wall->source, "environment");
    EXPECT_EQ(_registry.Authenticate("unc_panel-secret-0123456789")->scopes, ScopeRun | ScopeInput);
    EXPECT_EQ(_registry.Authenticate("unc_every-secret-0123456789")->scopes, kCompanionAllScopes);
    EXPECT_FALSE(_registry.Authenticate("abc").has_value());

    ScratchFolder folder("companion-store-env");
    const std::string path = FileHelper::FromFsPath(folder.Path() / "companions.json");
    _registry.SetStorePath(path);
    Pair("Phone");
    CompanionRegistry restarted;
    restarted.SetStorePath(path);
    EXPECT_EQ(restarted.List().size(), 1u) << "environment companions are never stored";
}

TEST_F(CompanionRegistry_Test, EveryChangeIsAnnounced)
{
    MessageCenter& messageCenter = MessageCenter::DefaultMessageCenter();
    std::mutex eventsMutex;
    std::vector<std::string> events;
    const uint64_t observer = messageCenter.AddObserver(NC_COMPANION_CHANGED, [&](int, Message* message) {
        auto* payload = message ? dynamic_cast<CompanionChangedPayload*>(message->obj) : nullptr;
        if (payload && payload->name.rfind("announce-", 0) == 0)
        {
            std::lock_guard<std::mutex> lock(eventsMutex);
            events.push_back(payload->event + ":" + payload->name);
        }
    });
    struct ObserverGuard
    {
        MessageCenter& center;
        uint64_t id;
        ~ObserverGuard() { center.RemoveObserverById(NC_COMPANION_CHANGED, id); }
    } observerGuard{messageCenter, observer};

    const auto a = _registry.RequestPairing("announce-a", "", 0, "10.0.0.1");
    _registry.ApprovePairing(a.pairing.id);
    const auto b = _registry.RequestPairing("announce-b", "", 0, "10.0.0.2");
    _registry.DenyPairing(b.pairing.id);
    const auto c = _registry.RequestPairing("announce-c", "", 0, "10.0.0.3");
    _registry.CancelPairing(c.pairing.id);
    const auto d = _registry.RequestPairing("announce-d", "", 0, "10.0.0.4");
    _now += 121 * 1000;
    _registry.Pending();
    _registry.PollPairing(d.pairing.id);
    _registry.Revoke("announce-a");

    const std::vector<std::string> expected{
        "pairing_requested:announce-a", "paired:announce-a",           "pairing_requested:announce-b",
        "denied:announce-b",            "pairing_requested:announce-c", "pairing_cancelled:announce-c",
        "pairing_requested:announce-d", "pairing_expired:announce-d",   "revoked:announce-a"};
    ASSERT_TRUE(TestWait::For([&]() {
        std::lock_guard<std::mutex> lock(eventsMutex);
        return events.size() >= expected.size();
    }));
    std::lock_guard<std::mutex> lock(eventsMutex);
    EXPECT_EQ(events, expected);
}

TEST_F(CompanionRegistry_Test, NamesAreCleanedAndScopeNamesRoundTrip)
{
    EXPECT_EQ(CompanionRegistry::CleanName("  Desk\tpanel\n "), "Deskpanel");
    EXPECT_EQ(CompanionRegistry::CleanName(std::string(100, 'x')).size(), CompanionRegistry::kMaxNameLength);
    const std::string cyrillic = std::string(63, 'x') + "\xD0\x96";  // a two-byte character across the limit
    EXPECT_EQ(CompanionRegistry::CleanName(cyrillic), std::string(63, 'x'));

    EXPECT_EQ(CompanionScopeNames(kCompanionAllScopes),
              (std::vector<std::string>{"run", "input", "media", "machine", "rewind", "takeback", "debug"}));
    for (const std::string& name : CompanionScopeNames(kCompanionAllScopes))
        EXPECT_EQ(CompanionScopeName(CompanionScopeFromName(name)), name);
    EXPECT_EQ(CompanionScopeFromName("all"), kCompanionAllScopes);
    EXPECT_EQ(CompanionScopeFromName("fly"), 0u);
    EXPECT_FALSE(CompanionRegistry::ScopesFromNames({"run", "fly"}).has_value());
    EXPECT_EQ(*CompanionRegistry::ScopesFromNames({"run", "media"}), ScopeRun | ScopeMedia);
}

// The gate's decision every surface asks (WebAPI, CLI): a granted scope passes while nothing holds the machine; a
// missing scope is named; a held session refuses and names the way back; stepping needs a pause
TEST(CompanionCheckAction_Test, ScopeSessionAndPauseDecide)
{
    companion::CompanionInfo me;
    me.scopes = ScopeRun | ScopeDebug | ScopeTakeBack;
    EXPECT_TRUE(companion::CheckAction(me, ScopeRun, false, "", "take-back").allowed);
    EXPECT_TRUE(companion::CheckAction(me, ScopeDebug, false, "", "take-back").allowed) << "debugger actions too";

    const companion::ActionCheck media = companion::CheckAction(me, ScopeMedia, false, "", "take-back");
    EXPECT_EQ(media.error, "companion_forbidden");
    EXPECT_EQ(media.required, uint32_t(ScopeMedia));
    EXPECT_NE(media.message.find("'media'"), std::string::npos) << media.message;

    const companion::ActionCheck held = companion::CheckAction(me, ScopeRun, true, "mcp (MCP agent)", "'companion take-back'");
    EXPECT_EQ(held.error, "held_by_session");
    EXPECT_NE(held.message.find("mcp (MCP agent) controls the emulator; 'companion take-back' takes control back"),
              std::string::npos)
        << held.message;
    EXPECT_EQ(companion::CheckAction(me, ScopeMedia, true, "x", "y").error, "companion_forbidden")
        << "the scope is checked first: a held session does not hide a missing grant";

    EXPECT_EQ(companion::CheckAction(me, ScopeDebug, false, "", "y", true, false).error, "not_now")
        << "a step while the machine runs";
    EXPECT_TRUE(companion::CheckAction(me, ScopeDebug, false, "", "y", true, true).allowed) << "paused: it steps";
    EXPECT_EQ(companion::CheckAction(me, ScopeDebug, true, "x", "y", true, false).error, "held_by_session")
        << "a session comes before the pause";

    me.scopes = ScopeRun;
    EXPECT_NE(companion::CheckAction(me, ScopeRun, true, "x", "y").message.find("may not take control back"),
              std::string::npos);
}

// What a companion may do now (companionactions.h): the panel greys out the rest, each with its reason
TEST(CompanionActions_Test, AvailabilityFollowsTheMachinesState)
{
    companion::CompanionInfo me;
    me.scopes = kCompanionAllScopes & ~ScopeMedia;
    auto available = [](const StateNode& report, const char* action) {
        const StateNode* node = report.find(action);
        return node && node->find("available") && node->find("available")->b;
    };
    auto reason = [](const StateNode& report, const char* action) {
        const StateNode* node = report.find(action);
        const StateNode* r = node ? node->find("reason") : nullptr;
        return r ? r->s : std::string();
    };

    companion::MachineView running;
    running.running = true;
    StateNode report = companion::ActionsReport(me, running);
    EXPECT_EQ(report.size(), companion::Actions().size());
    EXPECT_TRUE(available(report, "pause"));
    EXPECT_FALSE(available(report, "resume"));
    EXPECT_FALSE(available(report, "step"));
    EXPECT_EQ(reason(report, "step"), "the machine runs: pause first");
    EXPECT_TRUE(available(report, "memory")) << "memory needs no pause";
    EXPECT_TRUE(available(report, "model"));
    EXPECT_EQ(reason(report, "disk"), "the user did not grant 'media'");
    EXPECT_FALSE(available(report, "rewind")) << "no recorded history";
    EXPECT_FALSE(available(report, "take_back")) << "no session";

    companion::MachineView paused;
    paused.paused = true;
    paused.history = true;
    report = companion::ActionsReport(me, paused);
    EXPECT_TRUE(available(report, "resume"));
    EXPECT_TRUE(available(report, "step"));
    EXPECT_TRUE(available(report, "registers"));
    EXPECT_TRUE(available(report, "rewind"));
    EXPECT_FALSE(available(report, "pause"));

    companion::MachineView held = running;
    held.claimed = true;
    held.holder = "MCP agent";
    held.cooldowns = true;
    report = companion::ActionsReport(me, held);
    EXPECT_EQ(reason(report, "reset"), "MCP agent controls the emulator");
    EXPECT_TRUE(available(report, "take_back"));
    EXPECT_TRUE(available(report, "allow_again"));
    EXPECT_TRUE(available(report, "pause")) << "a pause is free during a session, as for everybody";

    report = companion::ActionsReport(me, companion::MachineView{});
    EXPECT_EQ(reason(report, "reset"), "the machine is stopped");
}

TEST_F(CompanionRegistry_Test, PairingTurnedOffRefusesRequestsAndDeniesTheWaitingOnes)
{
    std::vector<std::string> events;
    std::mutex eventsMutex;
    MessageCenter& center = MessageCenter::DefaultMessageCenter();
    const uint64_t observer = center.AddObserver(NC_COMPANION_CHANGED, [&](int, Message* message) {
        if (auto* payload = message ? dynamic_cast<CompanionChangedPayload*>(message->obj) : nullptr)
        {
            std::lock_guard<std::mutex> lock(eventsMutex);
            events.push_back(payload->event);
        }
    });
    const std::string kept = Pair("Desk panel");
    const companion::PairingRequestResult waiting = _registry.RequestPairing("Phone", "", 0, "192.168.1.41");
    ASSERT_EQ(waiting.error, "");
    EXPECT_TRUE(_registry.IsPairingAllowed()) << "on by default";

    _registry.SetPairingAllowed(false);
    EXPECT_EQ(_registry.PollPairing(waiting.pairing.id).state, "denied");
    const companion::PairingRequestResult refused = _registry.RequestPairing("Tablet", "", 0, "192.168.1.42");
    EXPECT_EQ(refused.error, "pairing_disabled");
    {
        const StateNode report = _registry.Report();
        const StateNode* allowed = report.find("pairing_allowed");
        ASSERT_NE(allowed, nullptr);
        EXPECT_FALSE(allowed->b);
    }
    EXPECT_TRUE(_registry.Authenticate(kept).has_value()) << "paired companions keep working";
    EXPECT_TRUE(TestWait::For(
        [&] {
            std::lock_guard<std::mutex> lock(eventsMutex);
            return std::find(events.begin(), events.end(), "denied") != events.end();
        },
        std::chrono::seconds(2)))
        << "the prompt of the waiting request is told";
    center.RemoveObserverById(NC_COMPANION_CHANGED, observer);

    _registry.SetPairingAllowed(true);
    EXPECT_EQ(_registry.RequestPairing("Tablet", "", 0, "192.168.1.42").error, "");
}
