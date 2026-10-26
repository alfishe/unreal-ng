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
#include "debugger/analyzers/analysis/l2export.h"
#include "debugger/analyzers/analysis/l3findings.h"
#include "debugger/analyzers/analysis/l3naming.h"
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

// ---------------------------------------------------------------------------------------------------- naming, entries

namespace
{

std::string Hex4Text(uint16_t z80)
{
    char text[8];
    std::snprintf(text, sizeof(text), "$%04X", z80);
    return text;
}

/// Record and build @p fixture through Execute (the review's own path)
bool BuildThroughExecute(Emulator& emulator)
{
    if (!Execute(emulator, "start", {{"record", "true"}}).Ok())
        return false;
    emulator.RunNFrames(3);
    const Reply built = Execute(emulator, "build", {{"wait", "20000"}});
    return Member(built.body, "state").s == "done";
}

}  // namespace

TEST_F(AnalysisL3_Test, NamingBatchesPickAddressNamesInBatchesOfOneClass)
{
    const Built& b = Fixture("tail-entry.asm");
    const DraftPlan plan = MakePlan(b.l1, b.l2, ExportOptions{});
    const NamingResult naming = NamingBatches(b.l1, b.l2, plan);
    ASSERT_FALSE(naming.batches.empty());
    EXPECT_EQ(naming.capped, 0u);
    uint32_t subjects = 0;
    bool point = false, data = false, operand = false;
    for (size_t i = 0; i < naming.batches.size(); ++i)
    {
        const NamingBatch& batch = naming.batches[i];
        EXPECT_EQ(batch.id, "N" + std::to_string(i + 1));
        ASSERT_FALSE(batch.subjects.empty());
        EXPECT_LE(batch.subjects.size(), kNameBatchSubjects);
        for (const NamingSubject& s : batch.subjects)
        {
            EXPECT_EQ(s.code, batch.code) << s.label << ": code and data never share a batch";
            EXPECT_TRUE(IsAddressName(s.label, s.z80)) << s.label;
            point = point || !s.entry;
            data = data || !s.code;
            operand = operand || (!s.code && s.variable);
            ++subjects;
        }
    }
    EXPECT_EQ(subjects, naming.subjects);
    EXPECT_TRUE(data) << "the variable n";
    EXPECT_TRUE(operand) << "n is a variable: over the cap it ranks as a data entry";
    // rj: an absolute JP reaches it (a point); main: only `jr main` does (a loop label), a subject too
    const uint16_t rj = b.symbols.at("rj");
    const uint16_t loop = b.symbols.at("main");
    bool rjSubject = false, loopSubject = false;
    for (const NamingBatch& batch : naming.batches)
        for (const NamingSubject& s : batch.subjects)
        {
            rjSubject = rjSubject || (s.z80 == rj && !s.entry);
            loopSubject = loopSubject || (s.z80 == loop && !s.entry);
        }
    EXPECT_TRUE(point && rjSubject);
    EXPECT_TRUE(loopSubject) << "a loop label is named too";
    // Deterministic: the same batches again
    const NamingResult again = NamingBatches(b.l1, b.l2, MakePlan(b.l1, b.l2, ExportOptions{}));
    ASSERT_EQ(again.batches.size(), naming.batches.size());
    for (size_t i = 0; i < again.batches.size(); ++i)
        EXPECT_EQ(NamingItemClaim(again.batches[i]), NamingItemClaim(naming.batches[i]));
    // The sheet: at most 2 KB, a disassembly head for code, the resolve line
    for (const NamingBatch& batch : naming.batches)
    {
        const std::string sheet =
            NamingSheet(batch, b.l1, b.l2, plan, [](const uint8_t*, uint16_t) { return std::string("nop"); });
        EXPECT_LE(sheet.size(), kNameSheetBytes);
        EXPECT_NE(sheet.find("op=resolve id=" + batch.id + " names="), std::string::npos) << sheet;
        if (batch.code)
            EXPECT_NE(sheet.find("nop | nop"), std::string::npos) << sheet;
        else
            EXPECT_NE(sheet.find("reads"), std::string::npos) << sheet;
    }
    // Names: an object or an array, addresses in any spelling
    std::vector<GivenName> names;
    std::string error;
    ASSERT_TRUE(ParseGivenNames(R"({"$8068": "PlayerY", "0x8069": {"label": "Lives", "title": "Lives left"}})", names,
                                error))
        << error;
    ASSERT_EQ(names.size(), 2u);
    EXPECT_EQ(names[1].z80, 0x8069);
    EXPECT_EQ(names[1].title, "Lives left");
    ASSERT_TRUE(ParseGivenNames(R"([{"at": "8068", "label": "PlayerY"}])", names, error)) << error;
    EXPECT_FALSE(ParseGivenNames(R"({"nowhere": "X"})", names, error));
    EXPECT_FALSE(ParseGivenNames(R"([{"label": "X"}])", names, error));
    EXPECT_FALSE(ParseGivenNames("[]", names, error));
}

