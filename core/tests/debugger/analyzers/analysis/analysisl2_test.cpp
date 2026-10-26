/// @file analysisl2_test.cpp
/// @brief L2 of the analysis layers (docs/emulator/analysis/analysis-layers.md, "L2: structure") end to end: a
/// fixture program (testdata/analysis/fixtures/*.asm) runs a few frames under a TTD recording, the replay builds L1,
/// the detectors (detectors/detectors.h) claim regions, and the export (l2export.h) writes the draft. Each detector
/// has a positive fixture and a negative one: the same bytes not used the way the detector looks for give no claim
/// (or one below the weak confidence). Then the region store's rules, the fixed order, the integer confidence, and
/// the export's relocation rules (@isub, @keep, ALIGN 256, #R references, DEFB widths, the ctl round trip through
/// the skoolkit-ctl codec).

#include "pch.h"

#include <algorithm>
#include <fstream>
#include <functional>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "_helpers/emulatortesthelper.h"
#include "_helpers/testpathhelper.h"
#include "base/featuremanager.h"
#include "debugger/analyzers/analysis/detectors/detectors.h"
#include "debugger/analyzers/analysis/l1model.h"
#include "debugger/analyzers/analysis/l2export.h"
#include "debugger/analyzers/analysis/l2gallery.h"
#include "debugger/analyzers/analysis/l2model.h"
#include "debugger/analyzers/analysis/replaytracer.h"
#include "debugger/assembler/z80textassembler.h"
#include "debugger/disassembler/z80disasm.h"
#include "debugger/ttd/timetravelcontroller.h"
#include "emulator/cpu/core.h"
#include "emulator/cpu/z80.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/memory/memory.h"
#include "emulator/slots/slotcontrol.h"
#include "emulator/sound/soundmanager.h"
#include "3rdparty/lodepng/lodepng.h"
#include "unrealasm/symbols/codec.h"

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

struct Built
{
    AsmResult assembled;
    L1Result l1;
    L2Result l2;
};

}  // namespace

class AnalysisL2_Test : public ::testing::Test
{
protected:
    const Built* _built = nullptr;

    /// Assemble, record @p frames frames, replay, run the detectors: once per fixture (the machine, the recording
    /// and the replay are ~25 ms; the tests reading one fixture share them). @p source (non-empty) replaces the
    /// fixture's text (a mutation; @p fixture then only names the cache entry).
    const Built& Run(const std::string& fixture, unsigned frames = 3, bool kempston = false,
                     const std::string& source = std::string())
    {
        const std::string key = fixture + "/" + std::to_string(frames) + (kempston ? "/kempston" : "");
        if (const auto it = Cache().find(key); it != Cache().end())
            return *(_built = &it->second);
        static const Built kEmpty;
        _built = &kEmpty;

        std::function<void(CONFIG&)> slots;
        std::string error;
        if (kempston)
        {
            EXPECT_TRUE(SlotControl::CreateOverride({{"edge.1", "kempston"}}, slots, error)) << error;
        }
        Emulator* emulator = EmulatorTestHelper::CreateStandardEmulator("48K", LoggerLevel::LogError, RamPowerOn::Zero,
                                                                        slots);
        EXPECT_NE(emulator, nullptr);
        if (emulator == nullptr)
            return kEmpty;
        Built built;
        EmulatorContext* context = emulator->GetContext();
        emulator->GetFeatureManager()->setFeature(Features::kDebugMode, true);
        emulator->GetFeatureManager()->setFeature(Features::kTimeTravel, true);
        emulator->GetFeatureManager()->setFeature(Features::kSoundGeneration, false);
        if (context->pSoundManager)
            context->pSoundManager->mute();
        context->pMemory->UpdateFeatureCache();

        built.assembled = Z80TextAssembler().Assemble(source.empty() ? ReadFixture(fixture) : source, 0x8000);
        EXPECT_TRUE(built.assembled.ok) << fixture << ":" << built.assembled.error.line << ": "
                                        << built.assembled.error.message;
        bool ok = built.assembled.ok;
        if (ok)
        {
            for (const AsmSourceLine& line : built.assembled.lines)
                for (size_t i = 0; i < line.bytes.size(); ++i)
                    context->pMemory->DirectWriteToZ80Memory(static_cast<uint16_t>(line.address + i), line.bytes[i]);
            Z80* z80 = context->pCore->GetZ80();
            z80->pc = static_cast<uint16_t>(built.assembled.symbols.at("start"));
            z80->sp = 0x9F00;
            z80->iff1 = z80->iff2 = 0;
            ttd::TimeTravelController* ttd = context->pTimeTravelController;
            ok = ttd != nullptr && ttd->StartRecording();
            EXPECT_TRUE(ok) << (ttd ? ttd->LastStartError() : "no TTD");
        }
        TraceBundle bundle;
        if (ok)
        {
            emulator->RunNFrames(frames);
            ok = TraceBundle::Capture(*emulator, bundle, error);
            EXPECT_TRUE(ok) << error;
        }
        EmulatorTestHelper::CleanupEmulator(emulator);
        if (!ok)
            return kEmpty;
        ReplayOptions options;
        ok = ReplayBuild(bundle, options, built.l1, error);
        EXPECT_TRUE(ok) << error;
        EXPECT_EQ(built.l1.replay.diverged, 0u);
        if (!ok)
            return kEmpty;
        built.l2 = RunDetectors(built.l1);
        return *(_built = &Cache().emplace(key, std::move(built)).first->second);
    }

    static std::map<std::string, Built>& Cache()
    {
        static std::map<std::string, Built> cache;
        return cache;
    }
    static void TearDownTestSuite() { Cache().clear(); }

    uint16_t At(const std::string& label) const
    {
        const auto it = _built->assembled.symbols.find(label);
        EXPECT_NE(it, _built->assembled.symbols.end()) << "no label " << label;
        return it != _built->assembled.symbols.end() ? static_cast<uint16_t>(it->second) : 0;
    }

    /// The claims of @p kind covering @p z80 (any detector)
    std::vector<const Region*> Claims(uint16_t z80, RegionKind kind) const
    {
        std::vector<const Region*> out;
        for (const Region* r : _built->l2.store.ClaimsAt(_built->l1.StartKey(z80)))
            if (r->kind == kind)
                out.push_back(r);
        return out;
    }
    /// The claim of @p kind starting at @p z80 (nullptr: none)
    const Region* StartsAt(uint16_t z80, RegionKind kind, std::optional<DetectorId> detector = std::nullopt) const
    {
        for (const Region& r : _built->l2.store.Regions())
            if (r.z80 == z80 && r.kind == kind && (!detector || r.detector == *detector))
                return &r;
        return nullptr;
    }
    /// No claim of @p kind (at or above the weak confidence) on any byte of [lo, hi]
    bool NoStrongClaim(uint16_t lo, uint16_t hi, RegionKind kind) const
    {
        for (uint32_t a = lo; a <= hi; ++a)
            for (const Region* r : Claims(static_cast<uint16_t>(a), kind))
                if (r->confidence >= confidence::kWeak)
                    return false;
        return true;
    }
    bool IsEntry(uint16_t z80) const
    {
        return std::any_of(_built->l2.entries.begin(), _built->l2.entries.end(),
                           [z80](const auto& e) { return e.first == z80; });
    }
    static bool HasField(const Region& r, uint16_t offset, uint16_t size)
    {
        return std::any_of(r.geometry.fields.begin(), r.geometry.fields.end(),
                           [&](const Field& f) { return f.offset == offset && f.size == size; });
    }

