/// @file analysisl3_test.cpp
/// @brief L3 of the analysis layers (docs/emulator/analysis/analysis-layers.md, "L3: review") end to end: fixture
/// programs (testdata/analysis/fixtures/poke-flip.asm, key-test.asm, read-unwritten.asm) run a few frames under a TTD
/// recording, the replay builds L1 and L2, then the findings register, candidate bugs, the review queue (selection,
/// the cap, resolve writing into the draft), the experiment runner (observed, refuted, two in parallel touching
/// neither each other nor the source) and the coverage driver (a branch flipped by a held byte, a key test flipped by
/// a held key).

#include "pch.h"

#include <chrono>
#include <condition_variable>
#include <fstream>
#include <map>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <vector>

#include "_helpers/emulatortesthelper.h"
#include "_helpers/testpathhelper.h"
#include "base/featuremanager.h"
#include "debugger/analyzers/analysis/analysiscontrol.h"
#include "debugger/analyzers/analysis/analysisjobs.h"
#include "debugger/analyzers/analysis/detectors/detectors.h"
#include "debugger/analyzers/analysis/l3coverage.h"
#include "debugger/analyzers/analysis/l3experiment.h"
#include "debugger/analyzers/analysis/l3findings.h"
#include "debugger/analyzers/analysis/l3review.h"
#include "debugger/analyzers/analysis/replaytracer.h"
#include "debugger/assembler/z80textassembler.h"
#include "debugger/ttd/timetravelcontroller.h"
#include "emulator/cpu/core.h"
#include "emulator/cpu/z80.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/memory/memory.h"
#include "emulator/sound/soundmanager.h"

using namespace analysis;

namespace
{

std::string ReadFixture(const std::string& name)
{
    std::ifstream file(TestPathHelper::GetTestDataPath("analysis/fixtures/" + name), std::ios::binary);
    std::stringstream text;
    text << file.rdbuf();
    return text.str();
}

const StateNode& Member(const StateNode& node, const std::string& key)
{
    static const StateNode kNull;
    const StateNode* found = node.find(key);
    return found != nullptr ? *found : kNull;
}

/// A source machine running a fixture under a TTD recording
struct Source
{
    Emulator* emulator = nullptr;
    AsmResult assembled;

    bool Make(const std::string& fixture)
    {
        emulator = EmulatorTestHelper::CreateStandardEmulator("48K", LoggerLevel::LogError, RamPowerOn::Zero);
        if (emulator == nullptr)
            return false;
        EmulatorContext* context = emulator->GetContext();
        emulator->GetFeatureManager()->setFeature(Features::kDebugMode, true);
        emulator->GetFeatureManager()->setFeature(Features::kTimeTravel, true);
        emulator->GetFeatureManager()->setFeature(Features::kSoundGeneration, false);
        if (context->pSoundManager)
            context->pSoundManager->mute();
        context->pMemory->UpdateFeatureCache();
        assembled = Z80TextAssembler().Assemble(ReadFixture(fixture), 0x8000);
        if (!assembled.ok)
            return false;
        for (const AsmSourceLine& line : assembled.lines)
            for (size_t i = 0; i < line.bytes.size(); ++i)
                context->pMemory->DirectWriteToZ80Memory(static_cast<uint16_t>(line.address + i), line.bytes[i]);
        Z80* z80 = context->pCore->GetZ80();
        z80->pc = static_cast<uint16_t>(assembled.symbols.at("start"));
        z80->sp = 0x9F00;
        z80->iff1 = z80->iff2 = 0;
        return true;
    }

    uint16_t At(const std::string& symbol) const { return static_cast<uint16_t>(assembled.symbols.at(symbol)); }

