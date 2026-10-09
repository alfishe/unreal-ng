// The source an assembler holds in RAM (asm-synchronizer.md §10): golden dumps (testdata/sync) of ALASM 5.09 / 4.44
// and TASM 4.12 sessions in unreal-ng, each with the file the assembler itself saved at that moment. The reader must
// give that file from the pages byte for byte, and the probe must name the assembler and no other.

#include <gtest/gtest.h>

#include <algorithm>

#include "symbols/io/json.h"
#include "testdata.h"
#include "unrealasm/registry.h"
#include "unrealasm/sync/reader.h"
#include "unrealasm/sync/session.h"

using namespace unrealasm;
using namespace unrealasm::sync;
using unrealasm::testing::ReadTestData;
using unrealasm::testing::ReadTestText;

namespace
{
/// A dump folder: its pages (kept alive here) and the machine view over them
struct Dump
{
    std::vector<std::vector<uint8_t>> pages;
    MachineView machine;
    std::vector<uint8_t> expected;
    bool editor = false;
    bool typing = false;
};

Dump Load(const std::string& folder)
{
    Dump dump;
    symbols::json::Value meta;
    std::string error;
    size_t offset = 0;
    EXPECT_TRUE(symbols::json::Parse(ReadTestText("sync/" + folder + "/machine.json"), meta, error, offset)) << error;
    const auto* windows = meta.Get("windows");
    for (size_t w = 0; w < 4 && windows && w < windows->array.size(); ++w)
        dump.machine.windows[w] = static_cast<int>(windows->array[w].integer);
    for (const auto& page : meta.Get("pages")->array)
        dump.pages.push_back(ReadTestData("sync/" + folder + "/page" + std::to_string(page.integer) + ".bin"));
    for (size_t k = 0; k < dump.pages.size(); ++k)
        dump.machine.ram.push_back({static_cast<uint16_t>(meta.Get("pages")->array[k].integer), dump.pages[k]});
    dump.expected = ReadTestData("sync/" + folder + "/" + meta.Get("expected")->string);
    dump.editor = meta.Get("state")->Get("editor")->boolean;
    dump.typing = meta.Get("state")->Get("typing")->boolean;
    return dump;
}

struct Case
{
    const char* folder;
    const char* descriptor;
};

const Case kCases[] = {
    {"alasm509-loaded", "alasm-5.09"}, {"alasm509-typing", "alasm-5.09"}, {"alasm509-edited", "alasm-5.09"},
    {"alasm444-loaded", "alasm-4.44"}, {"alasm444-edited", "alasm-4.44"},
    {"tasm412-top", "tasm-4.12"},      {"tasm412-middle", "tasm-4.12"},   {"tasm412-typing", "tasm-4.12"},
    {"tasm412-command", "tasm-4.12"},
};
}  // namespace

TEST(Sync_Test, EveryDumpGivesTheFileTheAssemblerSaved)
{
    for (const Case& c : kCases)
    {
        SCOPED_TRACE(c.folder);
        const Dump dump = Load(c.folder);
        const SyncDescriptor* descriptor = FindDescriptor(c.descriptor);
        ASSERT_NE(descriptor, nullptr);
        const SyncText text = ReadText(dump.machine, *descriptor);
        ASSERT_TRUE(text.ok) << (text.diagnostics.empty() ? text.state : text.diagnostics.back().message);
        EXPECT_EQ(text.file.size(), dump.expected.size());
        EXPECT_TRUE(text.file == dump.expected) << "the live file differs from the saved one";
        EXPECT_EQ(text.typing, dump.typing && descriptor->typing.rule == TypingRule::NotInText);
        if (descriptor->family == LayoutFamily::GapBuffer)
        {
            EXPECT_EQ(text.editor, dump.editor) << "the line buffer belongs in the file only in the editor";
        }

        // The live file is what the codec reads
        const ISourceCodec* codec = CodecRegistry::Builtin().Find(descriptor->codec);
        ASSERT_NE(codec, nullptr);
        EXPECT_TRUE(codec->Decode(text.file, {}).ok);
    }
}

TEST(Sync_Test, TheProbeNamesTheAssemblerAndNoOther)
{
    for (const Case& c : kCases)
    {
        SCOPED_TRACE(c.folder);
        const Dump dump = Load(c.folder);
        const std::vector<ProbeCandidate> candidates = Probe(dump.machine);
        ASSERT_EQ(candidates.size(), 1u);
        EXPECT_EQ(candidates[0].descriptor->id, c.descriptor);
        EXPECT_EQ(candidates[0].score, 100);
    }
}