    ExportResult ExportDraft(const std::string& format, uint16_t width = 8,
                             std::vector<ExportRange> ranges = {{0x8000, 0x9FFF}},
                             std::map<uint16_t, ExportNote> notes = {}) const
    {
        static Z80Disassembler disassembler(nullptr);
        ExportOptions options;
        options.ranges = std::move(ranges);
        options.notes = std::move(notes);
        options.defbWidth = width;
        options.disassemble = [](const uint8_t* bytes, uint16_t z80) {
            const std::vector<uint8_t> code(bytes, bytes + 4);
            uint8_t length = 0;
            return disassembler.disassembleSingleCommand(code, z80, &length);
        };
        return Export(_built->l1, _built->l2, format, options);
    }
    static std::vector<std::string> Lines(const std::string& text)
    {
        std::vector<std::string> out;
        std::stringstream in(text);
        std::string line;
        while (std::getline(in, line))
            out.push_back(line);
        return out;
    }
    /// The index of the first line starting with @p prefix at or after @p from (npos: none)
    static size_t Find(const std::vector<std::string>& lines, const std::string& needle, size_t from = 0)
    {
        for (size_t i = from; i < lines.size(); ++i)
            if (lines[i].find(needle) != std::string::npos)
                return i;
        return std::string::npos;
    }
};

// ---------------------------------------------------------------------------------------------------- signatures

TEST_F(AnalysisL2_Test, AnalysisDetectorSignatures_Test)
{
    Run("print-font.asm");
    ASSERT_FALSE(HasFailure());
    // The ROM character set: matched at its address, the whole 768 bytes, labelled
    const Region* charset = StartsAt(0x3D00, RegionKind::Font, DetectorId::Signatures);
    ASSERT_NE(charset, nullptr);
    EXPECT_EQ(charset->Size(), 768u);
    EXPECT_EQ(charset->label, "CHARSET");

    // An entry signature: only where the replay entered a routine (glyph), not its copy never run (glyph_copy)
    const std::vector<Signature> catalog = {{"test.glyph", "test", "GLYPH", "E5 D5 6F 26 00 29 29", 0, 0,
                                             RegionKind::Code, SignatureCheck::Entry, "Draw a glyph"}};
    DetectorOptions options;
    options.catalog = &catalog;
    const L2Result l2 = RunDetectors(_built->l1, options);
    bool atGlyph = false, atCopy = false;
    for (const Region& r : l2.store.Regions())
        if (r.detector == DetectorId::Signatures)
        {
            atGlyph |= r.z80 == At("glyph") && r.label == "GLYPH";
            atCopy |= r.z80 == At("glyph_copy");
        }
    EXPECT_TRUE(atGlyph);
    EXPECT_FALSE(atCopy) << "the same bytes no routine entered";
    EXPECT_TRUE(std::any_of(l2.entries.begin(), l2.entries.end(),
                            [&](const auto& e) { return e.first == At("glyph") && e.second == "GLYPH"; }));
}

// ---------------------------------------------------------------------------------------------------- code map

TEST_F(AnalysisL2_Test, AnalysisDetectorCodeMap_Test)
{
    const Built& b = Run("smc.asm");
    ASSERT_FALSE(HasFailure());
    const CodeLine* patched = b.l2.CodeAt(b.l1.StartKey(At("patched")));
    ASSERT_NE(patched, nullptr);
    EXPECT_TRUE(patched->executed);
    EXPECT_TRUE(patched->smc) << "the operand is written while it is code";
    const CodeLine* never = b.l2.CodeAt(b.l1.StartKey(At("never")));
    ASSERT_NE(never, nullptr) << "the branch never taken is decoded";
    EXPECT_FALSE(never->executed);
    const std::vector<const Region*> inferred = Claims(At("never"), RegionKind::Code);
    ASSERT_EQ(inferred.size(), 1u);
    EXPECT_EQ(inferred.front()->status, RegionStatus::Inferred);
    EXPECT_LT(inferred.front()->confidence, confidence::kMax);
    // Negative: the bytes after the RET are not decoded (a run of zeros is filler)
    for (uint16_t a = At("data"); a < At("data") + 4; ++a)
        EXPECT_EQ(b.l2.CodeAt(b.l1.StartKey(a)), nullptr) << std::hex << a;
    EXPECT_TRUE(Claims(At("data"), RegionKind::Code).empty());
}

TEST_F(AnalysisL2_Test, AnalysisDetectorCodeMap_EntriesAfterJumps)
{
    Run("entry-after-jump.asm");
    ASSERT_FALSE(HasFailure());
    EXPECT_TRUE(IsEntry(At("second"))) << "reached only by a tail jump, after a RET";
    EXPECT_TRUE(IsEntry(At("bump"))) << "reached by a backward branch from a later routine, after a RET";
    EXPECT_FALSE(IsEntry(At("skip"))) << "a forward conditional skip over a JP";
    EXPECT_FALSE(IsEntry(At("loop"))) << "a loop head entered at its test (JR test jumps over it)";
    EXPECT_TRUE(IsEntry(At("first")));
}

TEST_F(AnalysisL2_Test, AnalysisDetectorCodeMap_TailEntries)
{
    const auto candidate = [this](uint16_t z80) -> const EntryCandidate* {
        for (const EntryCandidate& c : _built->l2.entryCandidates)
            if (c.z80 == z80)
                return &c;
        return nullptr;
    };
    Run("tail-entry.asm");
    ASSERT_FALSE(HasFailure());
    EXPECT_TRUE(IsEntry(At("tail"))) << "one conditional JP after a tail jump, closed code";
    EXPECT_EQ(candidate(At("tail")), nullptr);
    // Negative: code that returns or jumps back into the code before it stays inside its routine
    EXPECT_FALSE(IsEntry(At("done")));
    EXPECT_FALSE(IsEntry(At("back")));
    EXPECT_EQ(candidate(At("done")), nullptr);
    EXPECT_EQ(candidate(At("back")), nullptr);
    // Candidates: closed code from two sites, straight code from two sites that runs into a routine
    EXPECT_FALSE(IsEntry(At("shared")));
    ASSERT_NE(candidate(At("shared")), nullptr);
    EXPECT_EQ(candidate(At("shared"))->rule, "tail");
    EXPECT_EQ(candidate(At("shared"))->sources.size(), 2u);
    EXPECT_FALSE(IsEntry(At("pre")));
    ASSERT_NE(candidate(At("pre")), nullptr);
    EXPECT_EQ(candidate(At("pre"))->rule, "prologue");
    EXPECT_EQ(_built->l2.entryCandidates.size(), 2u);
    EXPECT_TRUE(IsEntry(At("other")));

    // Mutation: a RET instead of the tail jump before `tail` (the code before may come back) keeps it a candidate
    std::string mutated = ReadFixture("tail-entry.asm");
    const std::string jump = "        jp other\ntail:";
    const size_t at = mutated.find(jump);
    ASSERT_NE(at, std::string::npos);
    mutated.replace(at, jump.size(), "        ret\ntail:");
    Run("tail-entry.asm#ret", 3, false, mutated);
    ASSERT_FALSE(HasFailure());
    EXPECT_FALSE(IsEntry(At("tail")));
    ASSERT_NE(candidate(At("tail")), nullptr);
    EXPECT_EQ(candidate(At("tail"))->rule, "tail");
}