    ~Source()
    {
        if (emulator != nullptr)
        {
            L1Jobs::Instance().Forget(emulator->GetId());
            EmulatorTestHelper::CleanupEmulator(emulator);
        }
    }
};

struct Built
{
    std::shared_ptr<TraceBundle> bundle = std::make_shared<TraceBundle>();
    L1Result l1;
    L2Result l2;
    std::map<std::string, uint16_t> symbols;
};

/// Record @p frames frames of @p source, capture, replay, run the detectors
bool Build(Source& source, unsigned frames, Built& built)
{
    ttd::TimeTravelController* ttd = source.emulator->GetContext()->pTimeTravelController;
    EXPECT_TRUE(ttd != nullptr && ttd->StartRecording());
    source.emulator->RunNFrames(frames);
    std::string error;
    EXPECT_TRUE(TraceBundle::Capture(*source.emulator, *built.bundle, error)) << error;
    ReplayOptions options;
    const bool ok = ReplayBuild(*built.bundle, options, built.l1, error);
    EXPECT_TRUE(ok) << error;
    if (!ok)
        return false;
    built.l2 = RunDetectors(built.l1);
    for (const auto& [name, value] : source.assembled.symbols)
        built.symbols[name] = static_cast<uint16_t>(value);
    return true;
}

ExperimentSpec Spec(const std::string& json)
{
    std::vector<ExperimentSpec> specs;
    std::string error;
    EXPECT_TRUE(ParseExperiments(json, specs, error)) << error;
    return specs.empty() ? ExperimentSpec{} : specs.front();
}

}  // namespace

class AnalysisL3_Test : public ::testing::Test
{
protected:
    /// One recording per fixture (the source is released once captured): the tests reading one fixture share it
    const Built& Fixture(const std::string& name, unsigned frames = 3)
    {
        static std::map<std::string, Built> cache;
        const std::string key = name + "/" + std::to_string(frames);
        if (const auto it = cache.find(key); it != cache.end())
            return it->second;
        Built built;
        {
            Source source;
            EXPECT_TRUE(source.Make(name)) << name << ": " << source.assembled.error.message;
            if (source.emulator != nullptr && source.assembled.ok)
                Build(source, frames, built);
        }
        return cache.emplace(key, std::move(built)).first->second;
    }
};

TEST_F(AnalysisL3_Test, FindingsRegisterHasEveryClaimInAFixedOrder)
{
    const Built& b = Fixture("poke-flip.asm");
    const std::vector<Finding> findings = BaseFindings(b.l1, b.l2);
    ASSERT_GE(findings.size(), b.l2.store.Size());
    for (size_t i = 0; i < findings.size(); ++i)
        EXPECT_EQ(findings[i].id, "F" + std::to_string(i + 1));
    for (size_t i = 0; i < b.l2.store.Size(); ++i)
    {
        EXPECT_EQ(findings[i].kind, FindingKind::Claim);
        EXPECT_EQ(findings[i].region, static_cast<int32_t>(i));
        EXPECT_EQ(findings[i].start, b.l2.store.Regions()[i].z80);
    }
    // Groups in order: claims, conflicts, bugs, routines, unknown areas
    int last = 0;
    const auto rank = [](FindingKind k) {
        switch (k)
        {
            case FindingKind::Claim: return 0;
            case FindingKind::Conflict: return 1;
            case FindingKind::Bug: return 2;
            case FindingKind::Routine: return 3;
            default: return 4;
        }
    };
    for (const Finding& f : findings)
    {
        EXPECT_GE(rank(f.kind), last) << f.id;
        last = rank(f.kind);
    }
    // Deterministic: the same register again, byte for byte
    EXPECT_EQ(FindingsJsonl(findings), FindingsJsonl(BaseFindings(b.l1, b.l2)));
    const std::string md = FindingsMarkdown(findings, "");
    EXPECT_NE(md.find("| ID | Status | Claim | Evidence | Refs |"), std::string::npos);
    EXPECT_NE(md.find("| F1 | "), std::string::npos);
}