TEST_F(AnalysisL3_Test, ReviewNamesABatchIntoTheDraft)
{
    Source source;
    ASSERT_TRUE(source.Make("tail-entry.asm"));
    Emulator& emulator = *source.emulator;
    ASSERT_TRUE(BuildThroughExecute(emulator));

    const Reply list = Execute(emulator, "review", {{"op", "list"}, {"reason", "name"}, {"limit", "1000"}});
    ASSERT_TRUE(list.Ok()) << list.message;
    const std::vector<StateNode>& items = Member(list.body, "items").items;
    ASSERT_FALSE(items.empty()) << list.text;
    for (const StateNode& item : items)
        EXPECT_EQ(Member(item, "reason").s, "name");
    const StateNode& first = items.front();
    const std::string batch = Member(first, "id").s;
    ASSERT_EQ(batch, "N1");
    const std::vector<StateNode>& subjects = Member(first, "subjects").items;
    ASSERT_FALSE(subjects.empty());
    const std::string at = Member(subjects.front(), "at").s;

    const Reply shown = Execute(emulator, "review", {{"op", "show"}, {"id", batch}});
    ASSERT_TRUE(shown.Ok()) << shown.message;
    EXPECT_LE(Member(shown.body, "sheet_bytes").i, static_cast<int64_t>(kNameSheetBytes));
    EXPECT_NE(Member(shown.body, "sheet").s.find(at), std::string::npos);

    // Refusals: no names, not a subject, a bad label, names on a finding
    EXPECT_EQ(Execute(emulator, "review", {{"op", "resolve"}, {"id", batch}}).error, Error::BadRequest);
    EXPECT_EQ(Execute(emulator, "review", {{"op", "resolve"}, {"id", batch}, {"names", R"({"$0001": "Nope"})"}}).error,
              Error::BadRequest);
    EXPECT_EQ(Execute(emulator, "review", {{"op", "resolve"}, {"id", batch}, {"names", "{\"" + at + "\": \"9x\"}"}})
                  .error,
              Error::BadRequest);
    EXPECT_EQ(Execute(emulator, "review", {{"op", "resolve"}, {"id", "F1"}, {"names", "{\"" + at + "\": \"X\"}"}})
                  .error,
              Error::BadRequest);

    const uint64_t open = static_cast<uint64_t>(Member(Member(list.body, "queue"), "open").i);
    const Reply resolved = Execute(
        emulator, "review",
        {{"op", "resolve"}, {"id", batch}, {"names", "{\"" + at + "\": {\"label\": \"FirstName\", \"title\": \"Named\"}}"}});
    ASSERT_TRUE(resolved.Ok()) << resolved.message;
    EXPECT_EQ(static_cast<size_t>(Member(resolved.body, "skipped").i), subjects.size() - 1)
        << "the subjects left out are skipped";
    if (subjects.size() > 1)
    {
        const std::string second = Member(subjects[1], "at").s;
        EXPECT_EQ(Member(resolved.body, "skippedAt").items.size(), subjects.size() - 1);
        EXPECT_NE(resolved.text.find("skipped: " + second), std::string::npos)
            << "the reply lists the skipped addresses: " << resolved.text;
        // Resolved again with another subject only: the first name stays and the reply says so
        const Reply again = Execute(emulator, "review",
                                    {{"op", "resolve"}, {"id", batch}, {"names", "{\"" + second + "\": \"SecondName\"}"}});
        ASSERT_TRUE(again.Ok()) << again.message;
        EXPECT_EQ(Member(again.body, "kept").items.size(), 1u);
        EXPECT_NE(again.text.find("kept: " + at + " FirstName"), std::string::npos) << again.text;
        EXPECT_EQ(static_cast<size_t>(Member(again.body, "skipped").i), subjects.size() - 2);
    }
    const Reply after = Execute(emulator, "review", {{"op", "list"}, {"reason", "name"}, {"limit", "1000"}});
    EXPECT_EQ(static_cast<uint64_t>(Member(Member(after.body, "queue"), "open").i) + 1, open) << "the batch is decided";
    // A second batch may not take the name
    if (items.size() > 1)
    {
        const std::string other = Member(items[1], "id").s;
        const std::string otherAt = Member(Member(items[1], "subjects").items.front(), "at").s;
        EXPECT_EQ(Execute(emulator, "review", {{"op", "resolve"}, {"id", other}, {"names", "{\"" + otherAt + "\": "
                                                                                             "\"FirstName\"}"}})
                      .error,
                  Error::Conflict);
    }
    const Reply skool = Execute(emulator, "export", {{"format", "skool"}, {"ranges", "8000-80FF"}});
    ASSERT_TRUE(skool.Ok()) << skool.message;
    EXPECT_NE(Member(skool.body, "content").s.find("@label=FirstName"), std::string::npos)
        << Member(skool.body, "content").s;
}