// ---------------------------------------------------------------------------------------------------- text

TEST_F(AnalysisL2_Test, AnalysisDetectorText_Test)
{
    Run("print-font.asm");
    ASSERT_FALSE(HasFailure());
    const Region* message = StartsAt(At("message"), RegionKind::Text);
    ASSERT_NE(message, nullptr);
    EXPECT_EQ(message->geometry.text, "HELLO");
    EXPECT_EQ(message->geometry.terminator, "bit7");
    EXPECT_EQ(message->geometry.width, 5u);
    EXPECT_EQ(message->Size(), 5u);
    // Negative: the same characters only summed by a routine that draws nothing
    EXPECT_TRUE(NoStrongClaim(At("plain"), static_cast<uint16_t>(At("plain") + 4), RegionKind::Text));
}

TEST_F(AnalysisL2_Test, AnalysisDetectorText_SourceRemnant)
{
    Run("source-remnant.asm");
    ASSERT_FALSE(HasFailure());
    const std::vector<const Region*> unused = Claims(static_cast<uint16_t>(At("remnant") + 8), RegionKind::Unused);
    ASSERT_EQ(unused.size(), 1u);
    EXPECT_GE(unused.front()->geometry.count, 4u);
    EXPECT_NE(unused.front()->geometry.text.find("3970"), std::string::npos) << unused.front()->geometry.text;
    EXPECT_NE(unused.front()->geometry.text.find("CP"), std::string::npos) << unused.front()->geometry.text;
    // Negative: the same bytes read by the code are no remnant
    EXPECT_TRUE(Claims(static_cast<uint16_t>(At("read_copy") + 8), RegionKind::Unused).empty());
}

// ---------------------------------------------------------------------------------------------------- init tables

TEST_F(AnalysisL2_Test, AnalysisDetectorInitTables_Test)
{
    Run("init-rowtable.asm");
    ASSERT_FALSE(HasFailure());
    const Region* rows = StartsAt(At("rows"), RegionKind::InitTable);
    ASSERT_NE(rows, nullptr);
    EXPECT_EQ(rows->geometry.generator, "screen-rows");
    EXPECT_EQ(rows->geometry.stride, 2u);
    EXPECT_EQ(rows->Size(), 384u);
    // Negative: filled by the same loop, never read
    EXPECT_TRUE(NoStrongClaim(At("unread"), static_cast<uint16_t>(At("unread") + 383), RegionKind::InitTable));
}

TEST_F(AnalysisL2_Test, AnalysisDetectorInitTables_PageTable)
{
    Run("page-table.asm");
    ASSERT_FALSE(HasFailure());
    const Region* table = StartsAt(At("table"), RegionKind::InitTable);
    ASSERT_NE(table, nullptr);
    EXPECT_EQ(table->geometry.generator, "x*8");
    EXPECT_EQ(table->Size(), 256u);
    EXPECT_EQ(table->z80 & 0xFF, 0u);
}

// ---------------------------------------------------------------------------------------------------- records

TEST_F(AnalysisL2_Test, AnalysisDetectorRecords_Test)
{
    Run("records-ix.asm");
    ASSERT_FALSE(HasFailure());
    const Region* recs = StartsAt(At("recs"), RegionKind::RecordArray);
    ASSERT_NE(recs, nullptr);
    EXPECT_EQ(recs->geometry.stride, 6u);
    EXPECT_EQ(recs->geometry.count, 4u);
    EXPECT_EQ(recs->Size(), 24u);
    for (uint16_t offset = 0; offset < 6; ++offset)
        EXPECT_TRUE(HasField(*recs, offset, 1)) << "+" << offset;
    EXPECT_EQ(recs->geometry.terminator, "$FF");
    const Region* end = StartsAt(static_cast<uint16_t>(At("recs") + 24), RegionKind::Data, DetectorId::Records);
    ASSERT_NE(end, nullptr);
    EXPECT_EQ(end->title, "terminator");
    // Negative: the same bytes walked with steps 6, 7, 6: the bases sit on no lattice
    EXPECT_TRUE(NoStrongClaim(At("bytes"), static_cast<uint16_t>(At("bytes") + 24), RegionKind::RecordArray));
}

TEST_F(AnalysisL2_Test, AnalysisDetectorRecords_HlWalkWithAWord)
{
    Run("records-hl.asm");
    ASSERT_FALSE(HasFailure());
    const Region* recs = StartsAt(At("recs"), RegionKind::RecordArray);
    ASSERT_NE(recs, nullptr);
    EXPECT_EQ(recs->geometry.stride, 4u);
    EXPECT_EQ(recs->geometry.count, 4u);
    EXPECT_TRUE(HasField(*recs, 0, 1));
    EXPECT_TRUE(HasField(*recs, 1, 2)) << "the word read as LD E,(HL) / INC HL / LD D,(HL)";
    EXPECT_TRUE(HasField(*recs, 3, 1));
    // Negative: only copied by LDIR
    EXPECT_TRUE(NoStrongClaim(At("bytes"), static_cast<uint16_t>(At("bytes") + 16), RegionKind::RecordArray));
}

// ---------------------------------------------------------------------------------------------------- tables

TEST_F(AnalysisL2_Test, AnalysisDetectorPointerTables_Test)
{
    Run("jump-table.asm", 3);
    ASSERT_FALSE(HasFailure());
    const Region* table = StartsAt(At("table"), RegionKind::HandlerTable);
    ASSERT_NE(table, nullptr);
    EXPECT_EQ(table->geometry.count, 4u);
    EXPECT_EQ(table->Size(), 8u);
    for (const char* handler : {"h0", "h1", "h2", "h3"})
        EXPECT_TRUE(IsEntry(At(handler))) << handler;
    // Negative: the same words only summed as bytes
    EXPECT_TRUE(NoStrongClaim(At("copy"), static_cast<uint16_t>(At("copy") + 7), RegionKind::HandlerTable));
    EXPECT_TRUE(NoStrongClaim(At("copy"), static_cast<uint16_t>(At("copy") + 7), RegionKind::PointerTable));
}

TEST_F(AnalysisL2_Test, AnalysisDetectorPointerTables_PushRet)
{
    Run("push-ret.asm");
    ASSERT_FALSE(HasFailure());
    const Region* table = StartsAt(At("table"), RegionKind::HandlerTable);
    ASSERT_NE(table, nullptr);
    EXPECT_EQ(table->geometry.count, 4u);
}

// ---------------------------------------------------------------------------------------------------- variables

TEST_F(AnalysisL2_Test, AnalysisDetectorVariables_Test)
{
    Run("variables.asm");
    ASSERT_FALSE(HasFailure());
    const auto role = [&](const char* label) {
        const Region* r = StartsAt(At(label), RegionKind::Variable);
        return r != nullptr ? r->geometry.role : VarRole::None;
    };
    EXPECT_EQ(role("counter"), VarRole::Counter);
    EXPECT_EQ(role("flag"), VarRole::Flag);
    EXPECT_EQ(role("pointer"), VarRole::Pointer);
    EXPECT_EQ(role("value"), VarRole::Value);
    const Region* pointer = StartsAt(At("pointer"), RegionKind::Variable);
    ASSERT_NE(pointer, nullptr);
    EXPECT_EQ(pointer->Size(), 2u);
    // Negative: the neighbour only in a block copy
    EXPECT_TRUE(Claims(At("block"), RegionKind::Variable).empty());
}