TEST_F(AnalysisL3_Test, CounterThatWrapsIsACandidateBug)
{
    const Built& b = Fixture("poke-flip.asm");
    const uint16_t ticks = b.symbols.at("ticks");
    bool found = false;
    for (const Finding& f : BaseFindings(b.l1, b.l2))
        if (f.kind == FindingKind::Bug && f.subject == "counter-wrap" && f.start == ticks)
            found = true;
    EXPECT_TRUE(found) << "ticks (INC (HL) over thousands of passes) wraps $FF -> $00";
    // The rare counter never ran: no wrap there
    for (const Finding& f : BaseFindings(b.l1, b.l2))
        EXPECT_FALSE(f.kind == FindingKind::Bug && f.start == b.symbols.at("rarecount"));
}

TEST_F(AnalysisL3_Test, ReadUnwrittenFlagsABufferNotAStaticTable)
{
    const Built& b = Fixture("read-unwritten.asm");
    const uint16_t buffer = b.symbols.at("buffer");
    const uint16_t table = b.symbols.at("table");
    bool bufferFound = false;
    for (const Finding& f : BaseFindings(b.l1, b.l2))
    {
        if (f.kind != FindingKind::Bug || f.subject != "read-unwritten")
            continue;
        if (f.start <= buffer && f.end >= buffer)
            bufferFound = true;
        EXPECT_FALSE(f.end >= table && f.start < table + 32u)
            << "the zeros of a static table its reader also reads values from are no candidate: " << f.title;
    }
    EXPECT_TRUE(bufferFound) << "an area read and never written, all zeros, is a candidate";
}

TEST_F(AnalysisL3_Test, ReviewQueueSelectsDecisionsCapsAndDropsResolved)
{
    const Built& b = Fixture("poke-flip.asm");
    std::vector<Finding> findings = BaseFindings(b.l1, b.l2);
    ReviewOptions options;
    const ReviewQueue queue = SelectReview(findings, b.l2, options);
    ASSERT_FALSE(queue.items.empty());
    EXPECT_EQ(queue.items.front().reason, "bug") << "bugs first";
    for (size_t i = 1; i < queue.items.size(); ++i)
        EXPECT_LE(queue.items[i - 1].priority, queue.items[i].priority);
    for (const ReviewItem& item : queue.items)
    {
        const Finding& f = findings[item.finding];
        if (f.kind == FindingKind::Claim)
            EXPECT_LT(f.confidence, confidence::kReview) << item.id << ": a confident claim needs no decision";
    }
    // The sheet: <= 1 KB, with the resolve line
    ReviewTools tools;
    tools.disassemble = [](const uint8_t*, uint16_t) { return std::string("nop"); };
    const std::string sheet = ReviewSheet(findings[queue.items.front().finding], queue.items.front(), b.l1, b.l2, tools);
    EXPECT_LE(sheet.size(), kReviewSheetBytes);
    EXPECT_NE(sheet.find("op=resolve id=" + queue.items.front().id), std::string::npos) << sheet;

    // The cap leaves one summary of the rest
    ReviewOptions capped;
    capped.limit = 1;
    const ReviewQueue one = SelectReview(findings, b.l2, capped);
    ASSERT_EQ(one.items.size(), 1u);
    if (queue.total > 1)
        EXPECT_NE(one.summary.find("more items"), std::string::npos) << one.summary;
    // A decision leaves the queue
    findings[queue.items.front().finding].verdict = "ok";
    const ReviewQueue after = SelectReview(findings, b.l2, options);
    EXPECT_EQ(after.total + 1, queue.total);
    EXPECT_EQ(after.resolved, queue.resolved + 1);
}