TEST(Sync_Test, AlasmShowsTheEditedStateAndTheTextsName)
{
    const SyncText loaded = ReadText(Load("alasm509-loaded").machine, *FindDescriptor("alasm-5.09"));
    EXPECT_EQ(loaded.name, "SNAKE");
    EXPECT_EQ(loaded.page, 6) << "id #C6 of the 512K build on a 128K machine";
    EXPECT_FALSE(loaded.changed);
    EXPECT_FALSE(loaded.typing);
    const SyncText edited = ReadText(Load("alasm509-edited").machine, *FindDescriptor("alasm-5.09"));
    EXPECT_TRUE(edited.changed) << "T_OPT set by Enter, written as 0 by SAVE";
    EXPECT_EQ(edited.file.size(), loaded.file.size() + 1);
}

TEST(Sync_Test, TasmGivesTheCursorLineAndTheLineCount)
{
    const SyncText middle = ReadText(Load("tasm412-middle").machine, *FindDescriptor("tasm-4.12"));
    EXPECT_TRUE(middle.editor);
    EXPECT_EQ(middle.currentLine, 20);
    EXPECT_EQ(middle.lines, 104u);
    const SyncText command = ReadText(Load("tasm412-command").machine, *FindDescriptor("tasm-4.12"));
    EXPECT_FALSE(command.editor);
    EXPECT_EQ(command.currentLine, -1);
}

TEST(Sync_Test, BrokenPointersAreInconsistent)
{
    Dump dump = Load("tasm412-middle");
    // The gap start after the gap end
    std::vector<uint8_t>& page2 = dump.pages[0];
    page2[0x0F5D] = 0xFF;
    page2[0x0F5E] = 0xFE;
    SyncText text = ReadText(dump.machine, *FindDescriptor("tasm-4.12"));
    EXPECT_FALSE(text.ok);
    EXPECT_EQ(text.state, "inconsistent");

    // A line count that fits neither state
    dump = Load("tasm412-middle");
    dump.pages[0][0x14D9] = 50;
    text = ReadText(dump.machine, *FindDescriptor("tasm-4.12"));
    EXPECT_EQ(text.state, "inconsistent");

    // An ALASM page id that names a page without a text header
    dump = Load("alasm509-loaded");
    dump.pages[0][0x00CC] = 0x05;
    text = ReadText(dump.machine, *FindDescriptor("alasm-5.09"));
    EXPECT_EQ(text.state, "inconsistent");
    const std::vector<ProbeCandidate> candidates = Probe(dump.machine);
    ASSERT_EQ(candidates.size(), 1u);
    EXPECT_EQ(candidates[0].score, 60) << "identified, the text does not read";
}

TEST(Sync_Test, MemoryWithoutAnAssemblerProbesToNothing)
{
    const std::vector<uint8_t> zero(0x4000, 0);
    MachineView machine;
    machine.windows = {-1, 5, 2, 0};
    machine.ram = {{0, zero}, {2, zero}, {5, zero}};
    EXPECT_TRUE(Probe(machine).empty());
}

TEST(Sync_Test, TheExtendedPageIdFallsBackToThe128KPage)
{
    // On a 512K machine id #C6 names page 30 first; with only page 6 holding the text, page 6 is taken
    Dump dump = Load("alasm509-loaded");
    dump.machine.ramPages = 32;
    const SyncText text = ReadText(dump.machine, *FindDescriptor("alasm-5.09"));
    ASSERT_TRUE(text.ok);
    EXPECT_EQ(text.page, 6);
}

TEST(Sync_Test, TheLinearReaderCopiesFromStartToEndAndAddsTheMarker)
{
    std::vector<uint8_t> page(0x4000, 0);
    page[0x0000] = 0x10;   // start pointer #8010 at #8000
    page[0x0001] = 0x80;
    page[0x0002] = 0x14;   // end pointer #8014 at #8002
    page[0x0003] = 0x80;
    for (int k = 0; k < 4; ++k)
        page[0x10 + static_cast<size_t>(k)] = static_cast<uint8_t>('A' + k);
    MachineView machine;
    machine.windows = {-1, -1, 2, -1};
    machine.ram = {{2, page}};
    SyncDescriptor d;
    d.family = LayoutFamily::Linear;
    d.linear.startAt = 0x8000;
    d.linear.endAt = 0x8002;
    d.linear.end = std::string("\0\0", 2);
    const SyncText text = ReadText(machine, d);
    ASSERT_TRUE(text.ok);
    EXPECT_EQ(text.file, (std::vector<uint8_t>{'A', 'B', 'C', 'D', 0, 0}));

    std::vector<uint8_t> out;
    EXPECT_FALSE(machine.Read(0xBFFF, 2, out)) << "#C000 is in a window whose page was not copied";
}