TEST_F(AnalysisL2_Test, AnalysisDetectorVariables_GroupsAndStrides)
{
    Run("variable-groups.asm");
    ASSERT_FALSE(HasFailure());
    const uint16_t area = At("area");
    // Four colour bytes 6 apart: an array of 6-byte records, with the fifth unread record before x
    const Region* colours = StartsAt(area, RegionKind::RecordArray, DetectorId::Variables);
    ASSERT_NE(colours, nullptr);
    EXPECT_EQ(colours->geometry.stride, 6u);
    EXPECT_EQ(colours->geometry.count, 5u);
    EXPECT_EQ(colours->Size(), 30u);
    // x / y / dir used together by move: one record; the timer word (only tick) is not in it
    const Region* group = StartsAt(At("x"), RegionKind::RecordArray, DetectorId::Variables);
    ASSERT_NE(group, nullptr);
    EXPECT_EQ(group->Size(), 3u);
    EXPECT_EQ(group->geometry.fields.size(), 3u);
    EXPECT_TRUE(Claims(At("timer"), RegionKind::RecordArray).empty());
    // Negative: four bytes at uneven gaps: no array, no group
    EXPECT_TRUE(Claims(static_cast<uint16_t>(area + 40), RegionKind::RecordArray).empty());
    EXPECT_TRUE(Claims(static_cast<uint16_t>(area + 49), RegionKind::RecordArray).empty());
    // The buffer's field list takes them
    const Region* buffer = StartsAt(area, RegionKind::Buffer);
    ASSERT_NE(buffer, nullptr);
    EXPECT_TRUE(HasField(*buffer, 0, 30));
    EXPECT_TRUE(HasField(*buffer, 30, 3));
    EXPECT_TRUE(HasField(*buffer, 33, 2));
    // The group's variables follow it, nested in it (x, y, dir); the array's records are not listed one by one
    size_t at = 0;
    while (at < buffer->geometry.fields.size() && buffer->geometry.fields[at].offset != 30)
        ++at;
    ASSERT_LT(at + 3, buffer->geometry.fields.size());
    EXPECT_EQ(buffer->geometry.fields[at].name, "group");
    for (uint16_t k = 0; k < 3; ++k)
    {
        EXPECT_EQ(buffer->geometry.fields[at + 1 + k].offset, 30u + k);
        EXPECT_EQ(buffer->geometry.fields[at + 1 + k].size, 1u);
    }
    EXPECT_EQ(buffer->geometry.fields[at + 4].offset, 33u);
    EXPECT_FALSE(HasField(*buffer, 6, 1));
}

TEST_F(AnalysisL2_Test, BufferFieldsSplitAtANamedPage)
{
    Run("buffer-page.asm");
    ASSERT_FALSE(HasFailure());
    const Region* buffer = StartsAt(0x9000, RegionKind::Buffer);
    ASSERT_NE(buffer, nullptr);
    EXPECT_EQ(buffer->Size(), 512u);
    EXPECT_TRUE(HasField(*buffer, 3, 1));
    EXPECT_TRUE(HasField(*buffer, 5, 1));
    EXPECT_TRUE(HasField(*buffer, 6, 250)) << "the span after +5 ends where the named page starts";
    EXPECT_TRUE(HasField(*buffer, 256, 256));
    // Negative: the first page is not named: no cut inside it
    for (const Field& f : buffer->geometry.fields)
        EXPECT_FALSE(f.offset > 6 && f.offset < 256) << "+" << f.offset;
}

// ---------------------------------------------------------------------------------------------------- the store

TEST(AnalysisRegionStore_Test, RegionStoreKeepsConflicts)
{
    RegionStore store;
    Region code;
    code.lo = MakeKey(2, 0x8000);
    code.hi = MakeKey(2, 0x800F);
    code.z80 = 0x8000;
    code.kind = RegionKind::Code;
    code.detector = DetectorId::CodeMap;
    code.confidence = 1000;
    code.evidence.push_back(Evidence{"executed", 0x8000, 1, ""});
    Region text = code;
    text.lo = MakeKey(2, 0x8008);
    text.z80 = 0x8008;
    text.kind = RegionKind::Text;
    text.detector = DetectorId::Text;
    text.confidence = 400;
    Region records = code;
    records.kind = RegionKind::RecordArray;
    records.detector = DetectorId::Records;
    records.geometry.stride = 4;
    Region records2 = records;
    records2.geometry.stride = 8;
    records2.detector = DetectorId::Variables;

    EXPECT_TRUE(store.Add(text));
    EXPECT_TRUE(store.Add(code));
    EXPECT_TRUE(store.Add(records));
    EXPECT_TRUE(store.Add(records2));
    Region bare = code;
    bare.evidence.clear();
    EXPECT_FALSE(store.Add(bare)) << "a claim needs evidence";
    Region across = code;
    across.hi = MakeKey(5, 0x8001);
    EXPECT_FALSE(store.Add(across)) << "one page per claim";
    Region inverted = code;
    inverted.lo = MakeKey(2, 0x8010);
    inverted.hi = MakeKey(2, 0x8008);
    EXPECT_FALSE(store.Add(inverted));
    Region loud = code;
    loud.confidence = 4000;
    EXPECT_TRUE(store.Add(loud));
    store.Finish();

    // Every claim kept, several per byte
    EXPECT_EQ(store.Size(), 5u);
    EXPECT_EQ(store.ClaimsAt(MakeKey(2, 0x8008)).size(), 5u);
    EXPECT_EQ(store.ClaimsAt(MakeKey(2, 0x8000)).size(), 4u);
    for (const Region& r : store.Regions())
        EXPECT_LE(r.confidence, confidence::kMax);
    // Code vs text, code vs records (x2 code claims), text vs records, records of two steps: all reported
    const std::vector<RegionStore::Conflict> conflicts = store.Conflicts();
    bool codeText = false, steps = false;
    for (const RegionStore::Conflict& c : conflicts)
    {
        const Region& a = store.Regions()[c.a];
        const Region& b = store.Regions()[c.b];
        codeText |= (a.kind == RegionKind::Code && b.kind == RegionKind::Text) ||
                    (a.kind == RegionKind::Text && b.kind == RegionKind::Code);
        steps |= a.kind == RegionKind::RecordArray && b.kind == RegionKind::RecordArray;
    }
    EXPECT_TRUE(codeText);
    EXPECT_TRUE(steps);
    EXPECT_TRUE(store.ClaimedAs(MakeKey(2, 0x8009), RegionKind::Text));
    EXPECT_FALSE(store.ClaimedAs(MakeKey(2, 0x8001), RegionKind::Text));
}