TEST_F(AnalysisL3_Test, ExperimentObservedAndRefuted)
{
    const Built& b = Fixture("poke-flip.asm");
    char json[512];
    std::snprintf(json, sizeof(json),
                  R"([{"id": "mode5", "poke": {"$%04X": 5}, "frames": 2, "watch": [{"pc": "$%04X", "as": "rare"}],
                      "expect": {"rare": "reached", "mem:$%04X": "$AA", "new_code": ">0"}},
                     {"id": "nopoke", "frames": 2, "watch": [{"pc": "$%04X", "as": "rare"}],
                      "expect": {"rare": "reached"}}])",
                  b.symbols.at("mode"), b.symbols.at("rare"), b.symbols.at("seen"), b.symbols.at("rare"));
    std::vector<ExperimentSpec> specs;
    std::string error;
    ASSERT_TRUE(ParseExperiments(json, specs, error)) << error;
    ExperimentRunOptions options;
    options.parallel = 1;
    options.l1 = &b.l1;
    const ExperimentBatch batch = RunExperiments(*b.bundle, specs, options);
    ASSERT_TRUE(batch.error.empty()) << batch.error;
    ASSERT_EQ(batch.results.size(), 2u);
    EXPECT_EQ(batch.results[0].verdict, "observed") << ExperimentResultLine(batch.results[0]);
    EXPECT_GT(batch.results[0].newCode, 0u) << "the rare code never ran in the session";
    EXPECT_EQ(batch.results[1].verdict, "refuted") << ExperimentResultLine(batch.results[1]);
    EXPECT_EQ(batch.results[1].checks.at(0).actual, "0 hits");
}

TEST_F(AnalysisL3_Test, ExperimentSpecRefusals)
{
    std::vector<ExperimentSpec> specs;
    std::string error;
    EXPECT_FALSE(ParseExperiments("{", specs, error));
    EXPECT_FALSE(ParseExperiments(R"({"frames": 2})", specs, error)) << "no expect";
    EXPECT_NE(error.find("expect"), std::string::npos) << error;
    EXPECT_FALSE(ParseExperiments(R"({"frames": 2, "expect": {"rare": "reached"}})", specs, error)) << "no such watch";
    EXPECT_FALSE(ParseExperiments(R"({"poke": {"$10000": 1}, "expect": {"screen": "changed"}})", specs, error));
    EXPECT_FALSE(ParseExperiments(R"({"input": [{"key": "nokey"}], "expect": {"screen": "changed"}})", specs, error));
    EXPECT_FALSE(ParseExperiments(R"({"bogus": 1, "expect": {"screen": "changed"}})", specs, error));
    EXPECT_TRUE(ParseExperiments(R"({"from": "frame:1", "input": [{"frame": 0, "key": "q"}, {"frame": 1, "key": "q",
                                   "up": true}], "regs": {"a": 1}, "expect": {"screen": "unchanged"}})",
                                 specs, error))
        << error;
    ASSERT_EQ(specs.size(), 1u);
    EXPECT_EQ(specs[0].id, "e1");
    EXPECT_EQ(specs[0].from, ExperimentSpec::From::Frame);
    EXPECT_EQ(specs[0].input.size(), 2u);
    EXPECT_FALSE(specs[0].input[1].press);
}

