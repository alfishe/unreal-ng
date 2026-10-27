// Model-based fuzz of the execution arbiter (docs/inprogress/2026-10-09-execution-arbiter/design.md §7, §8.1).
//
// Each seed runs several control threads and one fake main loop against one arbiter:
// - the control threads submit intents, synchronous and posted commands (short, driving, nested, paired, with
//   expired deadlines), aborts, claim changes and waits;
// - the loop executes "instructions", fires PauseFromDriver and inline commands from its hooks;
// - a seeded interleave hook yields or spins at every lock release inside the arbiter.
// After the run the decision log is replayed through a sequential reference model, and the invariants I1-I9 are
// checked. A failure prints its seed; UNREAL_EXEC_FUZZ_SEED replays one seed, UNREAL_EXEC_FUZZ_ITERATIONS runs more.

#include <gtest/gtest.h>

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <mutex>
#include <random>
#include <set>
#include <sstream>
#include <thread>
#include <vector>

#include "_helpers/testtier.h"
#include "_helpers/testwaithelper.h"
#include "emulator/execution/executionarbiter.h"

using namespace execution;
using namespace std::chrono_literals;

namespace
{

struct Monitor
{
    std::atomic<int> active{0};
    std::atomic<int> violations{0};
    void Enter()
    {
        if (active.fetch_add(1, std::memory_order_acq_rel) != 0)
            violations.fetch_add(1, std::memory_order_relaxed);
    }
    void Leave() { active.fetch_sub(1, std::memory_order_acq_rel); }
};

thread_local std::mt19937* tlsRng = nullptr;

/// Up to `yields` yields, cut at `limit`: on a loaded host a yield can hand over a whole time slice (Windows
/// SwitchToThread), and a count alone stretched a 500-yield hold past the 10 s hang check (MSVC, seed 5)
void YieldFor(int yields, std::chrono::microseconds limit, const CommandContext* ctx = nullptr)
{
    const auto end = std::chrono::steady_clock::now() + limit;
    for (int i = 0; i < yields && !(ctx && ctx->IsAborted()) && std::chrono::steady_clock::now() < end; ++i)
        std::this_thread::yield();
}

struct Scenario
{
    uint32_t seed = 0;
    int controlThreads = 3;
    int opsPerThread = 25;
    std::chrono::milliseconds throttle{0};
    bool withLoop = true;
    bool stopMidway = false;
};

Scenario MakeScenario(uint32_t seed)
{
    std::mt19937 rng(seed);
    Scenario s;
    s.seed = seed;
    s.controlThreads = 1 + static_cast<int>(rng() % 6);
    s.opsPerThread = 10 + static_cast<int>(rng() % 30);
    const int throttles[] = {0, 0, 0, 1, 3};
    s.throttle = std::chrono::milliseconds(throttles[rng() % 5]);
    s.withLoop = rng() % 8 != 0;
    s.stopMidway = rng() % 6 == 0;
    return s;
}

std::string Describe(const Scenario& s)
{
    std::ostringstream o;
    o << "seed " << s.seed << " (UNREAL_EXEC_FUZZ_SEED=" << s.seed << "): threads " << s.controlThreads << ", ops "
      << s.opsPerThread << ", throttle " << s.throttle.count() << " ms, loop " << s.withLoop << ", stop midway "
      << s.stopMidway;
    return o.str();
}

const Requester kRequesters[] = {
    {Origin::Gui, {}}, {Origin::WebApi, {}}, {Origin::Cli, {}}, {Origin::Gdb, "1"},
    {Origin::DeZog, "a"}, {Origin::Ttd, {}}, {Origin::Internal, {}}};

/// Checks the decision log against the sequential model; returns the problems found
std::vector<std::string> CheckLog(const std::vector<Decision>& log, Intent initial, Intent final,
                                  uint64_t deliveredEdges, const std::vector<uint64_t>& completedCommands)
{
    std::vector<std::string> problems;
    auto fail = [&](const std::string& what) {
        if (problems.size() < 20)
            problems.push_back(what);
    };

    // I2 / I6: replay the intent through the model; each change is one edge, every applied decision continues
    // from the model's state
    Intent model = initial;
    uint64_t edges = 0;
    for (size_t i = 0; i < log.size(); ++i)
    {
        const Decision& d = log[i];
        const bool sets = d.kind == Decision::Kind::IntentApplied || d.kind == Decision::Kind::PriorityPause;
        if (!sets)
            continue;
        if (d.intentBefore != model)
            fail("#" + std::to_string(i) + " seq " + std::to_string(d.seq) + ": intent before " +
                 IntentName(d.intentBefore) + ", model " + IntentName(model));
        if (d.edge != (d.intentBefore != d.intentAfter))
            fail("#" + std::to_string(i) + ": edge flag does not match the change");
        if (d.edge)
            ++edges;
        model = d.intentAfter;
    }
    if (model != final)
        fail(std::string("final intent ") + IntentName(final) + ", model " + IntentName(model));
    if (edges != deliveredEdges)
        fail("edges in the log " + std::to_string(edges) + ", delivered " + std::to_string(deliveredEdges));

    // Ownership (owner-only Run) and the priority lane, against a model that evaluates each batch of queued intents
    // one by one from the state in force: a refusal or an allowance must match the model's owner
    {
        Intent actual = initial;
        Requester owner{Origin::Lifecycle, {}};
        Intent simIntent = actual;
        Requester simOwner = owner;
        uint64_t batch = 0;
        uint64_t lastPriority = 0;
        for (size_t i = 0; i < log.size(); ++i)
        {
            const Decision& d = log[i];
            const bool queuedIntent = !d.command && !d.priority && d.batch != 0;
            if (queuedIntent)
            {
                if (d.batch != batch)
                {
                    batch = d.batch;
                    simIntent = actual;
                    simOwner = owner;
                }
                if (d.seq < lastPriority)
                    fail("intent " + std::to_string(d.seq) + " decided after the priority pause " +
                         std::to_string(lastPriority) + " that superseded it");
                const bool ownerRefusal = d.onlyIfOwner && d.requested == Intent::Run && simIntent == Intent::Pause &&
                                          !(simOwner == d.requester);
                if (d.kind == Decision::Kind::IntentRefused)
                {
                    if (d.detail.rfind("run-control", 0) == 0)
                        continue;  // the claim's timing is not modelled
                    if (!ownerRefusal)
                        fail("intent " + std::to_string(d.seq) + " refused (" + d.detail + "), the model allows it");
                    continue;
                }
                if (ownerRefusal)
                    fail("intent " + std::to_string(d.seq) + " allowed, the model refuses it (owner " +
                         OriginName(simOwner.origin) + ")");
                if (d.requested != simIntent)
                {
                    simIntent = d.requested;
                    simOwner = d.requester;
                }
            }
            if (d.kind == Decision::Kind::PriorityPause)
                lastPriority = std::max(lastPriority, d.seq);
            if ((d.kind == Decision::Kind::IntentCoalesced || d.kind == Decision::Kind::IntentRefused) && d.command)
                fail("command " + std::to_string(d.seq) + " decided as an intent");
            const bool sets = d.kind == Decision::Kind::IntentApplied || d.kind == Decision::Kind::PriorityPause;
            if (sets && d.edge)
            {
                actual = d.intentAfter;
                owner = d.requester;
            }
        }
    }

    // Every command that completed Applied or Aborted ran: it has a finish in the log
    {
        std::set<uint64_t> ran;
        for (const Decision& d : log)
            if (d.kind == Decision::Kind::CommandFinished && d.outcome != Outcome::Refused)
                ran.insert(d.seq);
        for (uint64_t seq : completedCommands)
            if (!ran.count(seq))
                fail("command " + std::to_string(seq) + " completed without running");
    }

    // I3: queued commands start in seq order, each starts and finishes once; nothing overlaps
    std::map<uint64_t, int> started, finished;
    uint64_t lastStarted = 0;
    uint64_t open = 0;
    for (const Decision& d : log)
    {
        if (d.inlined)
            continue;
        if (d.kind == Decision::Kind::CommandStarted)
        {
            if (++started[d.seq] > 1)
                fail("command " + std::to_string(d.seq) + " started twice");
            if (d.seq < lastStarted)
                fail("command " + std::to_string(d.seq) + " started after " + std::to_string(lastStarted));
            if (open != 0)
                fail("command " + std::to_string(d.seq) + " started while " + std::to_string(open) + " is served");
            lastStarted = d.seq;
            open = d.seq;
        }
        else if (d.kind == Decision::Kind::CommandFinished && d.outcome != Outcome::Refused)
        {
            if (++finished[d.seq] > 1)
                fail("command " + std::to_string(d.seq) + " finished twice");
            if (open != d.seq)
                fail("command " + std::to_string(d.seq) + " finished but " + std::to_string(open) + " was served");
            open = 0;
        }
    }
    for (const auto& [seq, n] : started)
        if (!finished.count(seq))
            fail("command " + std::to_string(seq) + " never finished");

    // I4: a queued intent (not priority, not withdrawn, not a command's own effect) is decided after every command
    // with a smaller seq has finished and before any command with a larger seq starts
    std::set<uint64_t> finishedSoFar;
    std::set<uint64_t> startedSoFar;
    for (const Decision& d : log)
    {
        if (d.kind == Decision::Kind::Stop && d.seq == 0)
            break;  // stop refuses the queue and aborts the served command out of order, by design
        if (d.inlined)
            continue;
        if (d.kind == Decision::Kind::CommandStarted)
            startedSoFar.insert(d.seq);
        if (d.kind == Decision::Kind::CommandFinished)
            finishedSoFar.insert(d.seq);
        const bool queuedIntent =
            !d.command && !d.priority &&
            (d.kind == Decision::Kind::IntentApplied || d.kind == Decision::Kind::IntentCoalesced ||
             (d.kind == Decision::Kind::IntentRefused && d.outcome == Outcome::Refused));
        if (!queuedIntent)
            continue;
        for (uint64_t s : startedSoFar)
            if (s < d.seq && !finishedSoFar.count(s))
                fail("intent " + std::to_string(d.seq) + " decided while the older command " + std::to_string(s) +
                     " is served");
        for (uint64_t s : startedSoFar)
            if (s > d.seq)
                fail("intent " + std::to_string(d.seq) + " decided after the newer command " + std::to_string(s) +
                     " started");
    }
    return problems;
}

/// One fuzz run; returns the problems (empty = passed)
std::vector<std::string> RunScenario(const Scenario& s)
{
    std::vector<std::string> problems;
    ExecutionArbiter arbiter;
    Monitor monitor;
    arbiter.SetDecisionLog(true);
    arbiter.SetThrottle(s.throttle);
    std::atomic<uint64_t> delivered{0};
    std::atomic<int> edgeOrderErrors{0};
    std::mutex edgeMutex;
    Intent lastEdge = Intent::Pause;
    arbiter.SetEdgeListener([&](const IntentEdge& e) {
        std::lock_guard<std::mutex> lock(edgeMutex);
        if (e.intent == lastEdge)
            edgeOrderErrors.fetch_add(1);  // two edges to the same intent: one was lost or reordered
        lastEdge = e.intent;
        delivered.fetch_add(1);
    });
    arbiter.SetInterleaveHook([]() {
        if (!tlsRng)
            return;
        const uint32_t r = (*tlsRng)() % 16;
        if (r < 4)
            std::this_thread::yield();
        else if (r == 4)
        {
            volatile int spin = 0;   // a short busy wait the optimizer keeps
            while (spin < 200)
                spin = spin + 1;
        }
    });
    const Intent initial = arbiter.CurrentIntent();

    // Tickets and command results, from every thread
    std::mutex ticketsMutex;
    std::vector<Ticket> tickets;
    std::vector<Ticket> commandTickets;
    std::vector<uint64_t> completedCommands;  // Execute results that ran (Applied / Aborted)
    auto noteResult = [&](const Result& r) {
        if (r.outcome == Outcome::Applied || r.outcome == Outcome::Aborted)
        {
            std::lock_guard<std::mutex> lock(ticketsMutex);
            completedCommands.push_back(r.seq);
        }
    };

    // The loop
    std::atomic<bool> loopDone{false};
    std::atomic<bool> quiet{false};  // the loop's own requests stop before the quiescence check
    std::atomic<bool> quietSeen{false};
    std::thread loop;
    if (s.withLoop)
    {
        loop = std::thread([&]() {
            std::mt19937 rng(s.seed * 7919u + 1u);
            tlsRng = &rng;
            arbiter.AttachMainLoop();
            while (arbiter.LoopWait())
            {
                while (!arbiter.ShouldYield())
                {
                    // An instruction of varying length: a command that starts while one is in flight overlaps it
                    monitor.Enter();
                    for (uint32_t k = rng() % 4; k > 0; --k)
                        std::this_thread::yield();
                    monitor.Leave();
                    if (quiet.load())
                        quietSeen = true;
                    const uint32_t r = quiet.load() ? 400 : rng() % 400;
                    if (r == 0)
                        arbiter.PauseFromDriver(Requester{Origin::Debugger, {}}, "breakpoint");
                    else if (r == 1)
                        noteResult(arbiter.Execute(Requester{Origin::Ttd, {}}, "frame task", [&](CommandContext&) {
                            monitor.Enter();
                            monitor.Leave();
                        }));
                    else if (r == 2)
                        arbiter.SubmitIntent(Intent::Pause, Requester{Origin::Ttd, {}});  // posted, not waited
                    std::this_thread::yield();
                }
            }
            arbiter.DetachMainLoop();
            tlsRng = nullptr;
            loopDone = true;
        });
        TestWait::For([&]() { return arbiter.CurrentPhase() != Phase::NotStarted; });
    }

    // The control threads
    std::atomic<int> stopAt{s.stopMidway ? s.controlThreads * s.opsPerThread / 2 : -1};
    std::atomic<int> opsDone{0};
    std::vector<std::thread> threads;
    for (int t = 0; t < s.controlThreads; ++t)
    {
        threads.emplace_back([&, t]() {
            std::mt19937 rng(s.seed * 104729u + static_cast<uint32_t>(t));
            tlsRng = &rng;
            for (int op = 0; op < s.opsPerThread; ++op)
            {
                if (opsDone.fetch_add(1) == stopAt.load())
                    arbiter.BeginStop();
                const Requester who = kRequesters[rng() % (sizeof(kRequesters) / sizeof(kRequesters[0]))];
                const uint32_t kind = rng() % 21;
                if (kind < 7)
                {
                    IntentOptions options;
                    options.onlyIfOwner = rng() % 5 == 0;
                    Ticket ticket = arbiter.SubmitIntent(rng() % 2 ? Intent::Run : Intent::Pause, who, options);
                    if (rng() % 2)
                        ticket.Wait(std::chrono::milliseconds(rng() % 3));
                    std::lock_guard<std::mutex> lock(ticketsMutex);
                    tickets.push_back(ticket);
                }
                else if (kind < 13)
                {
                    CommandOptions options;
                    options.drivesZ80 = rng() % 3 == 0;
                    options.pauseFirst = options.drivesZ80 && rng() % 2;
                    if (rng() % 6 == 0)
                        options.thenIntent = rng() % 2 ? Intent::Run : Intent::Pause;
                    const bool nested = rng() % 5 == 0;
                    const Clock::time_point deadline =
                        rng() % 6 == 0 ? Clock::now() + std::chrono::microseconds(rng() % 500) : Clock::time_point::max();
                    const int spins = rng() % 4 == 0 ? 2000 : 20;  // a long hold lets the queue grow deep
                    const Result result = arbiter.Execute(who, "cmd", [&, nested, spins](CommandContext& ctx) {
                        monitor.Enter();
                        YieldFor(spins, spins > 20 ? 2000us : 200us, &ctx);
                        monitor.Leave();
                        if (nested)
                            noteResult(arbiter.Execute(who, "nested", [&](CommandContext&) {
                                monitor.Enter();
                                monitor.Leave();
                            }));
                    }, options, deadline);
                    noteResult(result);
                }
                else if (kind < 16)
                {
                    Ticket ticket = arbiter.Post(who, "posted", [&](CommandContext&) {
                        monitor.Enter();
                        monitor.Leave();
                    });
                    std::lock_guard<std::mutex> lock(ticketsMutex);
                    tickets.push_back(ticket);
                    commandTickets.push_back(ticket);
                }
                else if (kind == 16)
                    arbiter.AbortCurrent(who, "abort");
                else if (kind == 17)
                {
                    if (arbiter.TakeClaim(who))
                    {
                        std::this_thread::yield();
                        arbiter.ReleaseClaim(who);
                    }
                }
                else if (kind == 19)
                {
                    // The compat silent pair: a hold around the caller's own code, ended on this thread
                    Result r;
                    CommandOptions holdOptions;
                    holdOptions.drivesZ80 = rng() % 3 == 0;  // a direct run
                    holdOptions.pauseFirst = holdOptions.drivesZ80 && rng() % 2;
                    HoldToken hold = arbiter.BeginHold(who, "hold", rng() % 4 == 0 ? Clock::now() + 300us
                                                                                     : Clock::time_point::max(),
                                                       &r, holdOptions);
                    if (hold.Valid())
                    {
                        monitor.Enter();
                        YieldFor(static_cast<int>(rng() % 30), 200us);
                        monitor.Leave();
                        const uint64_t seq = hold.request ? r.seq : 0;
                        arbiter.EndHold(hold);
                        std::lock_guard<std::mutex> lock(ticketsMutex);
                        completedCommands.push_back(seq);
                    }
                }
                else if (kind == 18)
                {
                    // A burst behind a long posted command: intents and commands interleaved in one deep queue
                    std::vector<Ticket> burst;
                    std::vector<Ticket> burstCommands;
                    burst.push_back(arbiter.Post(who, "burst.hold", [&](CommandContext& ctx) {
                        monitor.Enter();
                        YieldFor(500, 1000us, &ctx);
                        monitor.Leave();
                    }));
                    burstCommands.push_back(burst.back());
                    const int n = 3 + static_cast<int>(rng() % 6);
                    for (int i = 0; i < n; ++i)
                    {
                        if (rng() % 2)
                            burst.push_back(arbiter.SubmitIntent(rng() % 2 ? Intent::Run : Intent::Pause, who));
                        else
                        {
                            burst.push_back(arbiter.Post(who, "burst.cmd", [&](CommandContext&) {
                                monitor.Enter();
                                monitor.Leave();
                            }));
                            burstCommands.push_back(burst.back());
                        }
                    }
                    std::lock_guard<std::mutex> lock(ticketsMutex);
                    tickets.insert(tickets.end(), burst.begin(), burst.end());
                    commandTickets.insert(commandTickets.end(), burstCommands.begin(), burstCommands.end());
                }
                else
                    std::this_thread::yield();
            }
            tlsRng = nullptr;
        });
    }
    // I9: no deadlock. A hung run is reported with the arbiter's state, then released by a stop
    std::atomic<int> finishedThreads{0};
    std::vector<std::thread> joiners;
    for (auto& th : threads)
        joiners.emplace_back([&th, &finishedThreads]() {
            th.join();
            finishedThreads.fetch_add(1);
        });
    if (!TestWait::For([&]() { return finishedThreads.load() == s.controlThreads; }, 10000ms))
    {
        const Snapshot snap = arbiter.GetSnapshot();
        std::ostringstream o;
        o << "hang: phase " << PhaseName(snap.phase) << ", intent " << IntentName(snap.intent) << ", loop parked "
          << snap.loopParked << ", serving " << (snap.serving ? snap.serving->tag : std::string("none"))
          << ", queue " << snap.queue.size();
        for (const SnapshotEntry& e : snap.queue)
            o << " [#" << e.seq << " " << (e.command ? e.tag : std::string(IntentName(e.intent))) << "]";
        problems.push_back(o.str());
        arbiter.BeginStop();
        if (!TestWait::For([&]() { return finishedThreads.load() == s.controlThreads; }, 2000ms))
        {
            // A request lost outside the queue: stop cannot release its waiter, and the threads cannot be joined
            std::fprintf(stderr, "ExecutionArbiterFuzz: %s\n  %s\n  unrecoverable, exiting\n", Describe(s).c_str(),
                         o.str().c_str());
            std::fflush(stderr);
            std::_Exit(1);
        }
    }
    for (auto& j : joiners)
        j.join();

    // Quiescence: with a loop running, posted work and queued intents drain; then stop
    quiet = true;
    // A hook in flight when the flag went up ends within one loop step; a parked loop runs no hooks
    TestWait::For([&]() { return quietSeen.load() || arbiter.IsParked(); }, 500ms);
    if (s.withLoop)
        TestWait::For([&]() { return arbiter.GetSnapshot().queue.empty() && !arbiter.GetSnapshot().serving; }, 2000ms);
    else
        arbiter.SubmitIntent(arbiter.CurrentIntent(), Requester{Origin::Internal, {}}).Wait(2s);  // pumps the rest
    const Intent finalIntent = arbiter.CurrentIntent();
    arbiter.BeginStop();
    if (loop.joinable())
        loop.join();

    // I5: every ticket completed
    for (const Ticket& ticket : tickets)
        if (!ticket.Done())
            problems.push_back("ticket " + std::to_string(ticket.Peek().seq) + " never completed");

    for (const Ticket& ticket : commandTickets)
    {
        const Result r = ticket.Peek();
        if (r.outcome == Outcome::Applied || r.outcome == Outcome::Aborted)
            completedCommands.push_back(r.seq);
    }

    // I1 / I7
    if (monitor.violations.load() != 0)
        problems.push_back(std::to_string(monitor.violations.load()) + " moments with two drivers");
    if (edgeOrderErrors.load() != 0)
        problems.push_back("delivered edges do not alternate");

    // The log (taken after stop: the stop's refusals are in it too)
    const std::vector<Decision> log = arbiter.TakeDecisions();
    const Intent afterStop = arbiter.CurrentIntent();
    if (afterStop != finalIntent && !s.stopMidway)
        problems.push_back("the intent changed during stop");
    for (const std::string& p : CheckLog(log, initial, afterStop, delivered.load(), completedCommands))
        problems.push_back(p);
    return problems;
}

uint32_t EnvU32(const char* name, uint32_t fallback)
{
    const char* v = std::getenv(name);
    return v && *v ? static_cast<uint32_t>(std::strtoul(v, nullptr, 10)) : fallback;
}

}  // namespace

TEST(ExecutionArbiterFuzz_Functional, SeededScenariosKeepTheInvariants)
{
    SKIP_UNLESS_FUNCTIONAL();
    // A fixed set in the default suite (each seed is a few ms); UNREAL_EXEC_FUZZ_ITERATIONS for long runs
    const char* one = std::getenv("UNREAL_EXEC_FUZZ_SEED");
    const uint32_t iterations = one ? 1 : EnvU32("UNREAL_EXEC_FUZZ_ITERATIONS", 12);
    const uint32_t first = one ? EnvU32("UNREAL_EXEC_FUZZ_SEED", 0) : 1;
    for (uint32_t i = 0; i < iterations; ++i)
    {
        const Scenario s = MakeScenario(first + i);
        const std::vector<std::string> problems = RunScenario(s);
        if (!problems.empty())
        {
            std::ostringstream o;
            for (const std::string& p : problems)
                o << "\n  " << p;
            FAIL() << Describe(s) << o.str();
        }
    }
}