TEST_F(AnalysisL2_Test, DetectorOrderStable)
{
    const Built& b = Run("jump-table.asm", 3);
    ASSERT_FALSE(HasFailure());
    const L2Result again = RunDetectors(b.l1);
    ASSERT_EQ(again.store.Size(), b.l2.store.Size());
    for (size_t i = 0; i < again.store.Size(); ++i)
    {
        const Region& x = again.store.Regions()[i];
        const Region& y = b.l2.store.Regions()[i];
        EXPECT_EQ(x.lo, y.lo);
        EXPECT_EQ(x.hi, y.hi);
        EXPECT_EQ(x.kind, y.kind);
        EXPECT_EQ(x.detector, y.detector);
        EXPECT_EQ(x.confidence, y.confidence);
    }
    // The fixed order: address, then detector, then the claim's own order
    for (size_t i = 1; i < b.l2.store.Size(); ++i)
    {
        const Region& p = b.l2.store.Regions()[i - 1];
        const Region& q = b.l2.store.Regions()[i];
        ASSERT_TRUE(p.lo < q.lo || (p.lo == q.lo && (p.detector < q.detector ||
                                                     (p.detector == q.detector && p.order < q.order))))
            << i;
    }
    EXPECT_EQ(ExportDraft("ctl").text, ExportDraft("ctl").text);
}

TEST(AnalysisConfidence_Test, ConfidenceFixedPoint)
{
    EXPECT_EQ(confidence::Combine(500, 500), 750);
    EXPECT_EQ(confidence::Combine(1000, 3), 1000);
    EXPECT_EQ(confidence::Combine(0, 0), 0);
    EXPECT_EQ(confidence::Combine(700, 300), 790);
    EXPECT_EQ(confidence::Combine(2000, 0), 1000) << "inputs clamp";
    EXPECT_EQ(confidence::Scale(1000, 1, 3), 333);
    EXPECT_EQ(confidence::Scale(900, 2, 0), 0);
    EXPECT_EQ(confidence::FromCount(500, 950, 2, 2), 725);
    EXPECT_EQ(confidence::FromCount(500, 950, 0, 2), 500);
    EXPECT_LT(confidence::FromCount(500, 950, 1000000, 2), 950);
    EXPECT_EQ(confidence::FromCount(800, 600, 9, 1), 800);
    EXPECT_EQ(confidence::Clamp(-5), 0);
    EXPECT_EQ(confidence::Clamp(5000), 1000);
}

// ---------------------------------------------------------------------------------------------------- media

/// Every claim of the media detectors, one a line (the failure message of the media tests)
static std::string MediaClaims(const L2Result& l2)
{
    std::ostringstream out;
    for (const Region& r : l2.store.Regions())
        if (r.detector >= DetectorId::Graphics)
            out << DetectorName(r.detector) << " " << RegionKindName(r.kind) << " $" << std::hex << r.z80 << "-$"
                << r.Z80End() << std::dec << " " << r.geometry.width << "x" << r.geometry.height << "x"
                << r.geometry.frames << " stride " << r.geometry.stride << " prefix " << r.geometry.prefix
                << (r.geometry.mask ? " mask" : "") << " " << r.geometry.layout << " conf " << r.confidence
                << " " << r.label << "\n";
    return out.str();
}

TEST_F(AnalysisL2_Test, AnalysisDetectorSprites_Test)
{
    const Built& b = Run("sprite-16x16.asm");
    ASSERT_FALSE(HasFailure());
    const Region* sprite = StartsAt(At("sprites"), RegionKind::Sprite, DetectorId::Graphics);
    ASSERT_NE(sprite, nullptr) << MediaClaims(b.l2);
    // 2 bytes a row, 16 rows, a mask byte before each graphic byte: 64 bytes a frame, the four frames drawn
    EXPECT_EQ(sprite->geometry.width, 16u) << MediaClaims(b.l2);
    EXPECT_EQ(sprite->geometry.height, 16u);
    EXPECT_TRUE(sprite->geometry.mask);
    EXPECT_EQ(sprite->geometry.stride, 64u);
    EXPECT_EQ(sprite->geometry.frames, 4u);
    EXPECT_EQ(sprite->Size(), 256u);
    EXPECT_EQ(sprite->geometry.layout, "rows");
    EXPECT_GE(sprite->confidence, confidence::kReview);
    // Negative: the same bytes only summed
    EXPECT_TRUE(NoStrongClaim(At("plain"), static_cast<uint16_t>(At("plain") + 63), RegionKind::Sprite))
        << MediaClaims(b.l2);
}

/// The mutation the geometry has to catch: the object loop around the blit runs 3 times; a width (or height) taken
/// from it instead of the row loop's source stride is not 16 x 16, and no claim may span the object records
TEST_F(AnalysisL2_Test, AnalysisDetectorSprites_WidthNotFromTheOuterLoop)
{
    const Built& b = Run("sprite-16x16.asm");
    ASSERT_FALSE(HasFailure());
    size_t sprites = 0;
    for (const Region& r : b.l2.store.Regions())
        if (r.kind == RegionKind::Sprite)
        {
            ++sprites;
            EXPECT_EQ(r.geometry.width, 16u) << MediaClaims(b.l2);
            EXPECT_EQ(r.geometry.height, 16u);
            EXPECT_NE(r.geometry.width, 3u * 8u);
            EXPECT_NE(r.geometry.height, 3u);
        }
    EXPECT_EQ(sprites, 1u) << MediaClaims(b.l2);
    EXPECT_TRUE(Claims(At("objects"), RegionKind::Sprite).empty());
}

TEST_F(AnalysisL2_Test, AnalysisDetectorTiles_Test)
{
    const Built& b = Run("tiles-level.asm", 8);
    ASSERT_FALSE(HasFailure());
    const Region* tile = StartsAt(At("tiles"), RegionKind::Tile, DetectorId::Graphics);
    ASSERT_NE(tile, nullptr) << MediaClaims(b.l2);
    // An attribute, then 8 rows: 9 bytes a tile, the four tiles the map uses
    EXPECT_EQ(tile->geometry.width, 8u) << MediaClaims(b.l2);
    EXPECT_EQ(tile->geometry.height, 8u);
    EXPECT_EQ(tile->geometry.stride, 9u);
    EXPECT_EQ(tile->geometry.prefix, 1u);
    EXPECT_EQ(tile->geometry.frames, 4u);
    EXPECT_FALSE(tile->geometry.mask);
    EXPECT_EQ(tile->Size(), 36u);
    // Negative: the copy only summed
    EXPECT_TRUE(NoStrongClaim(At("plain"), static_cast<uint16_t>(At("plain") + 35), RegionKind::Tile))
        << MediaClaims(b.l2);
    EXPECT_TRUE(NoStrongClaim(At("plain"), static_cast<uint16_t>(At("plain") + 35), RegionKind::Sprite));
}

TEST_F(AnalysisL2_Test, AnalysisDetectorLevelMaps_Test)
{
    const Built& b = Run("tiles-level.asm", 8);
    ASSERT_FALSE(HasFailure());
    const Region* map = StartsAt(At("level"), RegionKind::LevelMap, DetectorId::LevelMaps);
    ASSERT_NE(map, nullptr) << MediaClaims(b.l2);
    EXPECT_EQ(map->Size(), 512u);
    EXPECT_EQ(map->geometry.width, 32u);
    EXPECT_EQ(map->geometry.height, 16u);
    EXPECT_EQ(map->geometry.layout, "cells");
    // Negative: the map's first 64 bytes again, only summed
    EXPECT_TRUE(NoStrongClaim(static_cast<uint16_t>(At("plain") + 36), static_cast<uint16_t>(At("plain") + 99),
                              RegionKind::LevelMap))
        << MediaClaims(b.l2);
}