/// The plan's mutation "the experiment runner reuses the source instance" fails here: the source's memory, frame and
/// CPU stay as they were, and each experiment sees only its own poke
TEST_F(AnalysisL3_Test, ParallelExperimentsTouchNeitherEachOtherNorTheSource)
{
    Source source;
    ASSERT_TRUE(source.Make("poke-flip.asm"));
    Built b;
    ASSERT_TRUE(Build(source, 3, b));
    Memory* memory = source.emulator->GetMemory();
    std::vector<uint8_t> before(0x10000);
    for (uint32_t a = 0; a < 0x10000; ++a)
        before[a] = memory->DirectReadFromZ80Memory(static_cast<uint16_t>(a));
    const uint64_t frame = source.emulator->GetContext()->emulatorState.frame_counter;
    const uint16_t pc = source.emulator->GetContext()->pCore->GetZ80()->pc;

    char json[640];
    std::snprintf(json, sizeof(json),
                  R"([{"id": "five", "poke": {"$%04X": 5}, "frames": 2, "expect": {"mem:$%04X": "$AA", "mem:$%04X": "5"}},
                     {"id": "one", "poke": {"$%04X": 1}, "frames": 2,
                      "watch": [{"pc": "$%04X", "as": "rare"}], "expect": {"mem:$%04X": "0", "rare": "not_reached"}}])",
                  b.symbols.at("mode"), b.symbols.at("seen"), b.symbols.at("mode"), b.symbols.at("mode"),
                  b.symbols.at("rare"), b.symbols.at("seen"));
    std::vector<ExperimentSpec> specs;
    std::string error;
    ASSERT_TRUE(ParseExperiments(json, specs, error)) << error;

    // Both experiments wait for each other once their machines hold their state: they run at the same time
    std::mutex mutex;
    std::condition_variable cv;
    int started = 0;
    bool together = true;
    ExperimentRunOptions options;
    options.parallel = 2;
    options.l1 = &b.l1;
    options.onStarted = [&](size_t) {
        std::unique_lock<std::mutex> lock(mutex);
        ++started;
        cv.notify_all();
        if (!cv.wait_for(lock, std::chrono::seconds(10), [&] { return started >= 2; }))
            together = false;
    };
    const ExperimentBatch batch = RunExperiments(*b.bundle, specs, options);
    ASSERT_TRUE(batch.error.empty()) << batch.error;
    EXPECT_TRUE(together) << "the second experiment never started while the first waited";
    EXPECT_EQ(batch.workers, 2u);
    EXPECT_EQ(batch.peakConcurrency, 2u) << "the race did not happen: both must run at once";
    EXPECT_NE(batch.results[0].worker, batch.results[1].worker);
    EXPECT_EQ(batch.results[0].verdict, "observed") << ExperimentResultLine(batch.results[0]);
    EXPECT_EQ(batch.results[1].verdict, "observed") << ExperimentResultLine(batch.results[1])
                                                    << ": the other experiment's poke leaked in";

    size_t changed = 0;
    for (uint32_t a = 0; a < 0x10000; ++a)
        changed += memory->DirectReadFromZ80Memory(static_cast<uint16_t>(a)) != before[a] ? 1 : 0;
    EXPECT_EQ(changed, 0u) << "an experiment wrote into the source instance";
    EXPECT_EQ(source.emulator->GetContext()->emulatorState.frame_counter, frame) << "the source ran";
    EXPECT_EQ(source.emulator->GetContext()->pCore->GetZ80()->pc, pc);
    EXPECT_EQ(memory->DirectReadFromZ80Memory(b.symbols.at("seen")), 0u);
}

TEST_F(AnalysisL3_Test, CoverageFlipsABranchWithAHeldByte)
{
    const Built& b = Fixture("poke-flip.asm");
    CoverOptions options;
    options.frames = 3;
    options.parallel = 1;
    const CoverResult result = RunCoverage(*b.bundle, b.l1, options);
    ASSERT_TRUE(result.error.empty()) << result.error;
    const uint16_t branch = static_cast<uint16_t>(b.symbols.at("rare") - 4);  // jr z,rare (then jr main)
    const CoverBranch* hit = nullptr;
    for (const CoverBranch& c : result.branches)
        if (c.z80 == branch)
            hit = &c;
    ASSERT_NE(hit, nullptr) << "the never-taken jr z is a candidate";
    EXPECT_EQ(hit->status, "flipped") << CoverBranchLine(*hit);
    EXPECT_EQ(hit->tested, "memory");
    EXPECT_EQ(hit->address, b.symbols.at("mode"));
    EXPECT_EQ(hit->how.rfind("poke $", 0), 0u) << hit->how;
    EXPECT_NE(hit->how.find("=$05"), std::string::npos) << "the compared constant first: " << hit->how;
    EXPECT_EQ(hit->attempts, 1u);
    EXPECT_GE(hit->fromFrame, 1u) << "from the last frame the branch ran in, not the recording's start";
    EXPECT_GT(hit->newCode, 0u);
    EXPECT_GE(result.newCode, hit->newCode);
}