// The watch (phase Y1): a change is built once typing has paused; the build gives the labels and the hints

TEST(SyncSession_Test, AChangeIsBuiltAfterThePauseAndOnlyOnce)
{
    SyncSession session;
    const Dump loaded = Load("alasm509-loaded");
    TickResult tick = session.Tick(loaded.machine, 1000);
    EXPECT_EQ(tick.event, TickEvent::Found);
    ASSERT_NE(session.Descriptor(), nullptr);
    EXPECT_EQ(session.Descriptor()->id, "alasm-5.09");
    EXPECT_FALSE(session.TakeBuild(1200)) << "still inside the quiet period";
    std::optional<BuildInput> input = session.TakeBuild(1500);
    ASSERT_TRUE(input);
    EXPECT_EQ(input->generation, 1u);
    EXPECT_FALSE(session.TakeBuild(9000)) << "nothing changed since";

    EXPECT_EQ(session.Tick(loaded.machine, 2000).event, TickEvent::None);
    // Typing a line ALASM keeps apart does not change the text; Enter does
    EXPECT_EQ(session.Tick(Load("alasm509-typing").machine, 2500).event, TickEvent::None);
    const Dump edited = Load("alasm509-edited");
    EXPECT_EQ(session.Tick(edited.machine, 3000).event, TickEvent::Changed);
    EXPECT_EQ(session.Tick(edited.machine, 3400).event, TickEvent::None);
    EXPECT_FALSE(session.TakeBuild(3400));
    input = session.TakeBuild(3500);
    ASSERT_TRUE(input);
    EXPECT_EQ(input->generation, 2u);
    EXPECT_TRUE(input->file == edited.expected);

    // The assembler gone: lost, and found again afterwards
    const std::vector<uint8_t> zero(0x4000, 0);
    MachineView empty;
    empty.windows = {-1, 5, 2, 6};
    empty.ram = {{2, zero}, {5, zero}, {6, zero}};
    EXPECT_EQ(session.Tick(empty, 4000).event, TickEvent::Lost);
    EXPECT_EQ(session.Descriptor(), nullptr);
    EXPECT_TRUE(session.NeedsAllPages());
    EXPECT_EQ(session.Tick(edited.machine, 5000).event, TickEvent::Found);
    EXPECT_FALSE(session.NeedsAllPages());
    EXPECT_EQ(session.TextPages(), std::vector<int>{6});
}

TEST(SyncSession_Test, TheBuildGivesTheLabelsOfTheLiveText)
{
    SyncSession session;
    session.Tick(Load("tasm412-typing").machine, 0);
    const std::optional<BuildInput> input = session.TakeBuild(1000);
    ASSERT_TRUE(input);
    const BuildResult built = SyncSession::Build(*input);
    ASSERT_TRUE(built.decoded);
    EXPECT_EQ(built.document.lines.size(), 104u);
    EXPECT_FALSE(built.labels.symbols.empty());
    bool key = false;
    for (const symbols::Symbol& s : built.labels.symbols)
        key = key || s.name == "KEY";
    EXPECT_TRUE(key) << "KEY = [#5C08] of SNAKE";
    // SNAKE's .IF inside DEFMAC is flat, as TASM reads it; the game it plays while assembling (keys, the screen)
    // keeps its "=" values moving, so the layout does not settle (labels still move)
    for (const Diagnostic& d : built.hints)
        EXPECT_EQ(d.message.find("IF without ENDIF"), std::string::npos) << d.message;
}

TEST(SyncSession_Test, AHintPointsAtTheSourceLine)
{
    // TASM text with an undefined name on its third line, as the TASM codec writes it
    const ISourceCodec* tasm = CodecRegistry::Builtin().Find("tasm");
    ASSERT_NE(tasm, nullptr);
    SourceDocument document = SourceDocument::FromText("        ORG     30000\n; a comment\nSTART   LD      HL,MISSING\n        RET", "tasm");
    EncodeOptions options;
    options.subversion = "4.12";
    const EncodeResult encoded = tasm->Encode(document, options);
    ASSERT_TRUE(encoded.ok);
    BuildInput input;
    input.descriptor = FindDescriptor("tasm-4.12");
    input.file = encoded.bytes;
    input.generation = 7;
    const BuildResult built = SyncSession::Build(input);
    ASSERT_TRUE(built.decoded);
    EXPECT_EQ(built.generation, 7u);
    EXPECT_FALSE(built.complete);
    bool found = false;
    for (const Diagnostic& d : built.hints)
        if (d.message.find("MISSING") != std::string::npos)
        {
            found = true;
            EXPECT_EQ(d.line, 3u) << d.message;
        }
    EXPECT_TRUE(found) << "the undefined name is reported";
}
