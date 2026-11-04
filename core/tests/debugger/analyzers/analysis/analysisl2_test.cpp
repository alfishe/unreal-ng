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
#include <cstdio>
#include <fstream>
#include <functional>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <tuple>
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
#include "debugger/analyzers/analysis/l3findings.h"
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

/// Text printed through a proportional font no detector knows: the codes are the index of their glyphs
TEST_F(AnalysisL2_Test, AnalysisDetectorText_GlyphIndex)
{
    Run("print-proportional.asm", 16);
    ASSERT_FALSE(HasFailure());
    const Region* message = StartsAt(At("message"), RegionKind::Text);
    ASSERT_NE(message, nullptr);
    EXPECT_EQ(message->geometry.text, "PROPORTIONAL TEXT");
    EXPECT_EQ(message->geometry.terminator, "zero");
    EXPECT_TRUE(std::any_of(message->evidence.begin(), message->evidence.end(),
                            [this](const Evidence& e) { return e.what == "glyph" && e.z80 == At("width"); }));
    // The walk over memory read the ROM font (claimed) and the message with one instruction: no glyph reader
    EXPECT_NE(StartsAt(0x3D00, RegionKind::Font), nullptr);
    // Negative: printable tile codes looked up the same way, no words; digits through the font; the glyph bytes the
    // readers read
    EXPECT_TRUE(NoStrongClaim(At("map"), static_cast<uint16_t>(At("map") + 15), RegionKind::Text));
    EXPECT_TRUE(NoStrongClaim(At("score"), static_cast<uint16_t>(At("score") + 5), RegionKind::Text));
    EXPECT_TRUE(NoStrongClaim(0xC100, 0xC1FF, RegionKind::Text));
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

/// Text a routine draws from a buffer the code fills by block copies only: the text is the copied bytes, claimed in
/// the buffer and in each source the replay copied (a copy is all that wrote the buffer, a count in the code ends it)
TEST_F(AnalysisL2_Test, AnalysisDetectorText_InABuffer)
{
    Run("room-buffer.asm", 4);
    ASSERT_FALSE(HasFailure());
    const Region* name = StartsAt(0x9010, RegionKind::Text);
    ASSERT_NE(name, nullptr);
    EXPECT_EQ(name->geometry.width, 16u);
    EXPECT_EQ(name->geometry.terminator, "count-in-code");
    for (const char* room : {"room0", "room1"})
    {
        const auto start = static_cast<uint16_t>(At(room) + 16);
        const std::vector<const Region*> texts = Claims(start, RegionKind::Text);
        EXPECT_TRUE(std::any_of(texts.begin(), texts.end(), [start](const Region* r) {
            return r->z80 == start && r->geometry.width == 16u &&
                   std::any_of(r->evidence.begin(), r->evidence.end(), [](const Evidence& e) { return e.what == "copy"; });
        })) << room;
    }
    // Negative: the room layouts before the names are not printable, no text there
    EXPECT_TRUE(NoStrongClaim(At("room0"), static_cast<uint16_t>(At("room0") + 15), RegionKind::Text));
}

/// One blitter draws the characters (from the ROM set) and a sprite: the sprite's bytes are read by the glyph reader,
/// they are pixels even where they are printable; the message that selects the glyphs is text
TEST_F(AnalysisL2_Test, AnalysisDetectorText_GlyphReaderReadsPixels)
{
    Run("glyph-blit.asm");
    ASSERT_FALSE(HasFailure());
    const Region* message = StartsAt(At("message"), RegionKind::Text);
    ASSERT_NE(message, nullptr);
    EXPECT_EQ(message->geometry.text, "HELLO");
    EXPECT_TRUE(Claims(At("sprite"), RegionKind::Text).empty());
    EXPECT_TRUE(Claims(static_cast<uint16_t>(At("sprite") + 4), RegionKind::Text).empty());
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

/// ROM bytes only written are a sink: the writes are lost, the claim says nothing of the bytes and the ROM's
/// signatures over them hold without a conflict
TEST_F(AnalysisL2_Test, AnalysisDetectorCodeMap_RomWriteSink)
{
    Run("rom-write-sink.asm");
    ASSERT_FALSE(HasFailure());
    for (const uint16_t z80 : {uint16_t{0x0000}, uint16_t{0x0400}})
    {
        const std::vector<const Region*> data = Claims(z80, RegionKind::Data);
        ASSERT_EQ(data.size(), 1u) << z80;
        EXPECT_EQ(data.front()->title, kRomWriteSink) << z80;
        EXPECT_FALSE(Claims(z80, RegionKind::Code).empty()) << "the ROM signature at " << z80;
    }
    const RegionStore& store = _built->l2.store;
    for (const RegionStore::Conflict& c : store.Conflicts())
    {
        const Region& a = store.Regions()[c.a];
        const Region& b = store.Regions()[c.b];
        EXPECT_FALSE(a.title == kRomWriteSink || b.title == kRomWriteSink)
            << RegionTitle(a) << " / " << RegionTitle(b);
    }
    // Negative: a ROM byte read and a RAM byte only written are plain data
    for (const uint16_t z80 : {uint16_t{0x0010}, At("sink")})
    {
        const std::vector<const Region*> data = Claims(z80, RegionKind::Data);
        ASSERT_FALSE(data.empty()) << z80;
        for (const Region* r : data)
            EXPECT_NE(r->title, kRomWriteSink) << z80;
    }
}

/// A record walk by HL that also restarts inside each record (ADD HL to a field, as a hit test does): its bases sit
/// at 16k and 16k + 2, gaps 2 and 14 as often as each other. One array of 16-byte records with the restart's fields
/// shifted into it, not records of 2 bytes
TEST_F(AnalysisL2_Test, AnalysisDetectorRecords_WalkRestartsInsideRecords)
{
    Run("records-hl-restart.asm");
    ASSERT_FALSE(HasFailure());
    const Region* recs = StartsAt(At("recs"), RegionKind::RecordArray);
    ASSERT_NE(recs, nullptr);
    EXPECT_EQ(recs->geometry.stride, 16u);
    EXPECT_EQ(recs->geometry.count, 6u);
    // An HL walk keeps no offsets per base: every restart's offsets land at every residue, so the field at +0 may
    // read as a word too (a known limit); the word read after ADD HL,BC is at its place in the record
    EXPECT_TRUE(std::any_of(recs->geometry.fields.begin(), recs->geometry.fields.end(),
                            [](const Field& f) { return f.offset == 0; }));
    EXPECT_TRUE(HasField(*recs, 2, 2)) << "the word read after ADD HL,BC, at its place in the record";
    for (const Region* r : Claims(At("recs"), RegionKind::RecordArray))
        EXPECT_EQ(r->geometry.stride, 16u) << "no array of the restarts' spacing";
}

/// A list SP walks (LD SP,list / POP HL / LD (HL),A): its words are the addresses written, a pointer table; the
/// stack's own PUSH / POP leaves none
TEST_F(AnalysisL2_Test, AnalysisDetectorPointerTables_ListPoppedBySp)
{
    Run("pop-list.asm");
    ASSERT_FALSE(HasFailure());
    const Region* table = StartsAt(At("list"), RegionKind::PointerTable);
    ASSERT_NE(table, nullptr) << "the words POP HL took and LD (HL),A wrote through";
    EXPECT_EQ(table->geometry.count, 6u);
    // Negative: the stack below $9F00 holds no table
    for (uint16_t z80 = 0x9EF0; z80 < 0x9F00; ++z80)
    {
        EXPECT_TRUE(Claims(z80, RegionKind::PointerTable).empty()) << z80;
        EXPECT_TRUE(Claims(z80, RegionKind::HandlerTable).empty()) << z80;
    }
}

/// Lists of words with a hole between them: the bases' period (8) has three of its four word slots taken. Records of
/// 2 bytes, as the walk reads them; no fold into 8-byte records
TEST_F(AnalysisL2_Test, AnalysisDetectorRecords_WordListsAreNoRestarts)
{
    Run("records-word-lists.asm");
    ASSERT_FALSE(HasFailure());
    const std::vector<const Region*> recs = Claims(At("lists"), RegionKind::RecordArray);
    ASSERT_FALSE(recs.empty());
    for (const Region* r : recs)
        EXPECT_EQ(r->geometry.stride, 2u) << RegionTitle(*r);
}

/// A buffer with text: one more source on the step of the sources the replay copied, where the text is printable
/// too (inferred); the lattice stops at code (below) and at a slot whose text offsets hold no text (above)
TEST_F(AnalysisL2_Test, AnalysisDetectorRecords_LatticeSources)
{
    Run("room-buffer.asm", 4);
    ASSERT_FALSE(HasFailure());
    const Region* buffer = StartsAt(0x9000, RegionKind::Buffer);
    ASSERT_NE(buffer, nullptr);
    EXPECT_EQ(buffer->geometry.stride, 32u);
    EXPECT_EQ(buffer->geometry.instances, (std::vector<uint16_t>{At("room0"), At("room1"), At("room2")}));
    EXPECT_TRUE(std::any_of(buffer->evidence.begin(), buffer->evidence.end(),
                            [this](const Evidence& e) { return e.what == "lattice" && e.z80 == At("room2"); }));
    const Region* inferred = StartsAt(static_cast<uint16_t>(At("room2") + 16), RegionKind::Text);
    ASSERT_NE(inferred, nullptr) << "the buffer's text, projected into the inferred source";
    EXPECT_EQ(inferred->status, RegionStatus::Inferred);
    EXPECT_LT(inferred->confidence, confidence::kMax);
    // Negative: the zeros after room2 are no source
    EXPECT_TRUE(Claims(At("after"), RegionKind::Text).empty());
}

/// Each source of a buffer is an entry of its own in the draft, numbered from 0 and titled by its text
TEST_F(AnalysisL2_Test, SourceEntriesInTheDraft)
{
    Run("room-buffer.asm", 4);
    ASSERT_FALSE(HasFailure());
    const ExportResult ctl = ExportDraft("ctl");
    ASSERT_TRUE(ctl.ok) << ctl.error;
    const std::vector<std::string> lines = Lines(ctl.text);
    const struct
    {
        const char* label;
        const char* title;
    } sources[] = {{"room0", "Source 0 of the buffer at $9000: The Hall"},
                   {"room1", "Source 1 of the buffer at $9000: The Kitchen"},
                   {"room2", "Source 2 of the buffer at $9000: The Cellar"}};
    for (const auto& source : sources)
    {
        char entry[16];
        std::snprintf(entry, sizeof(entry), "b $%04X ", At(source.label));
        const size_t line = Find(lines, entry);
        ASSERT_NE(line, std::string::npos) << source.label << "\n" << ctl.text;
        EXPECT_NE(lines[line].find(source.title), std::string::npos) << lines[line];
    }
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

/// Untouched says no access reached the bytes, not what they are: a claim from their content holds over it. Text
/// is data: a data claim may hold it. Code over data or text still conflicts
TEST(AnalysisRegionStore_Test, UntouchedAndDataHoldContentClaims)
{
    const auto claim = [](uint16_t lo, uint16_t hi, RegionKind kind, DetectorId detector) {
        Region r;
        r.lo = MakeKey(2, lo);
        r.hi = MakeKey(2, hi);
        r.z80 = lo;
        r.kind = kind;
        r.detector = detector;
        r.confidence = 800;
        r.evidence.push_back(Evidence{"test", lo, 1, ""});
        return r;
    };
    const auto conflicts = [](std::initializer_list<Region> regions) {
        RegionStore store;
        for (const Region& r : regions)
            EXPECT_TRUE(store.Add(r));
        store.Finish();
        return store.Conflicts().size();
    };
    EXPECT_EQ(conflicts({claim(0x8000, 0x80FF, RegionKind::Untouched, DetectorId::CodeMap),
                         claim(0x8010, 0x8017, RegionKind::Code, DetectorId::Signatures)}),
              0u)
        << "a signature over bytes the replay never reached";
    EXPECT_EQ(conflicts({claim(0x8000, 0x80FF, RegionKind::Untouched, DetectorId::CodeMap),
                         claim(0x8010, 0x8017, RegionKind::Text, DetectorId::Text)}),
              0u);
    EXPECT_EQ(conflicts({claim(0x8000, 0x8073, RegionKind::Data, DetectorId::CodeMap),
                         claim(0x8010, 0x801C, RegionKind::Text, DetectorId::Text)}),
              0u)
        << "strings inside a block read as data";
    // Negative: code over data and code over text stay conflicts
    EXPECT_EQ(conflicts({claim(0x8000, 0x8073, RegionKind::Data, DetectorId::CodeMap),
                         claim(0x8010, 0x8017, RegionKind::Code, DetectorId::Signatures)}),
              1u);
    EXPECT_EQ(conflicts({claim(0x8000, 0x8073, RegionKind::Text, DetectorId::Text),
                         claim(0x8010, 0x8017, RegionKind::Code, DetectorId::Signatures)}),
              1u);
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

/// The sprite fixture draws unsynchronised to the frame: an animation frame has to be on the screen at two frame ends
/// before its bytes' neighbours count, which takes about 10 frames
constexpr unsigned kSpriteFrames = 10;

TEST_F(AnalysisL2_Test, AnalysisDetectorSprites_Test)
{
    const Built& b = Run("sprite-16x16.asm", kSpriteFrames);
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

/// The same sprites masked into a back buffer the code puts on the screen with plain loads and stores: the copy carries
/// the mask ANDed into each buffer byte with the graphic byte, so the screen shows a masked 16 x 16 sprite
TEST_F(AnalysisL2_Test, AnalysisDetectorSprites_MaskCarriedThroughBackBuffer)
{
    const Built& b = Run("sprite-drawn-buffer.asm", kSpriteFrames);
    ASSERT_FALSE(HasFailure());
    const Region* sprite = StartsAt(At("sprites"), RegionKind::Sprite, DetectorId::Graphics);
    ASSERT_NE(sprite, nullptr) << MediaClaims(b.l2);
    EXPECT_EQ(sprite->geometry.width, 16u) << MediaClaims(b.l2);
    EXPECT_EQ(sprite->geometry.height, 16u);
    EXPECT_TRUE(sprite->geometry.mask);
    EXPECT_EQ(sprite->geometry.stride, 64u);
    const Region* buffer = StartsAt(At("buffer"), RegionKind::Screen, DetectorId::Screens);
    ASSERT_NE(buffer, nullptr) << MediaClaims(b.l2);
    EXPECT_EQ(buffer->geometry.layout, "drawn");
}

/// The same sprites stored bottom line first and drawn up the screen: one claim from the frames' first byte, 16 x 16
/// with the mask, marked bottom up (the gallery and the skool picture flip it)
TEST_F(AnalysisL2_Test, AnalysisDetectorSprites_StoredBottomUp)
{
    const Built& b = Run("sprite-bottom-up.asm", kSpriteFrames);
    ASSERT_FALSE(HasFailure());
    const Region* sprite = StartsAt(At("sprites"), RegionKind::Sprite, DetectorId::Graphics);
    ASSERT_NE(sprite, nullptr) << MediaClaims(b.l2);
    EXPECT_EQ(sprite->geometry.width, 16u) << MediaClaims(b.l2);
    EXPECT_EQ(sprite->geometry.height, 16u);
    EXPECT_TRUE(sprite->geometry.mask);
    EXPECT_EQ(sprite->geometry.stride, 64u);
    EXPECT_EQ(sprite->geometry.frames, 4u);
    EXPECT_TRUE(sprite->geometry.bottomUp);
}

/// A message printed by a glyph loop, and its bytes also copied down a screen column: text, no sprite
TEST_F(AnalysisL2_Test, AnalysisDetectorGraphics_TextIsNoPicture)
{
    const Built& b = Run("text-as-pixels.asm", kSpriteFrames);
    ASSERT_FALSE(HasFailure());
    EXPECT_FALSE(Claims(At("message"), RegionKind::Text).empty()) << MediaClaims(b.l2);
    for (const RegionKind kind : {RegionKind::Sprite, RegionKind::Tile})
        for (const Region* r : Claims(At("message"), kind))
            EXPECT_NE(r->detector, DetectorId::Graphics) << "the message's bytes drawn as pixels: " << r->title;
}

/// Five sprite blocks named by one table, block c never drawn: inferred as its drawn neighbours' sprite
TEST_F(AnalysisL2_Test, AnalysisDetectorSprites_PointerTableBlockNeverDrawn)
{
    const Built& b = Run("sprite-pointer-blocks.asm", kSpriteFrames);
    ASSERT_FALSE(HasFailure());
    ASSERT_NE(StartsAt(At("block_b"), RegionKind::Sprite, DetectorId::Graphics), nullptr) << MediaClaims(b.l2);
    const Region* c = StartsAt(At("block_c"), RegionKind::Sprite, DetectorId::Graphics);
    ASSERT_NE(c, nullptr) << MediaClaims(b.l2);
    EXPECT_EQ(c->status, RegionStatus::Inferred);
    EXPECT_EQ(c->geometry.width, 16u);
    EXPECT_EQ(c->geometry.height, 16u);
    EXPECT_TRUE(c->geometry.mask);
    EXPECT_EQ(c->Z80End(), At("block_d") - 1u);
}

/// The mutation the geometry has to catch: the object loop around the blit runs 3 times; a width (or height) taken
/// from it instead of the row loop's source stride is not 16 x 16, and no claim may span the object records
TEST_F(AnalysisL2_Test, AnalysisDetectorSprites_WidthNotFromTheOuterLoop)
{
    const Built& b = Run("sprite-16x16.asm", kSpriteFrames);
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

/// Tiles stored a pixel row per page pair (picture, then its mask a page up), as Skool Daze and its kin keep them: one
/// set with the picture page's pitch, a byte from tile to tile, the mask page no tile of its own
TEST_F(AnalysisL2_Test, AnalysisDetectorTiles_PageStrided)
{
    const Built& b = Run("strided-tiles.asm", 4);
    ASSERT_FALSE(HasFailure());
    const Region* set = nullptr;
    for (const Region& r : b.l2.store.Regions())
        if (r.z80 == At("tiles") && r.detector == DetectorId::Graphics &&
            (r.kind == RegionKind::Tile || r.kind == RegionKind::Sprite))
            set = &r;
    ASSERT_NE(set, nullptr) << MediaClaims(b.l2);
    EXPECT_EQ(set->geometry.width, 8u) << MediaClaims(b.l2);
    EXPECT_EQ(set->geometry.height, 8u);
    EXPECT_EQ(set->geometry.pitch, 512u);
    EXPECT_EQ(set->geometry.stride, 1u);
    EXPECT_EQ(set->geometry.frames, 6u);
    EXPECT_TRUE(StartsAt(static_cast<uint16_t>(At("tiles") + 0x100), RegionKind::Tile) == nullptr &&
                StartsAt(static_cast<uint16_t>(At("tiles") + 0x100), RegionKind::Sprite) == nullptr)
        << MediaClaims(b.l2);
}

/// A sprite bank (Dizzy's): records of a header (width x 2, height) and the rows. Every record is claimed, in runs of
/// one size: the one before the first drawn (named by the walk's LD HL,bank), the ones after the last; the glyph
/// records printed side by side are a font, with no text to name them. A second bank in another format (the height and
/// a flag byte, every record one width, a mask byte before each graphic byte) is found too
TEST_F(AnalysisL2_Test, AnalysisDetectorGraphics_SpriteBank)
{
    const Built& b = Run("sprite-bank.asm");
    ASSERT_FALSE(HasFailure());
    const auto hex = [](uint16_t v) {
        char text[8];
        std::snprintf(text, sizeof(text), "$%04X", v);
        return std::string(text);
    };
    const auto run = [&](uint16_t at, RegionKind kind) {
        const Region* r = StartsAt(at, kind, DetectorId::Graphics);
        EXPECT_NE(r, nullptr) << hex(at) << "\n" << MediaClaims(b.l2);
        return r;
    };
    const Region* hero = run(At("bank"), RegionKind::Sprite);
    const Region* font = run(At("glyphs"), RegionKind::Font);
    const Region* big = run(At("big"), RegionKind::Sprite);
    const Region* boxes = run(At("boxes"), RegionKind::Sprite);
    ASSERT_TRUE(hero && font && big && boxes);
    for (const auto& [r, width, height, frames] :
         {std::tuple{hero, 16u, 16u, 3u}, {font, 8u, 8u, 12u}, {big, 24u, 24u, 2u}, {boxes, 32u, 12u, 2u}})
    {
        EXPECT_EQ(r->geometry.width, width) << hex(r->z80);
        EXPECT_EQ(r->geometry.height, height) << hex(r->z80);
        EXPECT_EQ(r->geometry.frames, frames) << hex(r->z80);
        EXPECT_EQ(r->geometry.prefix, 2u) << hex(r->z80);
        EXPECT_EQ(r->geometry.stride, 2u + width / 8 * height) << hex(r->z80);
        EXPECT_EQ(r->Size(), frames * r->geometry.stride) << hex(r->z80);
    }
    EXPECT_EQ(hero->status, RegionStatus::Observed);
    EXPECT_EQ(boxes->status, RegionStatus::Inferred);
    // The second bank: a header of the height and a flag, one width, a mask byte before each graphic byte
    const Region* masked = run(At("heroes"), RegionKind::Sprite);
    ASSERT_NE(masked, nullptr);
    EXPECT_EQ(masked->geometry.width, 16u);
    EXPECT_EQ(masked->geometry.height, 8u);
    EXPECT_EQ(masked->geometry.frames, 10u);
    EXPECT_TRUE(masked->geometry.mask);
    EXPECT_EQ(masked->geometry.prefix, 2u);
    EXPECT_EQ(masked->geometry.stride, 34u);
    EXPECT_EQ(masked->Size(), 340u);
    EXPECT_EQ(masked->status, RegionStatus::Observed);
    // The frames drawn are the bank's: no claim of its own starts at a record's rows
    for (const uint16_t at : {static_cast<uint16_t>(At("hero") + 2), static_cast<uint16_t>(At("glyphs") + 12)})
        for (const RegionKind kind : {RegionKind::Sprite, RegionKind::Font, RegionKind::Tile})
            EXPECT_EQ(StartsAt(at, kind, DetectorId::Graphics), nullptr) << hex(at) << "\n" << MediaClaims(b.l2);
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

/// Tiles on the lattice of the text's codes (Hungry Horace's maze tiles): the walk that picked them read a map, no
/// text, so they are tiles, not a font
TEST_F(AnalysisL2_Test, AnalysisDetectorFonts_TilesPickedByAMap)
{
    const Built& b = Run("print-tiles.asm", 8);
    ASSERT_FALSE(HasFailure());
    const uint16_t tiles = At("tiles");
    for (const Region& r : b.l2.store.Regions())
        EXPECT_FALSE(r.kind == RegionKind::Font && r.z80 <= tiles + 47 && tiles <= r.Z80End())
            << "a font over the tiles at $" << std::hex << r.z80 << "\n" << MediaClaims(b.l2);
    EXPECT_NE(StartsAt(tiles, RegionKind::Tile, DetectorId::Graphics), nullptr) << MediaClaims(b.l2);
    EXPECT_NE(StartsAt(At("message"), RegionKind::Text), nullptr) << "the font rule needs the text";
}

/// Two maps side by side, one walk of the map loop each: two maps of the walk's size, not one map over both
TEST_F(AnalysisL2_Test, AnalysisDetectorLevelMaps_SideBySide)
{
    const Built& b = Run("maps-side.asm", 8);
    ASSERT_FALSE(HasFailure());
    for (const char* name : {"level0", "level1"})
    {
        const Region* map = StartsAt(At(name), RegionKind::LevelMap, DetectorId::LevelMaps);
        ASSERT_NE(map, nullptr) << name << "\n" << MediaClaims(b.l2);
        EXPECT_EQ(map->Size(), 256u) << name;
        EXPECT_EQ(map->geometry.width, 32u) << name;
        EXPECT_EQ(map->geometry.height, 8u) << name;
    }
}

/// One map in two walks drawn to different places (the halves of a screen): one map, not two
TEST_F(AnalysisL2_Test, AnalysisDetectorLevelMaps_HalvesAreOneMap)
{
    const Built& b = Run("map-halves.asm", 8);
    ASSERT_FALSE(HasFailure());
    const Region* map = StartsAt(At("level0"), RegionKind::LevelMap, DetectorId::LevelMaps);
    ASSERT_NE(map, nullptr) << MediaClaims(b.l2);
    EXPECT_EQ(map->Size(), 512u);
    EXPECT_EQ(map->geometry.width, 32u);
    EXPECT_EQ(map->geometry.height, 16u);
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

/// The game's own font, no ROM printing: the glyph blit puts the message on a print path, the message's codes name the
/// font; the glyph bytes the blit read are pixels, not text
TEST_F(AnalysisL2_Test, AnalysisDetectorFonts_OwnFontThroughTheGlyphBlit)
{
    const Built& b = Run("print-own-font.asm");
    ASSERT_FALSE(HasFailure());
    const Region* text = StartsAt(At("message"), RegionKind::Text);
    ASSERT_NE(text, nullptr) << MediaClaims(b.l2);
    EXPECT_EQ(text->geometry.text, "PRESS FIRE");
    // The glyphs drawn, "I" to "S" at least (the space is blank): the rest of the set is a guess the claim leaves out
    const auto glyph = [&](char c) { return static_cast<uint16_t>(At("font") + (c - 32) * 8); };
    const Region* font = nullptr;
    for (const Region* r : Claims(glyph('S'), RegionKind::Font))
        if (r->detector == DetectorId::Graphics)
            font = r;
    ASSERT_NE(font, nullptr) << MediaClaims(b.l2);
    EXPECT_LE(font->z80, glyph('I')) << MediaClaims(b.l2);
    EXPECT_GE(font->z80, At("font"));
    EXPECT_EQ((font->z80 - At("font")) % 8, 0);
    EXPECT_LE(font->Z80End(), At("font") + 767u);
    EXPECT_EQ(font->geometry.stride, 8u);
    EXPECT_TRUE(NoStrongClaim(At("font"), static_cast<uint16_t>(At("font") + 767), RegionKind::Text)) << MediaClaims(b.l2);
}

/// A back buffer put on the screen with plain loads and stores (no block copy): the bytes a screen writer reads and the
/// code rewrites are one back buffer; a picture drawn the same way but never written is none
TEST_F(AnalysisL2_Test, AnalysisDetectorScreens_DrawnBackBuffer)
{
    const Built& b = Run("drawn-back-buffer.asm", 20);
    ASSERT_FALSE(HasFailure());
    const Region* buffer = StartsAt(At("buffer"), RegionKind::Screen, DetectorId::Screens);
    ASSERT_NE(buffer, nullptr) << MediaClaims(b.l2);
    EXPECT_EQ(buffer->Z80End(), At("buffer") + 2047u);
    EXPECT_EQ(buffer->geometry.layout, "drawn");
    for (const Region* r : Claims(At("picture"), RegionKind::Screen))
        EXPECT_NE(r->detector, DetectorId::Screens) << "a picture never written is no back buffer: " << r->title;
}

/// Tiles block-copied into a column back buffer (one LDI chain a tile) that plain loads put on the screen: the tile set
/// is one claim of 8 tiles one byte wide and 8 lines high, the never-copied tile 4 inside it, the data after it out
TEST_F(AnalysisL2_Test, AnalysisDetectorTiles_CopiedToADrawnBuffer)
{
    // The value flow follows the copy to the screen, or ends in the buffer (a scroll rewrites its bytes); a tile never
    // copied but read by other code still joins the set
    for (const char* fixture : {"tiles-copied.asm", "tiles-copied-shifted.asm", "tiles-copied-read.asm"})
    {
        SCOPED_TRACE(fixture);
        const Built& b = Run(fixture, 30);
        ASSERT_FALSE(HasFailure());
        const Region* tiles = StartsAt(At("tiles"), RegionKind::Tile, DetectorId::Graphics);
        ASSERT_NE(tiles, nullptr) << MediaClaims(b.l2);
        EXPECT_EQ(tiles->Z80End(), At("tiles") + 63u) << MediaClaims(b.l2);
        EXPECT_EQ(tiles->geometry.width, 8u);
        EXPECT_EQ(tiles->geometry.height, 8u);
        EXPECT_EQ(tiles->geometry.frames, 8u);
        EXPECT_FALSE(tiles->geometry.bottomUp);
        for (const Region* r : Claims(At("after"), RegionKind::Tile))
            EXPECT_NE(r->detector, DetectorId::Graphics) << "the data after the set is no tile: " << r->title;
        for (const Region* r : Claims(At("tiles"), RegionKind::Sprite))
            EXPECT_NE(r->detector, DetectorId::Graphics) << "a tile is no sprite: " << r->title;
    }
}

/// The map whose bytes pick the copied tiles (each byte's value times 8 is where the LDI chain read its tile): L1 records
/// the map bytes as the index of the block reads, and the level map detector claims the map, 24 cells (a buffer column) a
/// row
TEST_F(AnalysisL2_Test, AnalysisDataFlow_IndexOfABlockRead)
{
    const Built& b = Run("tiles-copied-map.asm", 30);
    ASSERT_FALSE(HasFailure());
    const uint16_t map = At("map");
    const uint16_t tiles = At("tiles");
    size_t selectors = 0;
    for (const IndexUse& x : b.l1.indexes)
        if (x.selectorZ80 >= map && x.selectorZ80 < map + 264 && x.lo >= tiles && x.hi < tiles + 64)
            ++selectors;
    EXPECT_GE(selectors, 200u) << "the map's bytes index the tile reads";
    const Region* m = StartsAt(map, RegionKind::LevelMap, DetectorId::LevelMaps);
    ASSERT_NE(m, nullptr) << MediaClaims(b.l2);
    EXPECT_EQ(m->Z80End(), map + 263u) << MediaClaims(b.l2);
    EXPECT_EQ(m->geometry.width, 24u) << "a column of the buffer, the copying loop's 24 trips";
    EXPECT_EQ(m->geometry.height, 11u);
    for (const Region* r : Claims(At("after"), RegionKind::LevelMap))
        EXPECT_NE(r->detector, DetectorId::LevelMaps) << "the data after the map is none of it: " << r->title;
}

/// A string kept in the slot of a character no text prints: read by the print loop, never drawn as a glyph. The font
/// claim leaves that slot out, so the text claim there meets no font (no conflict); the glyphs around it stay the font
TEST_F(AnalysisL2_Test, AnalysisDetectorFonts_StringInAnUnusedSlot)
{
    const Built& b = Run("font-slot-text.asm");
    ASSERT_FALSE(HasFailure());
    const auto glyph = [&](char c) { return static_cast<uint16_t>(At("font") + (c - 32) * 8); };
    const Region* text = StartsAt(At("slot_text"), RegionKind::Text);
    ASSERT_NE(text, nullptr) << MediaClaims(b.l2);
    EXPECT_EQ(text->geometry.text, "AUTO");
    bool below = false, above = false;
    for (const char c : {'E', 'S'})
        for (const Region* r : Claims(glyph(c), RegionKind::Font))
            if (r->detector == DetectorId::Graphics)
                (c == 'E' ? below : above) = true;
    EXPECT_TRUE(below && above) << "the glyphs on both sides of the slot are the font: " << MediaClaims(b.l2);
    for (uint16_t at = glyph('K'); at < glyph('L'); ++at)
        for (const Region* r : Claims(at, RegionKind::Font))
            EXPECT_NE(r->detector, DetectorId::Graphics) << "the slot holds a string: " << r->title;
    bool mark = false;
    for (const Region* r : Claims(glyph('M'), RegionKind::Font))
        mark |= r->detector == DetectorId::Graphics;
    EXPECT_TRUE(mark) << "a slot another loop drew is a glyph: " << MediaClaims(b.l2);
    // The glyphs after the last one printed, never read nor drawn, are the font too, up to 8 slots on (the object's
    // frames run to 'U': the font ends at ']', '_' is past it)
    bool z = false, past = false;
    for (const Region* r : Claims(glyph('Z'), RegionKind::Font))
        z |= r->detector == DetectorId::Graphics;
    for (const Region* r : Claims(glyph('_'), RegionKind::Font))
        past |= r->detector == DetectorId::Graphics;
    EXPECT_TRUE(z) << "'Z', never printed, is the next glyph of the font: " << MediaClaims(b.l2);
    std::string fonts;
    for (const Region& r : b.l2.store.Regions())
        if (r.kind == RegionKind::Font)
            fonts += std::to_string(r.z80) + "-" + std::to_string(r.Z80End()) + " " + r.title + "\n";
    EXPECT_FALSE(past) << "no more than 8 unprinted glyphs: " << fonts;
}

/// The ROM's character set printed by the game's own glyph loop from codes that are no text (each plus $60): the font is
/// the ROM's 96 characters
TEST_F(AnalysisL2_Test, AnalysisDetectorFonts_RomSetThroughOwnLoop)
{
    const Built& b = Run("rom-font-own-print.asm");
    ASSERT_FALSE(HasFailure());
    const Region* font = StartsAt(0x3D00, RegionKind::Font, DetectorId::Graphics);
    ASSERT_NE(font, nullptr) << MediaClaims(b.l2);
    EXPECT_EQ(font->Z80End(), 0x3FFF);
    EXPECT_EQ(font->geometry.frames, 96u);
    EXPECT_EQ(font->geometry.stride, 8u);
    EXPECT_TRUE(NoStrongClaim(At("message"), static_cast<uint16_t>(At("message") + 15), RegionKind::Text))
        << MediaClaims(b.l2);
}

/// Glyphs drawn with codes in one contiguous run (0-3) are user graphics, not a character set
TEST_F(AnalysisL2_Test, AnalysisDetectorFonts_ContiguousCodesAreNoFont)
{
    const Built& b = Run("udg-run.asm");
    ASSERT_FALSE(HasFailure());
    EXPECT_TRUE(NoStrongClaim(At("udgs"), static_cast<uint16_t>(At("udgs") + 31), RegionKind::Font)) << MediaClaims(b.l2);
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

/// A routine that plays the AY and calls the drawing: its tune is music, the picture the drawing callee reads is not
TEST_F(AnalysisL2_Test, AnalysisDetectorMusic_PlayerThatDraws)
{
    const Built& b = Run("ay-and-draw.asm", 4);
    ASSERT_FALSE(HasFailure());
    bool tune = false;
    for (const Region& r : b.l2.store.Regions())
        tune = tune || (r.kind == RegionKind::MusicData && r.z80 <= At("tune") + 8 && r.Z80End() >= At("tune") + 16);
    EXPECT_TRUE(tune) << MediaClaims(b.l2);
    EXPECT_TRUE(NoStrongClaim(At("picture"), static_cast<uint16_t>(At("picture") + 23), RegionKind::MusicData))
        << MediaClaims(b.l2);
}

/// The AY written by a helper that reads nothing: its caller's tune is the music
TEST_F(AnalysisL2_Test, AnalysisDetectorMusic_RegisterWriteHelper)
{
    const Built& b = Run("ay-helper.asm", 4);
    ASSERT_FALSE(HasFailure());
    bool tune = false;
    for (const Region& r : b.l2.store.Regions())
        tune = tune || (r.kind == RegionKind::MusicData && r.z80 <= At("tune") + 8 && r.Z80End() >= At("tune") + 24);
    EXPECT_TRUE(tune) << MediaClaims(b.l2);
}

/// The AY sent from a register copy another routine fills: that routine's tune is the music
TEST_F(AnalysisL2_Test, AnalysisDetectorMusic_RegisterCopy)
{
    const Built& b = Run("ay-shadow.asm", 4);
    ASSERT_FALSE(HasFailure());
    bool tune = false;
    for (const Region& r : b.l2.store.Regions())
        tune = tune || (r.kind == RegionKind::MusicData && r.z80 <= At("tune") + 8 && r.Z80End() >= At("tune") + 24);
    EXPECT_TRUE(tune) << MediaClaims(b.l2);
    EXPECT_TRUE(NoStrongClaim(At("regs"), static_cast<uint16_t>(At("regs") + 13), RegionKind::MusicData));
}

/// A register store helper between the player and the copy: store reads nothing nothing wrote, its caller tick is the
/// player, the tune it reads is music
TEST_F(AnalysisL2_Test, AnalysisDetectorMusic_RegisterStoreHelper)
{
    const Built& b = Run("ay-store.asm", 4);
    ASSERT_FALSE(HasFailure());
    bool tune = false;
    for (const Region& r : b.l2.store.Regions())
        tune = tune || (r.kind == RegionKind::MusicData && r.z80 <= At("tune") + 8 && r.Z80End() >= At("tune") + 24);
    EXPECT_TRUE(tune) << MediaClaims(b.l2);
    EXPECT_TRUE(NoStrongClaim(At("regs"), static_cast<uint16_t>(At("regs") + 13), RegionKind::MusicData));
}

/// Characters the ROM drew (PR_ALL) from RAM are a font with no text: the 21 user-defined graphics at (UDG)
TEST_F(AnalysisL2_Test, AnalysisDetectorFonts_UdgThroughTheRom)
{
    const Built& b = Run("rom-udg.asm");
    ASSERT_FALSE(HasFailure());
    const Region* udg = StartsAt(At("udgs"), RegionKind::Font);
    ASSERT_NE(udg, nullptr) << MediaClaims(b.l2);
    EXPECT_EQ(udg->Z80End(), At("udgs") + 21 * 8 - 1);
    EXPECT_EQ(udg->geometry.frames, 21u);
}

/// An LDIR an interrupt broke into (the handler's own copy in the middle of it) is one copy, not its pieces: no run
/// starts inside the 6000 bytes (the last run may stop short: the recording ended in it)
TEST_F(AnalysisL2_Test, AnalysisCopies_InterruptedLdirIsOneCopy)
{
    const Built& b = Run("copy-interrupted.asm", 3);
    ASSERT_FALSE(HasFailure());
    uint64_t whole = 0;
    std::ostringstream runs;
    for (const BlockCopy& c : b.l1.copies)
        if (c.dst >= 0xA000)
        {
            runs << std::hex << "$" << c.src << " -> $" << c.dst << std::dec << " " << c.length << " x" << c.count
                 << "\n";
            whole += c.src == 0 && c.dst == 0xA000 && c.length == 6000 ? c.count : 0;
            EXPECT_EQ(c.src, 0u) << "a run that starts inside the copy\n" << runs.str();
        }
    EXPECT_GE(whole, 1u) << runs.str();
}

/// A back buffer copied by short rows (an LDI chain a row) is display: the frames drawn into it are graphics
TEST_F(AnalysisL2_Test, AnalysisDetectorSprites_BackBufferCopiedByRows)
{
    const Built& b = Run("backbuffer-rows.asm", 10);
    ASSERT_FALSE(HasFailure());
    const Region* sprite = StartsAt(At("frames"), RegionKind::Sprite, DetectorId::Graphics);
    ASSERT_NE(sprite, nullptr) << MediaClaims(b.l2);
    EXPECT_EQ(sprite->geometry.height, 8u);
    EXPECT_EQ(sprite->geometry.frames, 4u) << MediaClaims(b.l2);
    // The buffer is one claim: 24 rows of 32 bytes, 24 of each copied
    const Region* buffer = StartsAt(At("buffer"), RegionKind::Screen, DetectorId::Screens);
    ASSERT_NE(buffer, nullptr) << MediaClaims(b.l2);
    EXPECT_EQ(buffer->geometry.width, 256u);
    EXPECT_EQ(buffer->geometry.height, 24u);
    EXPECT_EQ(buffer->Z80End(), At("buffer") + 23 * 32 + 23) << MediaClaims(b.l2);
}

/// A working buffer filled, shifted and drawn for each of 8 columns is no picture: its bytes are drawn 8 times a frame
/// and written as often
TEST_F(AnalysisL2_Test, AnalysisDetectorSprites_LineBufferIsNoPicture)
{
    const Built& b = Run("line-buffer.asm", 10);
    ASSERT_FALSE(HasFailure());
    const uint16_t linebuf = At("linebuf");
    for (const Region& r : b.l2.store.Regions())
        if (r.kind == RegionKind::Sprite || r.kind == RegionKind::Tile || r.kind == RegionKind::Font)
            EXPECT_FALSE(r.z80 <= linebuf + 15 && r.Z80End() >= linebuf) << MediaClaims(b.l2);
    // The picture copied into it stays one
    const Region* picture = StartsAt(At("picture"), RegionKind::Sprite, DetectorId::Graphics);
    ASSERT_NE(picture, nullptr) << MediaClaims(b.l2);
    EXPECT_EQ(picture->geometry.height, 8u) << MediaClaims(b.l2);
}

/// Two frames stored back to back, the first one's last row drawn beside the second one's first byte (a shifted
/// sprite's spill byte read past the row): two frames, the step onto the next frame's first row links no line
TEST_F(AnalysisL2_Test, AnalysisDetectorSprites_SpillByteSplitsFrames)
{
    const Built& b = Run("spill-byte.asm", 10);
    ASSERT_FALSE(HasFailure());
    const Region* sprite = StartsAt(At("frames"), RegionKind::Sprite, DetectorId::Graphics);
    ASSERT_NE(sprite, nullptr) << MediaClaims(b.l2);
    EXPECT_EQ(sprite->geometry.width, 16u) << MediaClaims(b.l2);
    EXPECT_EQ(sprite->geometry.height, 8u) << MediaClaims(b.l2);
    EXPECT_EQ(sprite->geometry.frames, 2u) << MediaClaims(b.l2);
}

/// The export states the graphics: an image macro SkoolKit draws (in #UDGTABLE, which skool2asm drops) and the
/// geometry JSON with stride, mask, prefix and layout
TEST_F(AnalysisL2_Test, GraphicsGeometryInTheDraft)
{
    Run("sprite-16x16.asm", kSpriteFrames);
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
    const Built& b = Run("sprite-16x16.asm", kSpriteFrames);
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

/// A kind the review gives a range inside a page-aligned buffer (a table in the cavern buffer made a level map, run
/// C3 on Manic Miner) changes how that range is written only: the ALIGN 256 before the buffer stays where it was
/// (the +$101 shift test failed without it) and the buffer keeps its field list in the fields JSON
TEST_F(AnalysisL2_Test, ReviewKindInsideABufferKeepsItsAlignAndFields)
{
    Run("buffer-page.asm");
    ASSERT_FALSE(HasFailure());
    const auto aligned = [](const std::string& skool) {
        std::vector<std::string> next;  // the line each ALIGN goes after
        const std::vector<std::string> lines = Lines(skool);
        for (size_t i = 0; i + 1 < lines.size(); ++i)
            if (lines[i] == "@isub=+ALIGN 256")
                next.push_back(lines[i + 1].substr(0, 6));
        return next;
    };
    const ExportResult plain = ExportDraft("skool");
    const ExportResult plainFields = ExportDraft("fields");
    ASSERT_TRUE(plain.ok && plainFields.ok);
    ASSERT_FALSE(aligned(plain.text).empty()) << "the page-aligned buffer has its ALIGN\n" << plain.text;
    ASSERT_NE(plainFields.text.find("\"base\": \"$9000\""), std::string::npos) << plainFields.text;

    std::map<uint16_t, ExportNote> notes;
    notes[0x9006].kind = "text";
    notes[0x9006].kindEnd = 0x90FF;
    const ExportResult retyped = ExportDraft("skool", 8, {{0x8000, 0x9FFF}}, notes);
    const ExportResult retypedFields = ExportDraft("fields", 8, {{0x8000, 0x9FFF}}, notes);
    ASSERT_TRUE(retyped.ok && retypedFields.ok);
    EXPECT_NE(retyped.text.find("t$9006"), std::string::npos) << "the range is text now\n" << retyped.text;
    EXPECT_EQ(aligned(retyped.text), aligned(plain.text)) << retyped.text;
    EXPECT_NE(retypedFields.text.find("\"base\": \"$9000\""), std::string::npos)
        << "the buffer keeps its fields\n" << retypedFields.text;
}

/// A reviewer writes a port the way the hardware docs do ("port #FE"): a '#' starts a SkoolKit macro, and skool2asm
/// stopped at "unknown macro: #FE" (the Skool Daze review), so the draft writes it as #CHR(35)#HTML()
TEST_F(AnalysisL2_Test, ReviewTextHashIsNoMacro)
{
    Run("buffer-page.asm");
    ASSERT_FALSE(HasFailure());
    ExportOptions options;
    options.ranges = {{0x8000, 0x9FFF}};
    const DraftPlan plan = MakePlan(_built->l1, _built->l2, options);
    ASSERT_GE(plan.entries.size(), 2u);
    const uint16_t first = plan.entries[0].start;
    const uint16_t inner = static_cast<uint16_t>(plan.entries[1].start + 1);
    std::map<uint16_t, ExportNote> notes;
    notes[first].title = "Beep through port #FE";
    notes[first].comments = {"Bit 4 of #FE is the speaker."};
    notes[inner].title = "Read #FE";
    const ExportResult skool = ExportDraft("skool", 8, {{0x8000, 0x9FFF}}, notes);
    ASSERT_TRUE(skool.ok) << skool.error;
    EXPECT_EQ(skool.text.find("#FE"), std::string::npos) << skool.text;
    EXPECT_NE(skool.text.find("; Beep through port #CHR(35)#HTML()FE\n"), std::string::npos) << skool.text;
    EXPECT_NE(skool.text.find("Bit 4 of #CHR(35)#HTML()FE is the speaker."), std::string::npos) << skool.text;
    EXPECT_NE(skool.text.find("Read #CHR(35)#HTML()FE"), std::string::npos) << skool.text;
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

/// An entry that ends inside an instruction (a range end here; in a game, code later overwritten by a buffer that
/// starts at the instruction's last byte) writes only its own bytes: the instruction's head as DEFB, so the next
/// entry's bytes are not written twice and the rebuild does not shift
TEST_F(AnalysisL2_Test, SkoolDraftInstructionCutByTheEntryEnd)
{
    Run("page-table.asm");
    ASSERT_FALSE(HasFailure());
    // $8000 LD SP,$9F00 is 3 bytes: the first range ends after 2 of them
    const ExportResult skool = ExportDraft("skool", 8, {{0x8000, 0x8001}, {0x8002, 0x9FFF}});
    ASSERT_TRUE(skool.ok) << skool.error;
    const std::vector<std::string> lines = Lines(skool.text);
    // The line of an address: the entry's control character or a space, then the address
    const auto lineOf = [&lines](const char* address) {
        for (size_t i = 0; i < lines.size(); ++i)
            if (lines[i].size() > 6 && lines[i].compare(1, 5, address) == 0)
                return i;
        return std::string::npos;
    };
    const size_t head = lineOf("$8000");
    ASSERT_NE(head, std::string::npos) << skool.text;
    const std::string operation = lines[head].substr(0, lines[head].find(';'));
    EXPECT_NE(operation.find("DEFB $31,$00"), std::string::npos) << lines[head];
    EXPECT_EQ(operation.find("LD SP"), std::string::npos) << lines[head];
    EXPECT_NE(lines[head].find("in the next entry"), std::string::npos) << "the comment names the cut instruction";
    EXPECT_NE(lineOf("$8002"), std::string::npos) << skool.text;
}

/// A page-aligned table first in the range has no line before it for its ALIGN 256: it stays, the moved region
/// starts after it with an ORG
TEST_F(AnalysisL2_Test, LayoutRegionStartsAfterAnAlignedTable)
{
    Run("aligned-start.asm");
    ASSERT_FALSE(HasFailure());
    const ExportResult layout = ExportDraft("layout");
    ASSERT_TRUE(layout.ok) << layout.error;
    EXPECT_NE(layout.text.find("REGION_START := 0x8100"), std::string::npos) << layout.text;
    const ExportResult skool = ExportDraft("skool");
    ASSERT_TRUE(skool.ok) << skool.error;
    const std::vector<std::string> lines = Lines(skool.text);
    EXPECT_NE(Find(lines, "@isub=+ORG #8100"), std::string::npos) << skool.text;
    EXPECT_NE(Find(lines, "/256"), std::string::npos) << skool.text;
    // Negative: the same table after the code (page-table.asm) has its ALIGN, the region starts at the range
    Run("page-table.asm");
    ASSERT_FALSE(HasFailure());
    const ExportResult plain = ExportDraft("layout");
    ASSERT_TRUE(plain.ok) << plain.error;
    EXPECT_NE(plain.text.find("REGION_START := 0x8000"), std::string::npos) << plain.text;
}

/// The fixed tail starts after the last unused block below the first data found by index arithmetic
TEST_F(AnalysisL2_Test, LayoutFixedTailBelowArithmeticData)
{
    Run("arith-table.asm");
    ASSERT_FALSE(HasFailure());
    const ExportResult layout = ExportDraft("layout");
    ASSERT_TRUE(layout.ok) << layout.error;
    EXPECT_NE(layout.text.find("SHIFT_END := 0x9800"), std::string::npos) << layout.text;
}

/// An aligned table in the moved part: a $101 shift moves what follows it by $200, the fixed tail goes after an
/// unused block that takes that
TEST_F(AnalysisL2_Test, LayoutFixedTailMakesRoomForAlignment)
{
    Run("aligned-tail.asm");
    ASSERT_FALSE(HasFailure());
    const ExportResult skool = ExportDraft("skool");
    ASSERT_TRUE(skool.ok) << skool.error;
    EXPECT_NE(skool.text.find("ALIGN 256"), std::string::npos) << skool.text;
    const ExportResult layout = ExportDraft("layout");
    ASSERT_TRUE(layout.ok) << layout.error;
    EXPECT_NE(layout.text.find("SHIFT_END := 0x9680"), std::string::npos) << layout.text;
}

/// No unused block of $200 below the data found by arithmetic: the tail stays after the block of $101 (one above that
/// data would move it)
TEST_F(AnalysisL2_Test, LayoutFixedTailStaysBelowArithmeticData)
{
    Run("aligned-tail-above.asm");
    ASSERT_FALSE(HasFailure());
    const ExportResult layout = ExportDraft("layout");
    ASSERT_TRUE(layout.ok) << layout.error;
    EXPECT_NE(layout.text.find("SHIFT_END := 0x8400"), std::string::npos) << layout.text;
}

/// A return address built from two byte immediates: the RET is a dispatch, both immediates follow the target's label
TEST_F(AnalysisL2_Test, BuiltReturnAddressFollowsTheLabel)
{
    const Built& b = Run("built-return.asm");
    ASSERT_FALSE(HasFailure());
    const Routine* target = b.l1.RoutineAtZ80(At("target"));
    ASSERT_NE(target, nullptr);
    EXPECT_TRUE(target->kinds & entry::kDispatch) << EntryKindNames(target->kinds);
    EXPECT_TRUE(std::any_of(b.l1.calls.begin(), b.l1.calls.end(), [&](const CallEdge& c) {
        return c.kind == CallKind::Dispatch && &b.l1.routines[c.callee] == target;
    }));
    const ExportResult skool = ExportDraft("skool");
    ASSERT_TRUE(skool.ok) << skool.error;
    const std::vector<std::string> lines = Lines(skool.text);
    const size_t high = Find(lines, "@isub=LD (HL),");
    ASSERT_NE(high, std::string::npos) << skool.text;
    EXPECT_NE(lines[high].find("/256"), std::string::npos) << lines[high];
    const size_t low = Find(lines, "@isub=LD (HL),", high + 1);
    ASSERT_NE(low, std::string::npos) << skool.text;
    EXPECT_NE(lines[low].find("%256"), std::string::npos) << lines[low];
}

/// A pointer kept through EXX (the alternate set): the pointer use names its LD HL,nn, the draft keeps no number
TEST_F(AnalysisL2_Test, PointerThroughTheAlternateSet)
{
    const Built& b = Run("exx-pointer.asm");
    ASSERT_FALSE(HasFailure());
    EXPECT_TRUE(std::any_of(b.l1.pointers.begin(), b.l1.pointers.end(), [this](const PointerUse& p) {
        return p.form == PointerForm::Immediate && p.z80 == At("load") && p.value == At("frames") && p.uses > 0 &&
               p.minDelta == 0 && p.maxDelta == 24;
    }));
    const ExportResult skool = ExportDraft("skool");
    ASSERT_TRUE(skool.ok) << skool.error;
    const std::vector<std::string> lines = Lines(skool.text);
    char at[16];
    std::snprintf(at, sizeof(at), "$%04X LD HL,", At("load"));
    const size_t hl = Find(lines, at);
    ASSERT_NE(hl, std::string::npos) << skool.text;
    EXPECT_NE(lines[hl - 1], "@keep") << skool.text;
    // Negative: the same value used as a count stays a number
    std::snprintf(at, sizeof(at), "$%04X LD BC,", At("count"));
    const size_t bc = Find(lines, at);
    ASSERT_NE(bc, std::string::npos) << skool.text;
    EXPECT_EQ(lines[bc - 1], "@keep") << skool.text;
}

/// A pointer word loaded from a table, passed through the stack and stored into a draw list, then used from the list:
/// the table's words are pointers too (L1 follows the bytes through memory), and the table is a pointer table
TEST_F(AnalysisL2_Test, AnalysisDataFlow_PointerThroughAList)
{
    const Built& b = Run("pointer-through-list.asm", 20);
    ASSERT_FALSE(HasFailure());
    const uint16_t table = At("table");
    for (uint16_t slot = 0; slot < 4; ++slot)
        EXPECT_TRUE(std::any_of(b.l1.pointers.begin(), b.l1.pointers.end(), [&](const PointerUse& p) {
            return p.form == PointerForm::Word && p.z80 == table + 2 * slot && p.uses > 0;
        })) << "slot " << slot;
    EXPECT_TRUE(std::any_of(b.l1.pointers.begin(), b.l1.pointers.end(), [&](const PointerUse& p) {
        return p.form == PointerForm::Word && p.z80 == At("list") && p.uses > 0;
    })) << "the list's word stays a pointer as well";
    const Region* t = StartsAt(table, RegionKind::PointerTable);
    ASSERT_NE(t, nullptr);
    EXPECT_GE(t->geometry.count, 4u);
    // Negative: a byte only copied into the list, never an address
    EXPECT_FALSE(std::any_of(b.l1.pointers.begin(), b.l1.pointers.end(), [&](const PointerUse& p) {
        return p.form == PointerForm::Word && p.z80 == At("bytes");
    }));
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

/// One table read by two routines, one from each entry's start and one from the byte after it: one array of six
/// 5-byte records at the table, the second reader's offsets shifted into it; no record runs into the byte after
TEST_F(AnalysisL2_Test, AnalysisDetectorRecords_ShiftedReadersAreOneArray)
{
    const Built& b = Run("records-shifted-readers.asm");
    ASSERT_FALSE(HasFailure());
    const Region* table = StartsAt(At("table"), RegionKind::RecordArray, DetectorId::Records);
    ASSERT_NE(table, nullptr);
    EXPECT_EQ(table->geometry.stride, 5u);
    EXPECT_EQ(table->geometry.count, 6u);
    EXPECT_EQ(table->Z80End(), At("after") - 1u);
    size_t read = 0;
    for (const Field& f : table->geometry.fields)
        read += f.name != "unread" ? f.size : 0;
    EXPECT_EQ(read, 5u) << "the byte the first reader takes and the four the second does";
    for (const Region& r : b.l2.store.Regions())
        if (r.kind == RegionKind::RecordArray && r.z80 >= At("table") && r.z80 <= At("after"))
            EXPECT_EQ(r.z80, At("table")) << RegionTitle(r);
    EXPECT_TRUE(Claims(At("after"), RegionKind::RecordArray).empty());
}

/// Records copied 8 bytes at a time into the 16-byte slots of a record array: the blocks copied are records of 8
/// bytes with the slots' fields (observed), the blocks only the pointer table names inferred
TEST_F(AnalysisL2_Test, AnalysisDetectorRecords_GatheredByShortCopies)
{
    Run("records-gathered-blocks.asm");
    ASSERT_FALSE(HasFailure());
    for (const char* block : {"block1", "block2", "block3", "block4"})
    {
        const Region* r = StartsAt(At(block), RegionKind::RecordArray, DetectorId::Records);
        ASSERT_NE(r, nullptr) << block;
        EXPECT_EQ(r->geometry.stride, 8u) << block;
        EXPECT_EQ(r->geometry.count, 4u) << block;
        EXPECT_EQ(r->Z80End(), At(block) + 31u) << block;
        bool field1 = false, field3 = false;
        for (const Field& f : r->geometry.fields)
        {
            field1 = field1 || (f.offset == 1 && f.name != "unread");
            field3 = field3 || (f.offset == 3 && f.name != "unread");
        }
        EXPECT_TRUE(field1 && field3) << block << ": the slots' fields at +1 and +3";
        const bool played = std::string(block) == "block1" || std::string(block) == "block2";
        EXPECT_EQ(r->status, played ? RegionStatus::Observed : RegionStatus::Inferred) << block;
    }
}

/// Strips saved into an area through a moving pointer and copied back with the same lengths: the area is a save
/// area (a buffer titled with what it saves) over every slot seen, and the slots written only twice at its end are
/// no table one routine built
TEST_F(AnalysisL2_Test, AnalysisDetectorSaveAreas_SavedAndRestoredStrips)
{
    const Built& b = Run("save-area.asm", 20);
    ASSERT_FALSE(HasFailure());
    const uint16_t save = At("save");
    EXPECT_TRUE(NoStrongClaim(save, static_cast<uint16_t>(save + 71), RegionKind::InitTable));
    const Region* area = StartsAt(save, RegionKind::Buffer, DetectorId::Records);
    ASSERT_NE(area, nullptr);
    EXPECT_EQ(area->Z80End(), save + 71u);
    EXPECT_EQ(area->title.rfind("Save area of $9000-$930B", 0), 0u) << area->title;
}

/// Copies both ways between a score and the rows of a table: the rows, read only to put the score back, are a save
/// area; the score, which the game reads too, is the data saved, no save area
TEST_F(AnalysisL2_Test, AnalysisDetectorSaveAreas_LiveDataIsNoSave)
{
    const Built& b = Run("save-live-score.asm", 6);
    ASSERT_FALSE(HasFailure());
    const uint16_t score = At("score");
    const uint16_t rows = At("rows");
    const Region* live = StartsAt(score, RegionKind::Buffer, DetectorId::Records);
    EXPECT_EQ(live, nullptr) << live->title;
    const Region* area = StartsAt(rows, RegionKind::Buffer, DetectorId::Records);
    ASSERT_NE(area, nullptr);
    EXPECT_EQ(area->Z80End(), rows + 14u);
    EXPECT_EQ(area->title.rfind("Save area of $9000-$9004", 0), 0u) << area->title;
}

// ---------------------------------------------------------------------------------------------------- unused

/// Untouched bytes no detector explains: a fill of one value is Unused; a routine that never ran (its calls and jumps
/// land on its own instructions and in the ROM) is code the session did not reach, inferred Code with its instructions
/// in the code map, not Unused; a small picture nothing read is not code
TEST_F(AnalysisL2_Test, AnalysisDetectorCodeMap_FillIsUnusedLeftoverIsCode)
{
    const Built& b = Run("unused-areas.asm");
    ASSERT_FALSE(HasFailure());
    const Region* fill = StartsAt(At("fill"), RegionKind::Unused, DetectorId::CodeMap);
    ASSERT_NE(fill, nullptr);
    EXPECT_EQ(fill->Z80End(), At("fill") + 63u);
    EXPECT_NE(fill->title.find("Fill"), std::string::npos) << fill->title;
    const Region* code = StartsAt(At("leftover"), RegionKind::Code, DetectorId::CodeMap);
    ASSERT_NE(code, nullptr);
    EXPECT_EQ(code->status, RegionStatus::Inferred);
    EXPECT_GE(code->Z80End(), At("leftover_end") - 1u);
    EXPECT_NE(code->title.find("never ran"), std::string::npos) << code->title;
    EXPECT_TRUE(Claims(At("leftover"), RegionKind::Unused).empty()) << "code not yet run is no unused byte";
    bool line = false;
    for (const CodeLine& l : b.l2.code)
        line |= l.z80 == At("leftover");
    EXPECT_TRUE(line) << "its instructions are in the code map: the export disassembles them";
    for (const Region* r : Claims(At("picture"), RegionKind::Unused))
        ADD_FAILURE() << "a picture with no calls or jumps is no leftover code: " << r->title;
    for (const Region* r : Claims(At("picture"), RegionKind::Code))
        if (r->detector == DetectorId::CodeMap && r->status == RegionStatus::Inferred)
            ADD_FAILURE() << "a picture with no calls or jumps is no code: " << r->title;
    for (const Region* r : Claims(At("handler"), RegionKind::Unused))
        ADD_FAILURE() << "a routine a handler table points at did not run, it is no leftover: " << r->title;
    // The fill and the code explain the bytes: no unknown finding over them
    for (const Finding& f : BaseFindings(b.l1, b.l2))
        if (f.kind == FindingKind::Unknown)
            EXPECT_TRUE(f.end < At("fill") || f.start > At("leftover_end")) << f.claim;
}

// ---------------------------------------------------------------------------------------------------- printer arguments

/// A printer's argument: LD HL,gauge / CALL print_str passes a string of control records and cells the code fills
/// before each print to the routine that prints two plain strings. It is text from its address up to and including
/// the $FF the printer stops at after its other strings, the filled cells a field of it; the bytes after it are no
/// text
TEST_F(AnalysisL2_Test, AnalysisDetectorText_PrinterArgument)
{
    const Built& b = Run("print-argument.asm");
    ASSERT_FALSE(HasFailure());
    ASSERT_NE(StartsAt(At("score"), RegionKind::Text, DetectorId::Text), nullptr) << "the plain strings are text";
    ASSERT_NE(StartsAt(At("lives"), RegionKind::Text, DetectorId::Text), nullptr);
    const Region* gauge = StartsAt(At("gauge"), RegionKind::Text, DetectorId::Text);
    std::string texts;
    for (const Region& r : b.l2.store.Regions())
        if (r.kind == RegionKind::Text)
            texts += std::to_string(r.z80) + "-" + std::to_string(r.Z80End()) + " " + r.geometry.text + "\n";
    ASSERT_NE(gauge, nullptr) << texts;
    EXPECT_EQ(gauge->Z80End(), At("gauge_end")) << texts;
    EXPECT_EQ(gauge->geometry.terminator, "$FF");
    EXPECT_EQ(gauge->geometry.width, static_cast<uint16_t>(At("gauge_end") - At("gauge")));
    EXPECT_TRUE(std::any_of(gauge->evidence.begin(), gauge->evidence.end(),
                            [](const Evidence& e) { return e.what == "argument"; }));
    EXPECT_TRUE(std::any_of(gauge->geometry.fields.begin(), gauge->geometry.fields.end(), [this](const Field& f) {
        return f.offset == At("gauge_cells") - At("gauge") && f.size == 3 && f.name == "filled";
    })) << "the cells the code fills are a field";
    for (uint16_t a = At("after"); a < At("after") + 3; ++a)
        EXPECT_TRUE(Claims(a, RegionKind::Text).empty()) << "past the terminator: " << a << "\n" << texts;
}

// ---------------------------------------------------------------------------------------------------- blank strips

/// Masked sprite frames of three 8 x 16 strips, every strip drawn through its own word of a pointer table (each word
/// names one strip alone, so every strip is an object of its own). The last strip of the last frame is $FF,$00 on every
/// line: one value, no picture alone, but the same blit drew it with the same shape right after the strip before it.
/// It is part of that sprite's claim, not left unknown
TEST_F(AnalysisL2_Test, AnalysisDetectorSprites_BlankStripJoinsItsNeighbour)
{
    const Built& b = Run("sprite-blank-strip.asm", kSpriteFrames);
    ASSERT_FALSE(HasFailure());
    ASSERT_NE(StartsAt(At("frame0"), RegionKind::Sprite, DetectorId::Graphics), nullptr) << MediaClaims(b.l2);
    const Region* sprite = nullptr;
    for (const Region* r : Claims(At("blank"), RegionKind::Sprite))
        if (r->detector == DetectorId::Graphics)
            sprite = r;
    ASSERT_NE(sprite, nullptr) << "the transparent strip is no sprite\n" << MediaClaims(b.l2);
    EXPECT_GE(sprite->Z80End(), At("blank") + 31u) << MediaClaims(b.l2);
    EXPECT_LT(sprite->z80, At("blank")) << "joined to the strip before it\n" << MediaClaims(b.l2);
    EXPECT_EQ(sprite->geometry.width, 8u);
    EXPECT_EQ(sprite->geometry.height, 16u);
    EXPECT_TRUE(sprite->geometry.mask);
    EXPECT_EQ(sprite->geometry.stride, 32u);
}

// ---------------------------------------------------------------------------------------------------- frame lattice

/// Four masked frames of three 8 x 8 strips stored in columns, named by a table of words 48 bytes apart, drawn in
/// windows that clip whole strips: frames 0 and 2 whole, frame 1 as strip 0 and as strips 1-2, frame 3 as strips 0-1
/// and as its fully transparent strip 2 alone. Without the table the claims follow the column groups (and leave the
/// transparent strip out); the table's stride, the columns tiling its blocks and a frame drawn three columns wide
/// re-cut them: one sprite from frame 0 to the end of frame 3, four frames of 24 x 8, 48 bytes apart
TEST_F(AnalysisL2_Test, AnalysisDetectorSprites_PointerTableFrameLattice)
{
    const Built& b = Run("sprite-frame-lattice.asm", kSpriteFrames);
    ASSERT_FALSE(HasFailure());
    constexpr uint32_t kFrames = 4;
    constexpr uint32_t kFrameBytes = 48;
    const uint16_t first = At("frame0");
    const uint16_t end = At("frames_end");
    ASSERT_EQ(first + kFrames * kFrameBytes, end);
    const Region* sprite = StartsAt(first, RegionKind::Sprite, DetectorId::Graphics);
    ASSERT_NE(sprite, nullptr) << MediaClaims(b.l2);
    EXPECT_EQ(sprite->Z80End(), end - 1u) << MediaClaims(b.l2);
    EXPECT_EQ(sprite->geometry.frames, kFrames) << MediaClaims(b.l2);
    EXPECT_EQ(sprite->geometry.stride, kFrameBytes);
    EXPECT_EQ(sprite->geometry.width, 24u);
    EXPECT_EQ(sprite->geometry.height, 8u);
    EXPECT_TRUE(sprite->geometry.mask);
    EXPECT_EQ(sprite->geometry.layout, "columns");
    EXPECT_TRUE(std::any_of(sprite->evidence.begin(), sprite->evidence.end(),
                            [](const Evidence& e) { return e.what == "frame-lattice"; }));
    // No claim of a column group is left inside, the transparent strip included
    std::set<const Region*> others;
    for (uint16_t a = first; a < end; ++a)
        for (const RegionKind kind : {RegionKind::Sprite, RegionKind::Tile})
            for (const Region* r : Claims(a, kind))
                if (r->detector == DetectorId::Graphics && r != sprite)
                    others.insert(r);
    EXPECT_TRUE(others.empty()) << MediaClaims(b.l2);
}

// ---------------------------------------------------------------------------------------------------- review fixes

/// The save of save-area.asm seen the other way round: the restore copies the area back over the strips, and the save
/// copies the strips into the area again, so each strip also looks saved by the restore. The strips are the live
/// background (code draws into them between the copies): no save area on them
TEST_F(AnalysisL2_Test, AnalysisDetectorSaveAreas_SourceIsNoSaveArea)
{
    Run("save-area.asm", 20);
    ASSERT_FALSE(HasFailure());
    for (const uint16_t strip : {uint16_t{0x9000}, uint16_t{0x9100}, uint16_t{0x9200}, uint16_t{0x9300}})
        for (uint16_t a = strip; a < strip + 12; ++a)
            for (const Region* r : Claims(a, RegionKind::Buffer))
                EXPECT_FALSE(r->detector == DetectorId::Records && r->title.rfind("Save area", 0) == 0)
                    << "a strip the save copies from is no save area: " << r->title;
    ASSERT_NE(StartsAt(At("save"), RegionKind::Buffer, DetectorId::Records), nullptr) << "the area itself still is";
}

/// Three bases 2 bytes apart and a fourth far above them, one routine reading two bytes through IX at each: no record
/// array of 2-byte records over everything between them
TEST_F(AnalysisL2_Test, AnalysisDetectorRecords_SparseBasesAreNoArray)
{
    Run("records-sparse-bases.asm");
    ASSERT_FALSE(HasFailure());
    for (const uint16_t a : {At("between"), static_cast<uint16_t>(At("between") + 0x100), At("distant")})
        for (const Region* r : Claims(a, RegionKind::RecordArray))
            EXPECT_NE(r->detector, DetectorId::Records)
                << "four bases make no array of " << r->geometry.count << " records: " << RegionTitle(*r);
}

/// A working copy of an object the code updates every pass and copies into the records of an array is no template:
/// no record array of 9-byte records at it
TEST_F(AnalysisL2_Test, AnalysisDetectorRecords_WorkingCopyIsNoTemplate)
{
    Run("record-working-copy.asm");
    ASSERT_FALSE(HasFailure());
    const Region* records = StartsAt(At("records"), RegionKind::RecordArray, DetectorId::Records);
    ASSERT_NE(records, nullptr) << "the records walked through IX";
    EXPECT_EQ(records->geometry.stride, 16u);
    for (const Region* r : Claims(At("current"), RegionKind::RecordArray))
        EXPECT_NE(r->detector, DetectorId::Records) << "a working copy code writes: " << RegionTitle(*r);
}

/// An HL walk that restarts at +2 of each record and tests the byte there for the end: the last base is that restart,
/// the terminator is the byte it tested (+2 of the seventh record), the array is the six records before it
TEST_F(AnalysisL2_Test, AnalysisDetectorRecords_RestartTerminator)
{
    Run("records-hl-restart-end.asm");
    ASSERT_FALSE(HasFailure());
    const Region* recs = StartsAt(At("recs"), RegionKind::RecordArray, DetectorId::Records);
    ASSERT_NE(recs, nullptr);
    EXPECT_EQ(recs->geometry.stride, 16u);
    EXPECT_EQ(recs->geometry.count, 6u);
    EXPECT_EQ(recs->Z80End(), At("last") - 1u);
    EXPECT_EQ(recs->geometry.terminator, "$FF");
    const Region* end = StartsAt(static_cast<uint16_t>(At("last") + 2), RegionKind::Data, DetectorId::Records);
    ASSERT_NE(end, nullptr) << "the terminator at the restart's place";
    EXPECT_EQ(end->title, "terminator");
    EXPECT_EQ(end->Size(), 1u);
}

/// Entries of 9 bytes in 16-byte slots: seven bytes of padding after each entry stay inside one table
TEST_F(AnalysisL2_Test, AnalysisDetectorInitTables_SevenBytesOfPadding)
{
    const Built& b = Run("init-padded-wide.asm", 3);
    ASSERT_FALSE(HasFailure());
    const uint16_t table = At("table");
    std::string all;
    for (const Region& r : b.l2.store.Regions())
        if (r.kind == RegionKind::InitTable)
            all += "table " + std::to_string(r.z80) + "-" + std::to_string(r.Z80End()) + "\n";
    const Region* found = StartsAt(table, RegionKind::InitTable);
    ASSERT_NE(found, nullptr) << all;
    EXPECT_EQ(found->Z80End(), table + 7u * 16u + 8u) << "to the last entry's last read byte: " << all;
    EXPECT_LT(found->Z80End(), At("tail")) << all;
}

/// tiles-copied-map.asm with its map across the $C000 page boundary: the cells are claimed as two maps, one each side
/// of the boundary, not dropped as one claim across pages
TEST_F(AnalysisL2_Test, AnalysisDetectorLevelMaps_CopiedMapSplitsAtAPage)
{
    std::string source = ReadFixture("tiles-copied-map.asm");
    const std::string from = "        org $9100\nmap:";
    const size_t at = source.find(from);
    ASSERT_NE(at, std::string::npos);
    source.replace(at, from.size(), "        org $BF80\nmap:");
    const Built& b = Run("tiles-copied-map.asm#page", 30, false, source);
    ASSERT_FALSE(HasFailure());
    const uint16_t map = At("map");
    ASSERT_EQ(map, 0xBF80);
    const Region* low = StartsAt(map, RegionKind::LevelMap, DetectorId::LevelMaps);
    ASSERT_NE(low, nullptr) << MediaClaims(b.l2);
    EXPECT_EQ(low->Z80End(), 0xBFFFu) << MediaClaims(b.l2);
    const Region* high = StartsAt(0xC000, RegionKind::LevelMap, DetectorId::LevelMaps);
    ASSERT_NE(high, nullptr) << MediaClaims(b.l2);
    EXPECT_EQ(high->Z80End(), map + 263u) << MediaClaims(b.l2);
}

// ---------------------------------------------------------------------------------------------------- review fixes

/// A printer's argument is read where the printer reads its strings: a table of cell colours the caller hands to the
/// printer (LD BC,shade), read by an instruction of its own, ending on the printer's $FF, is no text; the gauge the
/// printer reads as a string stays text
TEST_F(AnalysisL2_Test, AnalysisDetectorText_PrinterArgumentTableIsNoText)
{
    const Built& b = Run("print-argument-table.asm");
    ASSERT_FALSE(HasFailure());
    std::string texts;
    for (const Region& r : b.l2.store.Regions())
        if (r.kind == RegionKind::Text)
            texts += std::to_string(r.z80) + "-" + std::to_string(r.Z80End()) + " " + r.geometry.text + "\n";
    ASSERT_NE(StartsAt(At("score"), RegionKind::Text, DetectorId::Text), nullptr) << texts;
    const Region* gauge = StartsAt(At("gauge"), RegionKind::Text, DetectorId::Text);
    ASSERT_NE(gauge, nullptr) << "the string argument is still text\n" << texts;
    EXPECT_EQ(gauge->Z80End(), At("gauge_end")) << texts;
    for (uint16_t a = At("shade"); a <= At("shade_end"); ++a)
        EXPECT_TRUE(Claims(a, RegionKind::Text).empty()) << "a table handed to the printer: " << a << "\n" << texts;
}

/// A room map unpacked again every pass and read by the room-draw routine (which writes the screen) only to pick the
/// tile bytes it draws is no back buffer: its own values never reach the screen
TEST_F(AnalysisL2_Test, AnalysisDetectorScreens_UnpackedRoomMapIsNoBackBuffer)
{
    const Built& b = Run("room-map-unpacked.asm", 30);
    ASSERT_FALSE(HasFailure());
    const uint16_t map = At("map");
    size_t selectors = 0;
    for (const IndexUse& x : b.l1.indexes)
        if (x.selectorZ80 >= map && x.selectorZ80 < map + 2048u)
            ++selectors;
    EXPECT_GE(selectors, 1024u) << "the map's cells index the tile bytes";
    for (uint32_t a = map; a < map + 2048u; a += 64)
        for (const Region* r : Claims(static_cast<uint16_t>(a), RegionKind::Screen))
            EXPECT_NE(r->detector, DetectorId::Screens) << "a map is no back buffer: " << r->title << "\n"
                                                        << MediaClaims(b.l2);
}

/// A drawn back buffer across a 16 K page boundary ($BC00-$C3FF): one claim a page, both halves claimed
TEST_F(AnalysisL2_Test, AnalysisDetectorScreens_DrawnBackBufferAcrossPages)
{
    std::string mutated = ReadFixture("drawn-back-buffer.asm");
    const std::string from = "        org $9000\nbuffer:";
    const size_t at = mutated.find(from);
    ASSERT_NE(at, std::string::npos);
    mutated.replace(at, from.size(), "        org $BC00\nbuffer:");
    const Built& b = Run("drawn-back-buffer.asm#pages", 20, false, mutated);
    ASSERT_FALSE(HasFailure());
    ASSERT_EQ(At("buffer"), 0xBC00);
    const Region* low = StartsAt(At("buffer"), RegionKind::Screen, DetectorId::Screens);
    ASSERT_NE(low, nullptr) << MediaClaims(b.l2);
    EXPECT_EQ(low->Z80End(), 0xBFFFu) << MediaClaims(b.l2);
    EXPECT_EQ(low->geometry.layout, "drawn");
    const Region* high = StartsAt(0xC000, RegionKind::Screen, DetectorId::Screens);
    ASSERT_NE(high, nullptr) << MediaClaims(b.l2);
    EXPECT_EQ(high->Z80End(), At("buffer") + 2047u) << MediaClaims(b.l2);
}

/// One LDIR copies a 192-byte strip into the drawn buffer in pieces of 40 and 36 bytes: their common divisor (4) is no
/// tile size when no copy took 4 bytes alone, so the strip is no set of 48 copied tiles
TEST_F(AnalysisL2_Test, AnalysisDetectorTiles_CopiedTilesNeedTheUnitAlone)
{
    const Built& b = Run("tiles-copied-unit.asm", 30);
    ASSERT_FALSE(HasFailure());
    EXPECT_FALSE(Claims(At("buffer"), RegionKind::Screen).empty()) << "the buffer is drawn\n" << MediaClaims(b.l2);
    for (uint16_t a = At("strip"); a < At("strip") + 192; a = static_cast<uint16_t>(a + 4))
        for (const Region* r : Claims(a, RegionKind::Tile))
            EXPECT_FALSE(r->detector == DetectorId::Graphics &&
                         std::any_of(r->evidence.begin(), r->evidence.end(),
                                     [](const Evidence& e) { return e.what == "copy"; }))
                << "pieces of 40 and 36 bytes are no 4-byte tiles: " << r->title << "\n"
                << MediaClaims(b.l2);
}

/// Tiles copied from two sets 2 K apart are two tile sets, not one claim over the bytes between them
TEST_F(AnalysisL2_Test, AnalysisDetectorTiles_CopiedTilesFarSetsApart)
{
    const Built& b = Run("tiles-copied-far.asm", 30);
    ASSERT_FALSE(HasFailure());
    const Region* low = StartsAt(At("tiles"), RegionKind::Tile, DetectorId::Graphics);
    ASSERT_NE(low, nullptr) << MediaClaims(b.l2);
    EXPECT_EQ(low->Z80End(), At("tiles") + 31u) << MediaClaims(b.l2);
    EXPECT_EQ(low->geometry.frames, 4u);
    const Region* high = StartsAt(At("far"), RegionKind::Tile, DetectorId::Graphics);
    ASSERT_NE(high, nullptr) << MediaClaims(b.l2);
    EXPECT_EQ(high->Z80End(), At("far") + 31u) << MediaClaims(b.l2);
    EXPECT_EQ(high->geometry.frames, 4u);
    for (uint32_t a = At("tiles") + 32u; a < At("far"); a += 64)
        for (const Region* r : Claims(static_cast<uint16_t>(a), RegionKind::Tile))
            EXPECT_NE(r->detector, DetectorId::Graphics) << "the bytes between the sets: " << r->title;
}

/// The font goes on over unprinted glyph slots only while nothing but the glyph readers touched them: a slot the code
/// writes into (a variable kept in the place of 'Y') ends it
TEST_F(AnalysisL2_Test, AnalysisDetectorFonts_WrittenSlotEndsTheFont)
{
    std::string mutated = ReadFixture("font-slot-text.asm");
    const std::string from = "        jr main\nprint:";
    const size_t at = mutated.find(from);
    ASSERT_NE(at, std::string::npos);
    mutated.replace(at, from.size(), "        ld (font+('Y'-32)*8+3),a\n        jr main\nprint:");
    const Built& b = Run("font-slot-text.asm#written", 3, false, mutated);
    ASSERT_FALSE(HasFailure());
    const auto glyph = [&](char c) { return static_cast<uint16_t>(At("font") + (c - 32) * 8); };
    bool w = false;
    for (const Region* r : Claims(glyph('W'), RegionKind::Font))
        w |= r->detector == DetectorId::Graphics;
    EXPECT_TRUE(w) << "'W', never printed, untouched, is the font: " << MediaClaims(b.l2);
    for (uint16_t a = glyph('Y'); a < glyph('Z'); ++a)
        for (const Region* r : Claims(a, RegionKind::Font))
            EXPECT_NE(r->detector, DetectorId::Graphics) << "a slot the code writes is no glyph: " << r->title;
}

// ---------------------------------------------------------------------------------------------------- stack review

/// A recording that begins inside a routine: POP DE / POP HL take the slots pushed before it (nothing in the replay
/// pushed them, no LD SP began a walk) and the code reads and writes through them. No word pointer, no pointer table
/// on the stack
TEST_F(AnalysisL2_Test, AnalysisDetectorPointerTables_PopBeforeRecordingIsNoTable)
{
    const Built& b = Run("pop-before-recording.asm");
    ASSERT_FALSE(HasFailure());
    const ByteUse* read = b.l1.UseAt(b.l1.StartKey(0xC100));
    ASSERT_NE(read, nullptr);
    ASSERT_NE(read->flags & use::kRead, 0) << "HL came from the slots";
    for (const PointerUse& p : b.l1.pointers)
        if (p.form == PointerForm::Word)
            EXPECT_TRUE(p.origin != b.l1.StartKey(0x9F00) && p.origin != b.l1.StartKey(0x9F02)) << p.z80;
    for (uint16_t z80 = 0x9F00; z80 < 0x9F04; ++z80)
    {
        EXPECT_TRUE(Claims(z80, RegionKind::PointerTable).empty()) << z80;
        EXPECT_TRUE(Claims(z80, RegionKind::HandlerTable).empty()) << z80;
    }
}

/// Rows an SP walk reads (LD SP,rows / POP DE) and nothing else touches: only popped, never pushed, no stack. The
/// CALL's slots are the stack
TEST_F(AnalysisL2_Test, AnalysisDetectorCodeMap_PoppedRowsAreNoStack)
{
    Run("pop-rows.asm");
    ASSERT_FALSE(HasFailure());
    const Region* stack = StartsAt(0x9EFE, RegionKind::Unused, DetectorId::CodeMap);
    ASSERT_NE(stack, nullptr) << "the CALL's return slots";
    EXPECT_EQ(stack->title.rfind("Stack", 0), 0u) << stack->title;
    for (uint16_t a = At("rows"); a < At("rows_end"); ++a)
        for (const Region* r : Claims(a, RegionKind::Unused))
            EXPECT_NE(r->title.rfind("Stack", 0), 0u) << "popped, never pushed: " << r->title << " at " << a;
}

/// The word after a handler table lies inside the targets jumped to, but points at the operand of an executed JP:
/// no entry, the table ends before it
TEST_F(AnalysisL2_Test, AnalysisDetectorPointerTables_HullWordIntoAnOperandIsNoEntry)
{
    Run("hull-operand.asm");
    ASSERT_FALSE(HasFailure());
    const Region* table = StartsAt(At("table"), RegionKind::HandlerTable);
    ASSERT_NE(table, nullptr);
    EXPECT_EQ(table->Z80End(), At("after") - 1u);
    EXPECT_EQ(table->geometry.count, 12u);
}

/// A handler a table entry points at that never ran ends at its first JP no branch before it goes past: the bytes
/// after it decode and end in a RET, but are no part of it
TEST_F(AnalysisL2_Test, AnalysisDetectorCodeMap_UnrunHandlerEndsAtItsFirstStop)
{
    Run("hull-operand.asm");
    ASSERT_FALSE(HasFailure());
    const Region* handler = StartsAt(At("unrun_a"), RegionKind::Code, DetectorId::CodeMap);
    ASSERT_NE(handler, nullptr);
    EXPECT_EQ(handler->status, RegionStatus::Inferred);
    EXPECT_EQ(handler->Z80End(), At("trailer") - 1u);
    EXPECT_TRUE(IsEntry(At("unrun_a")));
    EXPECT_TRUE(Claims(At("trailer"), RegionKind::Code).empty());
}

/// A routine that never ran whose best decode starts one byte into the stretch, and a table entry pointing at that
/// start: a routine the session did not reach, no leftover
TEST_F(AnalysisL2_Test, AnalysisDetectorCodeMap_TableEntryAtTheDecodedStart)
{
    Run("leftover-offset-entry.asm");
    ASSERT_FALSE(HasFailure());
    EXPECT_FALSE(Claims(At("fill"), RegionKind::Unused).empty()) << "the fill before it";
    for (const uint16_t a : {At("junk"), At("routine"), At("routine_loop")})
        for (const Region* r : Claims(a, RegionKind::Unused))
            ADD_FAILURE() << "a table entry points at the routine's start: " << r->title << " at " << a;
}