TEST_F(AnalysisL3_Test, CoverageFlipsAKeyTestWithAHeldKey)
{
    const Built& b = Fixture("key-test.asm");
    CoverOptions options;
    options.frames = 3;
    options.parallel = 1;
    const CoverResult result = RunCoverage(*b.bundle, b.l1, options);
    ASSERT_TRUE(result.error.empty()) << result.error;
    const uint16_t branch = static_cast<uint16_t>(b.symbols.at("pressed") - 4);  // jr z,pressed (after jr main)
    const CoverBranch* hit = nullptr;
    for (const CoverBranch& c : result.branches)
        if (c.z80 == branch)
            hit = &c;
    ASSERT_NE(hit, nullptr);
    EXPECT_EQ(hit->tested, "port");
    EXPECT_EQ(hit->address, 0xFBFE);
    EXPECT_EQ(hit->status, "flipped") << CoverBranchLine(*hit);
    EXPECT_EQ(hit->how, "key q");
    EXPECT_GT(hit->newCode, 0u);
}

TEST_F(AnalysisL3_Test, ExperimentWithAKeyReachesTheKeyHandler)
{
    const Built& b = Fixture("key-test.asm");
    char json[320];
    std::snprintf(json, sizeof(json),
                  R"({"input": [{"frame": 0, "key": "q"}], "frames": 2, "watch": [{"pc": "$%04X", "as": "pressed"},
                      {"write": "$%04X", "as": "w"}], "expect": {"pressed": "reached", "w": ">=1", "mem:$%04X": "$55"}})",
                  b.symbols.at("pressed"), b.symbols.at("hits"), b.symbols.at("hits"));
    ExperimentRunOptions options;
    options.parallel = 1;
    const ExperimentBatch batch = RunExperiments(*b.bundle, {Spec(json)}, options);
    ASSERT_EQ(batch.results.size(), 1u);
    EXPECT_EQ(batch.results[0].verdict, "observed") << ExperimentResultLine(batch.results[0]);
    // A branch attempt from every recorded frame: the state taken at a frame boundary may sit between the port
    // read and the branch, so the first arrival tests the old flags and must not decide the attempt
    for (uint32_t ff : {0u, 1u, 2u})
    {
        ExperimentSpec spec = Spec(R"({"input": [{"frame": 0, "key": "q"}], "expect": {"branch": "flipped"}})");
        spec.from = ExperimentSpec::From::Frame;
        spec.fromFrame = ff;
        spec.frames = 3;
        spec.branch = true;
        spec.branchPc = static_cast<uint16_t>(b.symbols.at("pressed") - 4);
        spec.branchLength = 2;
        spec.wantTaken = true;
        spec.releaseInputWhenDecided = true;
        ExperimentRunOptions run = options;
        run.l1 = &b.l1;
        const ExperimentBatch later = RunExperiments(*b.bundle, {spec}, run);
        ASSERT_EQ(later.results.size(), 1u);
        EXPECT_EQ(later.results[0].verdict, "observed") << ExperimentResultLine(later.results[0]);
    }
}

