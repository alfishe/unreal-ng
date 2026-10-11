// Tests for the run-control claim token in EmulatorContext.
// Sprint 0, Item 0.2 — GDB TDD §3.3 / parent TDD §7.2.
//
// The claim is an advisory per-instance owner token. While a surface holds the
// claim with the target paused, other surfaces' run-affecting operations are
// refused. Sprint 0 ships the mechanism only; enforcement at call sites lands
// in Phase 2 (TTD seek) and G1 (GDB stub). These tests cover just the token.

#include "pch.h"

#include <emulator/emulatorcontext.h>
#include <common/uuid.h>

#include <string>
#include <thread>
#include <vector>

/// region <Fixture>

class RunControlClaim_Test : public ::testing::Test
{
protected:
    void SetUp() override
    {
        _ctx = new EmulatorContext(LoggerLevel::LogError);
        ASSERT_NE(_ctx, nullptr);

        _ownerA = UUID::Generate();
        _ownerB = UUID::Generate();
        ASSERT_FALSE(_ownerA == UUID());
        ASSERT_FALSE(_ownerB == UUID());
        ASSERT_FALSE(_ownerA == _ownerB);
    }

    void TearDown() override
    {
        delete _ctx;
        _ctx = nullptr;
    }

    EmulatorContext* _ctx = nullptr;
    UUID _ownerA;
    UUID _ownerB;
};

/// endregion </Fixture>

/// region <Initial state>

TEST_F(RunControlClaim_Test, FreshContextIsUnclaimed)
{
    EXPECT_FALSE(_ctx->IsRunControlClaimed());
    EXPECT_FALSE(_ctx->HasRunControl(_ownerA));
    EXPECT_FALSE(_ctx->HasRunControl(_ownerB));
}

TEST_F(RunControlClaim_Test, FreshStateSnapshotIsEmpty)
{
    auto state = _ctx->GetRunControlState();
    EXPECT_FALSE(state.claimed);
    EXPECT_TRUE(state.surfaceLabel.empty());
    EXPECT_TRUE(state.ownerUuid.empty());
}

/// endregion </Initial state>

/// region <Take / idempotent re-take>

TEST_F(RunControlClaim_Test, FirstTakeSucceedsAndReportsClaimed)
{
    EXPECT_TRUE(_ctx->TakeRunControl(_ownerA, "gdb"));
    EXPECT_TRUE(_ctx->IsRunControlClaimed());
    EXPECT_TRUE(_ctx->HasRunControl(_ownerA));
    EXPECT_FALSE(_ctx->HasRunControl(_ownerB));
}

TEST_F(RunControlClaim_Test, SameOwnerCanReTakeIdempotently)
{
    ASSERT_TRUE(_ctx->TakeRunControl(_ownerA, "gdb"));
    // Re-take by the same owner must succeed and refresh the label.
    EXPECT_TRUE(_ctx->TakeRunControl(_ownerA, "gdb-v2"));
    auto state = _ctx->GetRunControlState();
    EXPECT_TRUE(state.claimed);
    EXPECT_EQ(state.surfaceLabel, "gdb-v2");
}

TEST_F(RunControlClaim_Test, StateSnapshotReflectsHolder)
{
    ASSERT_TRUE(_ctx->TakeRunControl(_ownerA, "webapi"));
    auto state = _ctx->GetRunControlState();
    EXPECT_TRUE(state.claimed);
    EXPECT_EQ(state.surfaceLabel, "webapi");
    EXPECT_EQ(state.ownerUuid, _ownerA.toString());
}

/// endregion </Take / idempotent re-take>

/// region <Contested take is refused>

TEST_F(RunControlClaim_Test, DifferentOwnerIsRefusedWithErrorReason)
{
    ASSERT_TRUE(_ctx->TakeRunControl(_ownerA, "gdb"));

    std::string reason;
    EXPECT_FALSE(_ctx->TakeRunControl(_ownerB, "webapi", &reason));
    EXPECT_FALSE(reason.empty()) << "Refusal must populate errorReason";
    EXPECT_NE(reason.find("gdb"), std::string::npos)
        << "Reason should mention the current holder's label";

    // State unchanged: still owner A / 'gdb'.
    auto state = _ctx->GetRunControlState();
    EXPECT_EQ(state.surfaceLabel, "gdb");
    EXPECT_EQ(state.ownerUuid, _ownerA.toString());
}

TEST_F(RunControlClaim_Test, DifferentOwnerRefusedEvenWithNullErrorReason)
{
    ASSERT_TRUE(_ctx->TakeRunControl(_ownerA, "gdb"));
    // Should not crash, should just return false.
    EXPECT_FALSE(_ctx->TakeRunControl(_ownerB, "webapi", nullptr));
}