TEST_F(AnalysisL2_Test, AnalysisDetectorScreens_Test)
{
    const Built& b = Run("tiles-level.asm", 8);
    ASSERT_FALSE(HasFailure());
    const Region* pixels = StartsAt(0xA000, RegionKind::Screen, DetectorId::Screens);
    ASSERT_NE(pixels, nullptr) << MediaClaims(b.l2);
    EXPECT_EQ(pixels->Size(), 4096u);
    EXPECT_EQ(pixels->geometry.width, 256u);
    EXPECT_EQ(pixels->geometry.height, 128u);
    EXPECT_EQ(pixels->geometry.layout, "screen");
    const Region* attributes = StartsAt(0xB000, RegionKind::Screen, DetectorId::Screens);
    ASSERT_NE(attributes, nullptr) << MediaClaims(b.l2);
    EXPECT_EQ(attributes->Size(), 512u);
    EXPECT_EQ(attributes->geometry.width, 32u);
    EXPECT_EQ(attributes->geometry.height, 16u);
    EXPECT_EQ(attributes->geometry.layout, "attributes");
    // Negative: the screen itself is not a back buffer, the map (read by the tile loop) is not a picture
    EXPECT_TRUE(Claims(0x4000, RegionKind::Screen).empty());
    EXPECT_TRUE(NoStrongClaim(At("level"), static_cast<uint16_t>(At("level") + 511), RegionKind::Screen));
}

TEST_F(AnalysisL2_Test, AnalysisDetectorFonts_Test)
{
    const Built& b = Run("print-font.asm");
    ASSERT_FALSE(HasFailure());
    // The glyph loop drew characters of the message from the ROM set: the printable 96 from the text's codes
    const Region* font = StartsAt(0x3D00, RegionKind::Font, DetectorId::Graphics);
    ASSERT_NE(font, nullptr) << MediaClaims(b.l2);
    EXPECT_EQ(font->geometry.width, 8u);
    EXPECT_EQ(font->geometry.height, 8u);
    EXPECT_EQ(font->geometry.frames, 96u);
    EXPECT_EQ(font->Size(), 768u);
    // Negative: the summed copy of the characters is no font
    EXPECT_TRUE(NoStrongClaim(At("plain"), static_cast<uint16_t>(At("plain") + 4), RegionKind::Font));
}

TEST_F(AnalysisL2_Test, AnalysisDetectorRng_Test)
{
    const Built& b = Run("rng.asm");
    ASSERT_FALSE(HasFailure());
    const Region* seed = StartsAt(At("seed"), RegionKind::RngState, DetectorId::Rng);
    ASSERT_NE(seed, nullptr) << MediaClaims(b.l2);
    EXPECT_EQ(seed->Size(), 2u);
    EXPECT_TRUE(std::any_of(b.l2.entries.begin(), b.l2.entries.end(),
                            [&](const auto& e) { return e.first == At("random") && e.second == "RANDOM"; }));
    // Negative: a counter only incremented, the result only stored
    EXPECT_TRUE(Claims(At("counter"), RegionKind::RngState).empty()) << MediaClaims(b.l2);
    EXPECT_TRUE(Claims(At("last"), RegionKind::RngState).empty());
}

TEST_F(AnalysisL2_Test, AnalysisDetectorUnpackers_Test)
{
    const Built& b = Run("unpack-lz.asm");
    ASSERT_FALSE(HasFailure());
    const Region* packed = StartsAt(At("packed"), RegionKind::Packed, DetectorId::Unpackers);
    ASSERT_NE(packed, nullptr) << MediaClaims(b.l2);
    EXPECT_EQ(packed->Size(), static_cast<uint32_t>(At("packed_end") - At("packed")));
    EXPECT_TRUE(std::any_of(b.l2.entries.begin(), b.l2.entries.end(),
                            [&](const auto& e) { return e.first == At("unpack") && e.second == "UNPACK"; }));
    EXPECT_TRUE(std::any_of(b.l2.entries.begin(), b.l2.entries.end(),
                            [&](const auto& e) { return e.first == 0xA000 && e.second == "UNPACKED"; }));
    // Negative: the same bytes block-copied to $B000 and read there
    EXPECT_TRUE(NoStrongClaim(0xB000, 0xB019, RegionKind::Packed)) << MediaClaims(b.l2);
}

TEST_F(AnalysisL2_Test, AnalysisDetectorMusic_Test)
{
    const Built& b = Run("ay-pt3.asm", 4);
    ASSERT_FALSE(HasFailure());
    const Region* player = StartsAt(0xC000, RegionKind::Code, DetectorId::Signatures);
    ASSERT_NE(player, nullptr) << MediaClaims(b.l2);
    EXPECT_EQ(player->label, "VTII_INIT");
    EXPECT_TRUE(std::any_of(b.l2.entries.begin(), b.l2.entries.end(),
                            [](const auto& e) { return e.first == 0xC005 && e.second == "VTII_PLAY"; }));
    const Region* module = StartsAt(0xC86E, RegionKind::MusicData, DetectorId::Music);
    ASSERT_NE(module, nullptr) << MediaClaims(b.l2);
    EXPECT_EQ(module->label, "VTII_MODULE");
    EXPECT_GE(module->Size(), 256u);
    // Negative: the player's first bytes again, only summed: no signature, no music
    for (const Region& r : b.l2.store.Regions())
        EXPECT_FALSE(r.z80 == At("header_copy") && r.detector == DetectorId::Signatures) << r.label;
    EXPECT_TRUE(NoStrongClaim(At("header_copy"), static_cast<uint16_t>(At("header_copy") + 33),
                              RegionKind::MusicData));
}

/// The export states the graphics: an image macro SkoolKit draws (in #UDGTABLE, which skool2asm drops) and the
/// geometry JSON with stride, mask, prefix and layout
TEST_F(AnalysisL2_Test, GraphicsGeometryInTheDraft)
{
    Run("sprite-16x16.asm");
    ASSERT_FALSE(HasFailure());
    const uint16_t sprites = At("sprites");
    const ExportResult skool = ExportDraft("skool");
    ASSERT_TRUE(skool.ok) << skool.error;
    const std::string first = std::to_string(sprites + 1) + "-" + std::to_string(sprites + 3 + 32) + "-2-32";
    const std::string mask = std::to_string(sprites) + "-" + std::to_string(sprites + 2 + 32) + "-2-32";
    EXPECT_NE(skool.text.find("#UDGTABLE { #UDGARRAY2,56,4,4(" + first + ":" + mask + ")"), std::string::npos)
        << skool.text.substr(0, 4000);
    EXPECT_NE(skool.text.find("16x16 sprites, 4 frames, masked"), std::string::npos);
    const ExportResult geometry = ExportDraft("geometry");
    ASSERT_TRUE(geometry.ok) << geometry.error;
    char start[8];
    std::snprintf(start, sizeof(start), "$%04X", sprites);
    EXPECT_NE(geometry.text.find(std::string("\"start\": \"") + start + "\""), std::string::npos) << geometry.text;
    EXPECT_NE(geometry.text.find("\"width\": 16, \"height\": 16, \"frames\": 4, \"stride\": 64, \"mask\": true"),
              std::string::npos)
        << geometry.text;
}