/// Through the verbs: review next, resolve with a label, the draft carries it; an experiment marks a finding; cover
TEST_F(AnalysisL3_Test, ReviewResolveWritesIntoTheRegisterAndTheDraft)
{
    Source source;
    ASSERT_TRUE(source.Make("poke-flip.asm"));
    Emulator& emulator = *source.emulator;
    EXPECT_EQ(Execute(emulator, "review").error, Error::Conflict) << "no L2 yet";
    ASSERT_TRUE(Execute(emulator, "start", {{"record", "true"}}).Ok());
    emulator.RunNFrames(3);
    const Reply built = Execute(emulator, "build", {{"wait", "20000"}});
    ASSERT_EQ(Member(built.body, "state").s, "done") << built.text;

    const Reply next = Execute(emulator, "review");
    ASSERT_TRUE(next.Ok()) << next.message;
    EXPECT_LE(Member(next.body, "sheet_bytes").i, static_cast<int64_t>(kReviewSheetBytes));
    const std::string first = Member(Member(next.body, "item"), "id").s;
    ASSERT_FALSE(first.empty()) << next.text;
    EXPECT_EQ(Member(Member(next.body, "item"), "state").s, "shown");
    EXPECT_NE(Member(Execute(emulator, "review").body, "item").find("id")->s, first) << "next moves on";

    // The rare routine's start: name it
    const uint16_t rare = source.At("rare");
    const Reply listed = Execute(emulator, "findings", {{"kind", "claim"}, {"range", "8000-80FF"}, {"limit", "500"}});
    ASSERT_TRUE(listed.Ok()) << listed.message;
    std::string rareId;
    char rareHex[8];
    std::snprintf(rareHex, sizeof(rareHex), "$%04X", rare);
    for (const StateNode& f : Member(listed.body, "findings").items)
        if (Member(f, "start").s == rareHex && Member(f, "subject").s == "code")
            rareId = Member(f, "id").s;
    ASSERT_FALSE(rareId.empty()) << listed.text;
    EXPECT_EQ(Execute(emulator, "review", {{"op", "resolve"}, {"id", rareId}, {"label", "9bad"}}).error,
              Error::BadRequest);
    EXPECT_EQ(Execute(emulator, "review", {{"op", "resolve"}, {"id", "F99999"}, {"verdict", "ok"}}).error,
              Error::NotFound);
    EXPECT_EQ(Execute(emulator, "review", {{"op", "resolve"}, {"id", rareId}}).error, Error::BadRequest);
    const Reply resolved = Execute(emulator, "review", {{"op", "resolve"},
                                                        {"id", rareId},
                                                        {"verdict", "ok"},
                                                        {"label", "RareMode"},
                                                        {"title", "Mode 5 handler"},
                                                        {"comment", "Runs only when mode is 5."}});
    ASSERT_TRUE(resolved.Ok()) << resolved.message;
    EXPECT_EQ(Member(Member(resolved.body, "finding"), "status").s, "confirmed");
    const Reply skool = Execute(emulator, "export", {{"format", "skool"}, {"ranges", "8000-80FF"}});
    ASSERT_TRUE(skool.Ok()) << skool.message;
    const std::string content = Member(skool.body, "content").s;
    EXPECT_NE(content.find("@label=RareMode"), std::string::npos) << content;
    EXPECT_NE(content.find("; Mode 5 handler"), std::string::npos) << content;
    EXPECT_NE(content.find("Runs only when mode is 5."), std::string::npos);

    // An experiment marks the finding observed
    char json[256];
    std::snprintf(json, sizeof(json),
                  R"({"poke": {"$%04X": 5}, "frames": 2, "watch": [{"pc": "$%04X", "as": "rare"}],
                      "expect": {"rare": "reached"}})",
                  source.At("mode"), rare);
    const Reply experiment = Execute(emulator, "experiment", {{"spec", json}, {"finding", rareId}});
    ASSERT_TRUE(experiment.Ok()) << experiment.message;
    EXPECT_EQ(Member(experiment.body, "observed").i, 1);
    const Reply after = Execute(emulator, "findings", {{"kind", "claim"}, {"range", "8000-80FF"}, {"limit", "500"}});
    for (const StateNode& f : Member(after.body, "findings").items)
        if (Member(f, "id").s == rareId)
            EXPECT_EQ(Member(f, "status").s, "observed");

    // Coverage: the flip is a finding and a draft note
    const Reply cover = Execute(emulator, "cover", {{"frames", "3"}, {"parallel", "1"}});
    ASSERT_TRUE(cover.Ok()) << cover.message;
    EXPECT_GE(Member(cover.body, "flipped").i, 1) << cover.text;
    const Reply coverage = Execute(emulator, "findings", {{"kind", "coverage"}});
    EXPECT_GE(Member(coverage.body, "count").i, 1);
    const Reply ctl = Execute(emulator, "export", {{"format", "skool"}, {"ranges", "8000-80FF"}});
    EXPECT_NE(Member(ctl.body, "content").s.find("needs poke"), std::string::npos);

    const Reply md = Execute(emulator, "findings", {{"format", "markdown"}});
    EXPECT_NE(Member(md.body, "content").s.find("RareMode"), std::string::npos);
}