/// endregion </Contested take is refused>

/// region <Release semantics>

TEST_F(RunControlClaim_Test, ReleaseByOwnerClearsClaim)
{
    ASSERT_TRUE(_ctx->TakeRunControl(_ownerA, "gdb"));
    _ctx->ReleaseRunControl(_ownerA);
    EXPECT_FALSE(_ctx->IsRunControlClaimed());
    EXPECT_FALSE(_ctx->HasRunControl(_ownerA));
}

TEST_F(RunControlClaim_Test, ReleaseByNonOwnerIsNoOp)
{
    ASSERT_TRUE(_ctx->TakeRunControl(_ownerA, "gdb"));
    _ctx->ReleaseRunControl(_ownerB);
    EXPECT_TRUE(_ctx->IsRunControlClaimed()) << "Release by non-owner must be a no-op";
    EXPECT_TRUE(_ctx->HasRunControl(_ownerA));
}

TEST_F(RunControlClaim_Test, CanReTakeAfterRelease)
{
    ASSERT_TRUE(_ctx->TakeRunControl(_ownerA, "gdb"));
    _ctx->ReleaseRunControl(_ownerA);
    EXPECT_TRUE(_ctx->TakeRunControl(_ownerB, "webapi"));
    EXPECT_TRUE(_ctx->HasRunControl(_ownerB));
    EXPECT_FALSE(_ctx->HasRunControl(_ownerA));
}

TEST_F(RunControlClaim_Test, ReleaseOnUnclaimedIsSafe)
{
    // Defensive: surfaces can call Release unconditionally at shutdown.
    _ctx->ReleaseRunControl(_ownerA);
    _ctx->ReleaseRunControl(_ownerB);
    EXPECT_FALSE(_ctx->IsRunControlClaimed());
}

/// endregion </Release semantics>

/// region <Nil owner UUID is rejected>

TEST_F(RunControlClaim_Test, NilOwnerUUIDIsRejected)
{
    UUID nil;  // default-constructed = all zeros
    std::string reason;
    EXPECT_FALSE(_ctx->TakeRunControl(nil, "broken-surface", &reason));
    EXPECT_FALSE(reason.empty());
    EXPECT_FALSE(_ctx->IsRunControlClaimed());
}

TEST_F(RunControlClaim_Test, NilOwnerUUIDReleaseIsNoOp)
{
    ASSERT_TRUE(_ctx->TakeRunControl(_ownerA, "gdb"));
    UUID nil;
    _ctx->ReleaseRunControl(nil);
    EXPECT_TRUE(_ctx->HasRunControl(_ownerA))
        << "Nil-UUID release must not steal the claim from a real holder";
}

/// endregion </Nil owner UUID is rejected>

/// region <Concurrency smoke>

TEST_F(RunControlClaim_Test, ConcurrentTakesExactlyOneWinner)
{
    // Many threads, two identities, racing for the claim. The semantics we need:
    // exactly one of (A, B) ends up holding the claim, never both, never neither,
    // and the final state matches exactly one of the contenders.
    constexpr int N = 16;

    std::vector<std::thread> threads;
    std::atomic<int> aWins{0};
    std::atomic<int> bWins{0};
    std::atomic<int> refusals{0};

    for (int i = 0; i < N; ++i)
    {
        const bool useA = (i % 2) == 0;
        threads.emplace_back([&, useA]()
        {
            const UUID& owner = useA ? _ownerA : _ownerB;
            const std::string label = useA ? "gdb" : "webapi";
            if (_ctx->TakeRunControl(owner, label))
            {
                if (useA) ++aWins; else ++bWins;
            }
            else
            {
                ++refusals;
            }
        });
    }
    for (auto& t : threads) t.join();

    // The first successful take wins; all later attempts by the *other* identity
    // are refusals. The same identity re-taking counts as success (idempotent)
    // but does not increment aWins/bWins beyond the first. We require: at least
    // one winner among A and B, no contradicting winner, and refusals plausible.
    EXPECT_GT(aWins.load() + bWins.load(), 0) << "Someone must have won the race";
    EXPECT_FALSE(aWins.load() > 0 && bWins.load() > 0)
        << "Both identities cannot both have won the claim";

    // Final state must match exactly one of them.
    auto state = _ctx->GetRunControlState();
    EXPECT_TRUE(state.claimed);
    if (aWins.load() > 0)
    {
        EXPECT_EQ(state.ownerUuid, _ownerA.toString());
        EXPECT_EQ(state.surfaceLabel, "gdb");
    }
    else
    {
        EXPECT_EQ(state.ownerUuid, _ownerB.toString());
        EXPECT_EQ(state.surfaceLabel, "webapi");
    }
}