/// The gallery draws what the claims say: the frames in a grid at the scale, the same bytes every time
TEST_F(AnalysisL2_Test, GalleryDrawsTheClaims)
{
    const Built& b = Run("sprite-16x16.asm");
    ASSERT_FALSE(HasFailure());
    const Region* sprite = StartsAt(At("sprites"), RegionKind::Sprite);
    ASSERT_NE(sprite, nullptr);
    const std::vector<const Region*> drawable = GalleryRegions(b.l2);
    EXPECT_NE(std::find(drawable.begin(), drawable.end(), sprite), drawable.end());
    GalleryImage image;
    std::string error;
    ASSERT_TRUE(RenderGallery(b.l1, *sprite, GalleryOptions{}, image, error)) << error;
    // 4 frames of 16 x 16 with a 1-pixel gap around each, zoomed 2 times
    EXPECT_EQ(image.frames, 4u);
    EXPECT_EQ(image.width, (4u * 17u + 1u) * 2u);
    EXPECT_EQ(image.height, 18u * 2u);
    ASSERT_GT(image.png.size(), 8u);
    EXPECT_EQ(image.png[1], 'P');
    std::vector<unsigned char> rgba;
    unsigned w = 0, h = 0;
    ASSERT_EQ(lodepng::decode(rgba, w, h, image.png), 0u);
    EXPECT_EQ(w, image.width);
    // Frame 0 row 0: mask $0E, graphic $F1: pixel 0 is ink (black), pixel 4 is mask-clear and no ink (light grey)
    const auto pixel = [&](unsigned x, unsigned y) {
        const unsigned char* p = rgba.data() + (static_cast<size_t>(y) * w + x) * 4u;
        return static_cast<uint32_t>(p[0]) | (p[1] << 8) | (p[2] << 16);
    };
    EXPECT_EQ(pixel(2, 2), 0x000000u);
    EXPECT_EQ(pixel(2 + 4 * 2, 2), 0xC0C0C0u);
    GalleryImage again;
    ASSERT_TRUE(RenderGallery(b.l1, *sprite, GalleryOptions{}, again, error));
    EXPECT_EQ(again.png, image.png);

    // A screen back buffer and a level map: 256 pixels wide; 8 x 8 cells
    const Built& t = Run("tiles-level.asm", 8);
    ASSERT_FALSE(HasFailure());
    const Region* pixels = StartsAt(0xA000, RegionKind::Screen);
    const Region* map = StartsAt(At("level"), RegionKind::LevelMap);
    ASSERT_NE(pixels, nullptr);
    ASSERT_NE(map, nullptr);
    GalleryOptions one;
    one.scale = 1;
    ASSERT_TRUE(RenderGallery(t.l1, *pixels, one, image, error)) << error;
    EXPECT_EQ(image.width, 256u);
    EXPECT_EQ(image.height, 128u);
    ASSERT_TRUE(RenderGallery(t.l1, *map, one, image, error)) << error;
    EXPECT_EQ(image.width, 32u * 8u);
    EXPECT_EQ(image.height, 16u * 8u);
    // Negative: code is not drawn
    const std::vector<const Region*> claims = Claims(At("start"), RegionKind::Code);
    ASSERT_FALSE(claims.empty());
    EXPECT_FALSE(RenderGallery(t.l1, *claims.front(), one, image, error));
}

// ---------------------------------------------------------------------------------------------------- the export

TEST_F(AnalysisL2_Test, CtlRoundTrip)
{
    const Built& b = Run("jump-table.asm", 3);
    ASSERT_FALSE(HasFailure());
    const ExportResult ctl = ExportDraft("ctl");
    ASSERT_TRUE(ctl.ok) << ctl.error;
    const unrealasm::symbols::ISymbolCodec* codec =
        unrealasm::symbols::SymbolCodecRegistry::Builtin().Find("skoolkit-ctl");
    ASSERT_NE(codec, nullptr);
    const std::vector<uint8_t> bytes(ctl.text.begin(), ctl.text.end());
    const unrealasm::symbols::SymbolDecodeResult decoded = codec->Decode(bytes);
    ASSERT_TRUE(decoded.ok);
    ASSERT_EQ(decoded.file.sets.size(), 1u);
    std::map<uint32_t, std::string> names;
    for (const unrealasm::symbols::Symbol& s : decoded.file.sets.front().symbols)
        names[static_cast<uint32_t>(s.location.offset)] = s.name;

    ExportOptions options;
    options.ranges = {{0x8000, 0x9FFF}};
    const DraftPlan plan = MakePlan(b.l1, b.l2, options);
    ASSERT_FALSE(plan.entries.empty());
    for (const DraftEntry& e : plan.entries)
    {
        const auto it = names.find(e.start);
        ASSERT_NE(it, names.end()) << "no entry at " << std::hex << e.start;
        EXPECT_EQ(it->second, e.label);
    }
    // The symbols export through the same codec gives the same names
    const ExportResult symbols = ExportDraft("symbols");
    ASSERT_TRUE(symbols.ok) << symbols.error;
    EXPECT_NE(symbols.text.find(names.at(At("table"))), std::string::npos);
}

/// A label the review gives an address inside an entry renames the draft's point label there, and the @isub
/// operands that refer to it follow: Relocate wrote them before the review's labels were applied, and a stale name
/// left skool2asm / sjasmplus with "Label not found" (run C2 on Manic Miner)
TEST_F(AnalysisL2_Test, ReviewLabelInsideAnEntryRenamesItsIsubReferences)
{
    Run("buffer-page.asm");
    ASSERT_FALSE(HasFailure());
    // The buffer's second page: a point label inside the buffer's entry, named by LD H,<page>/256
    ExportOptions options;
    options.ranges = {{0x8000, 0x9FFF}};
    const DraftPlan plan = MakePlan(_built->l1, _built->l2, options);
    for (const DraftEntry& e : plan.entries)
        ASSERT_NE(e.start, 0x9100) << "the page must be inside an entry for this test";
    const ExportResult plain = ExportDraft("skool");
    ASSERT_TRUE(plain.ok) << plain.error;
    const auto old = plan.labels.find(0x9100);
    ASSERT_NE(old, plan.labels.end());
    ASSERT_NE(plain.text.find("@isub=LD H," + old->second + "/256"), std::string::npos) << plain.text;

    std::map<uint16_t, ExportNote> notes;
    notes[0x9100].label = "SecondPage";
    const ExportResult named = ExportDraft("skool", 8, {{0x8000, 0x9FFF}}, notes);
    ASSERT_TRUE(named.ok) << named.error;
    EXPECT_NE(named.text.find("@label=SecondPage\n"), std::string::npos) << named.text;
    EXPECT_NE(named.text.find("@isub=LD H,SecondPage/256"), std::string::npos) << named.text;
    for (const std::string& line : Lines(named.text))
        if (line.rfind("@isub=", 0) == 0)
            EXPECT_EQ(line.find(old->second), std::string::npos) << "a stale name: " << line;
}