TEST_F(AnalysisL3_Test, ReviewEntryCandidatesAndKindChangesReachTheDraft)
{
    Source source;
    ASSERT_TRUE(source.Make("tail-entry.asm"));
    Emulator& emulator = *source.emulator;
    ASSERT_TRUE(BuildThroughExecute(emulator));
    const std::string shared = Hex4Text(source.At("shared"));
    const std::string pre = Hex4Text(source.At("pre"));

    const Reply list = Execute(emulator, "review", {{"op", "list"}, {"reason", "entry"}});
    ASSERT_TRUE(list.Ok()) << list.message;
    std::string sharedId, preId;
    for (const StateNode& item : Member(list.body, "items").items)
    {
        EXPECT_EQ(Member(item, "reason").s, "entry");
        if (Member(item, "start").s == shared)
            sharedId = Member(item, "id").s;
        if (Member(item, "start").s == pre)
            preId = Member(item, "id").s;
    }
    ASSERT_FALSE(sharedId.empty()) << list.text;
    ASSERT_FALSE(preId.empty()) << list.text;
    // The sheet of code never executed: four instructions decoded from the start image
    const Reply shown = Execute(emulator, "review", {{"op", "show"}, {"id", sharedId}});
    const std::string sheet = Member(shown.body, "sheet").s;
    EXPECT_NE(sheet.find("not executed"), std::string::npos) << sheet;
    EXPECT_NE(sheet.find("verdict=ok (a routine starts here"), std::string::npos) << sheet;

    // pre runs inside the routine pass (executed code, one region up to the entry other): accepted, it splits there
    const auto ctl = [&]() { return Member(Execute(emulator, "export", {{"format", "ctl"}, {"ranges", "8000-80FF"}}).body,
                                           "content").s; };
    EXPECT_EQ(ctl().find("\nc " + pre), std::string::npos) << "not an entry before the review";
    ASSERT_TRUE(Execute(emulator, "review", {{"op", "resolve"}, {"id", preId}, {"verdict", "ok"}}).Ok());
    EXPECT_NE(ctl().find("\nc " + pre), std::string::npos) << ctl();
    ASSERT_TRUE(Execute(emulator, "review", {{"op", "resolve"}, {"id", sharedId}, {"verdict", "wrong"}}).Ok());

    // A kind change: the variable n made text, then pre made data
    const std::string n = Hex4Text(source.At("n"));
    const Reply claims = Execute(emulator, "findings", {{"kind", "claim"}, {"range", n.substr(1) + "-" + n.substr(1)}});
    std::string nId;
    for (const StateNode& f : Member(claims.body, "findings").items)
        if (Member(f, "start").s == n)
            nId = Member(f, "id").s;
    ASSERT_FALSE(nId.empty()) << claims.text;
    EXPECT_EQ(Execute(emulator, "review", {{"op", "resolve"}, {"id", nId}, {"kind", "buffer"}}).error, Error::BadRequest);
    const Reply both = Execute(emulator, "review", {{"op", "resolve"}, {"id", nId}, {"verdict", "ok"}, {"kind", "text"}});
    EXPECT_EQ(both.error, Error::BadRequest) << "ok confirms the claim's kind, a kind replaces it";
    EXPECT_NE(both.message.find("verdict wrong with the kind"), std::string::npos) << both.message;
    ASSERT_TRUE(Execute(emulator, "review", {{"op", "resolve"}, {"id", nId}, {"kind", "text"}}).Ok());
    EXPECT_NE(ctl().find("\nt " + n), std::string::npos) << ctl();
    const Reply retyped = Execute(emulator, "findings", {{"kind", "claim"}, {"range", n.substr(1) + "-" + n.substr(1)}});
    for (const StateNode& f : Member(retyped.body, "findings").items)
        if (Member(f, "id").s == nId)
            EXPECT_EQ(Member(f, "status").s, "refuted") << "a kind alone decides the claim: it is not what it said";

    // Paging: offset 1 shows the second item of the same queue
    const Reply all = Execute(emulator, "review", {{"op", "list"}, {"limit", "1000"}});
    const Reply paged = Execute(emulator, "review", {{"op", "list"}, {"offset", "1"}, {"limit", "1000"}});
    const std::vector<StateNode>& a = Member(all.body, "items").items;
    const std::vector<StateNode>& p = Member(paged.body, "items").items;
    ASSERT_GE(a.size(), 2u);
    ASSERT_EQ(p.size() + 1, a.size());
    EXPECT_EQ(Member(p.front(), "id").s, Member(a[1], "id").s);
    EXPECT_EQ(Execute(emulator, "review", {{"op", "list"}, {"reason", "nonsense"}}).error, Error::BadRequest);
}