/// endregion </Concurrency smoke>

/// region <The automation gate: implicit sessions (owner decision 2026-10-11)>

namespace
{
constexpr std::chrono::milliseconds kLease{60000};
}

// A client that drives the machine without a session gets one, shown as implicit; its next requests go on in it
TEST_F(RunControlClaim_Test, TheGateOpensAnImplicitSessionAndTheSameClientGoesOnInIt)
{
    const auto first = _ctx->EnterImplicitRunControl("webapi", "webapi|127.0.0.1|curl/8", "curl/8", kLease, "127.0.0.1:5");
    ASSERT_TRUE(first.allowed) << first.message;
    EXPECT_TRUE(first.opened);
    EXPECT_TRUE(_ctx->HasRunControl(first.owner));
    const auto state = _ctx->GetRunControlState();
    EXPECT_TRUE(state.claimed);
    EXPECT_TRUE(state.implicit);
    EXPECT_EQ(state.client, "curl/8");
    EXPECT_EQ(state.leaseMs, 60000u);

    const auto again = _ctx->EnterImplicitRunControl("webapi", "webapi|127.0.0.1|curl/8", "curl/8", kLease, "127.0.0.1:6");
    ASSERT_TRUE(again.allowed);
    EXPECT_FALSE(again.opened);
    EXPECT_EQ(again.owner, first.owner);
}

// Another client is refused while the session is held, implicit or explicit
TEST_F(RunControlClaim_Test, TheGateRefusesAnotherClientWhileASessionIsHeld)
{
    ASSERT_TRUE(_ctx->EnterImplicitRunControl("webapi", "webapi|h|a", "a", kLease).allowed);
    const auto other = _ctx->EnterImplicitRunControl("webapi", "webapi|h|b", "b", kLease);
    EXPECT_FALSE(other.allowed);
    EXPECT_EQ(other.error, "held_by_other");

    RunControlClaim_Test::TearDown();
    RunControlClaim_Test::SetUp();
    ASSERT_TRUE(_ctx->TakeRunControl(_ownerA, "gdb", nullptr, "GDB", std::chrono::milliseconds(0)));
    const auto refused = _ctx->EnterImplicitRunControl("webapi", "webapi|h|a", "a", kLease);
    EXPECT_FALSE(refused.allowed);
    EXPECT_EQ(refused.error, "held_by_other");
    EXPECT_TRUE(_ctx->HasRunControl(_ownerA));
    EXPECT_FALSE(_ctx->GetRunControlState().implicit);
}

// The user takes control back: the same client is refused (revoked_by_user) instead of driving on implicitly
TEST_F(RunControlClaim_Test, AfterATakeBackTheGateRefusesThatClient)
{
    ASSERT_TRUE(_ctx->EnterImplicitRunControl("webapi", "webapi|h|a", "a", kLease).allowed);
    ASSERT_TRUE(_ctx->RevokeRunControl("gui"));
    EXPECT_FALSE(_ctx->IsRunControlClaimed());
    const auto refused = _ctx->EnterImplicitRunControl("webapi", "webapi|h|a", "a", kLease);
    EXPECT_FALSE(refused.allowed);
    EXPECT_EQ(refused.error, "revoked_by_user");
    EXPECT_FALSE(_ctx->IsRunControlClaimed());
    // Another client is not affected; the user's allow-again lifts the refusal
    EXPECT_TRUE(_ctx->EnterImplicitRunControl("webapi", "webapi|h|b", "b", kLease).allowed);
}

// The client takes control explicitly after driving implicitly: its implicit session gives way, nobody else's does
TEST_F(RunControlClaim_Test, AnExplicitTakeReplacesTheClientsOwnImplicitSession)
{
    const auto implicitSession = _ctx->EnterImplicitRunControl("webapi", "webapi|h|a", "a", kLease);
    ASSERT_TRUE(implicitSession.allowed);
    _ctx->ReleaseImplicitRunControl("webapi|h|b");                  // somebody else's identity: nothing happens
    EXPECT_TRUE(_ctx->HasRunControl(implicitSession.owner));
    _ctx->ReleaseImplicitRunControl("webapi|h|a");
    EXPECT_FALSE(_ctx->IsRunControlClaimed());
    ASSERT_TRUE(_ctx->TakeRunControl(_ownerA, "webapi", nullptr, "my-script", kLease));
    EXPECT_FALSE(_ctx->GetRunControlState().implicit);
    _ctx->ReleaseImplicitRunControl("webapi|h|a");                  // an explicit session is never released by it
    EXPECT_TRUE(_ctx->HasRunControl(_ownerA));
}

/// endregion </The automation gate>