TEST_F(AnalysisL2_Test, SkoolDraftIsub)
{
    Run("address-vs-constant.asm");
    ASSERT_FALSE(HasFailure());
    const ExportResult skool = ExportDraft("skool");
    ASSERT_TRUE(skool.ok) << skool.error;
    const std::vector<std::string> lines = Lines(skool.text);
    // LD (target+1),A: the operand points inside an instruction: @isub with label+1
    const size_t isub = Find(lines, "@isub=LD (");
    ASSERT_NE(isub, std::string::npos) << skool.text;
    EXPECT_NE(lines[isub].find("+1),A"), std::string::npos) << lines[isub];
    // LD HL,buffer is an address: no @isub or @keep, skool2asm puts the label of the line at buffer there
    const size_t hl = Find(lines, "LD HL,");
    ASSERT_NE(hl, std::string::npos);
    ASSERT_GT(hl, 0u);
    EXPECT_NE(lines[hl - 1].rfind("@isub", 0), 0u) << lines[hl - 1];
    EXPECT_NE(lines[hl - 1], "@keep");
    char buffer[16];
    std::snprintf(buffer, sizeof(buffer), "$%04X", At("buffer"));
    EXPECT_NE(lines[hl].find(std::string("LD HL,") + buffer), std::string::npos) << lines[hl];
    bool lineAtBuffer = false;
    for (const std::string& line : lines)
        lineAtBuffer |= line.size() > 5 && line.compare(1, 5, buffer) == 0;
    EXPECT_TRUE(lineAtBuffer) << skool.text;
    // LD BC with the same value used as a count: @keep, the number stays
    const size_t bc = Find(lines, "LD BC,");
    ASSERT_NE(bc, std::string::npos);
    ASSERT_GT(bc, 0u);
    EXPECT_EQ(lines[bc - 1], "@keep") << lines[bc];
    EXPECT_EQ(lines[bc - 1].find("@isub"), std::string::npos);
    EXPECT_NE(lines[bc].find("LD BC,$"), std::string::npos) << lines[bc];
    // Every other immediate stays as it is (no @isub on plain numbers)
    size_t isubs = 0;
    for (const std::string& line : lines)
        isubs += line.rfind("@isub=", 0) == 0 && line.rfind("@isub=+", 0) != 0 ? 1 : 0;
    EXPECT_EQ(isubs, 1u) << skool.text;
}

TEST_F(AnalysisL2_Test, SkoolDraftAlign)
{
    Run("page-table.asm");
    ASSERT_FALSE(HasFailure());
    const ExportResult skool = ExportDraft("skool");
    ASSERT_TRUE(skool.ok) << skool.error;
    const std::vector<std::string> lines = Lines(skool.text);
    const size_t page = Find(lines, "@isub=LD H,");
    ASSERT_NE(page, std::string::npos) << skool.text;
    EXPECT_NE(lines[page].find("/256"), std::string::npos) << lines[page];
    // ALIGN 256 goes after the instruction before the table (an insertion at an entry start breaks skool2asm -c)
    const size_t align = Find(lines, "@isub=+ALIGN 256");
    ASSERT_NE(align, std::string::npos) << skool.text;
    EXPECT_EQ(Find(lines, "@isub=>"), std::string::npos) << skool.text;
    size_t carrier = align + 1;
    while (carrier < lines.size() && !lines[carrier].empty() && lines[carrier][0] == '@')
        ++carrier;
    ASSERT_LT(carrier, lines.size());
    ASSERT_GT(lines[carrier].size(), 1u);
    EXPECT_EQ(lines[carrier][1], '$') << lines[carrier];
    char at[16];
    std::snprintf(at, sizeof(at), "$%04X", At("table"));
    const size_t table = Find(lines, at, carrier + 1);
    ASSERT_NE(table, std::string::npos);
    for (size_t i = carrier + 1; i < table; ++i)
        EXPECT_TRUE(lines[i].empty() || lines[i][0] == '@' || lines[i][0] == ';') << lines[i];
}

/// A range start after the first one is an ORG after the instruction before it, in '#' hex
TEST_F(AnalysisL2_Test, SkoolDraftOrgAfterAnInstruction)
{
    Run("page-table.asm");
    ASSERT_FALSE(HasFailure());
    const ExportResult skool = ExportDraft("skool", 8, {{0x8000, 0x80FF}, {0x8100, 0x9FFF}});
    ASSERT_TRUE(skool.ok) << skool.error;
    const std::vector<std::string> lines = Lines(skool.text);
    EXPECT_NE(Find(lines, "@org"), std::string::npos) << skool.text;
    EXPECT_NE(Find(lines, "@isub=+ORG #8100"), std::string::npos) << skool.text;
}

TEST(AnalysisExport_Test, CommentAddressesToRefs)
{
    const std::vector<uint16_t> entries = {0x8000, 0x8123};
    EXPECT_EQ(CommentRefs("Used by $8000 and $9000.", entries), "Used by #R$8000 and $9000.");
    EXPECT_EQ(CommentRefs("$8123", entries), "#R$8123");
    EXPECT_EQ(CommentRefs("#R$8000 stays", entries), "#R$8000 stays");
    EXPECT_EQ(CommentRefs("$80001 is no address", entries), "$80001 is no address");
    EXPECT_EQ(CommentRefs("($8000,$8123)", entries), "(#R$8000,#R$8123)");
}

TEST(AnalysisExport_Test, DefbSplitWidth)
{
    using Lines = std::vector<std::pair<uint16_t, uint16_t>>;
    EXPECT_EQ(SplitLines(0x8000, 20, 8, {0x8005}), (Lines{{0x8000, 5}, {0x8005, 8}, {0x800D, 7}}));
    EXPECT_EQ(SplitLines(0x8000, 16, 8, {}), (Lines{{0x8000, 8}, {0x8008, 8}}));
    EXPECT_EQ(SplitLines(0x8000, 3, 8, {0x8000, 0x8003}), (Lines{{0x8000, 3}}));
    EXPECT_EQ(SplitLines(0x8000, 4, 1, {}), (Lines{{0x8000, 1}, {0x8001, 1}, {0x8002, 1}, {0x8003, 1}}));
}

TEST(AnalysisExport_Test, SkoolSpelling)
{
    EXPECT_EQ(SkoolSpelling("ld a,(#8000)"), "LD A,($8000)");
    EXPECT_EQ(SkoolSpelling("ex af,af'"), "EX AF,AF'");
    EXPECT_EQ(SkoolSpelling("ld hx,#12"), "LD IXH,$12");
    EXPECT_EQ(SkoolSpelling("ld ly,a"), "LD IYL,A");
    EXPECT_EQ(SkoolSpelling("defm \"hx;a\""), "DEFM \"hx;a\"");
    EXPECT_EQ(SkoolSpelling("ld a,(ix+#05)"), "LD A,(IX+$05)");
}

TEST_F(AnalysisL2_Test, DefbWidthInTheSkool)
{
    Run("records-hl.asm");
    ASSERT_FALSE(HasFailure());
    for (const uint16_t width : {uint16_t{4}, uint16_t{16}})
    {
        const ExportResult skool = ExportDraft("skool", width);
        ASSERT_TRUE(skool.ok) << skool.error;
        for (const std::string& line : Lines(skool.text))
        {
            const size_t at = line.find("DEFB ");
            if (at == std::string::npos || line[0] == ';')
                continue;
            const std::string list = line.substr(at + 5, line.find(';', at) - at - 5);
            EXPECT_LE(static_cast<size_t>(std::count(list.begin(), list.end(), ',')) + 1, width) << line;
        }
    }
}

// ---------------------------------------------------------------------------------------------------- replay

/// A card plugged into the recording's machine (a Kempston for a game's joystick) is in the analysis instance too:
/// the replay's slot set check passes
TEST_F(AnalysisL2_Test, ReplayKeepsAPluggedCard)
{
    const Built& b = Run("variables.asm", 2, true);
    ASSERT_FALSE(HasFailure());
    EXPECT_EQ(b.l1.replay.diverged, 0u);
    EXPECT_FALSE(b.l1.routines.empty());
}
