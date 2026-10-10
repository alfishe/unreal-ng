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
    {"alasm508-typing", "alasm-5.08"}, {"alasm508-edited", "alasm-5.08"}, {"alasm507-typing", "alasm-5.07"},
    {"alasm507-edited", "alasm-5.07"}, {"alasm505-typing", "alasm-5.05"}, {"alasm505-edited", "alasm-5.05"},
    {"alasm50-typing", "alasm-5.00"},  {"alasm50-edited", "alasm-5.00"},   {"alasm45-typing", "alasm-4.5"},
    {"alasm45-edited", "alasm-4.5"},   {"alasm446-typing", "alasm-4.46"},  {"alasm446-edited", "alasm-4.46"},
    {"alasm445-typing", "alasm-4.45"}, {"alasm445-edited", "alasm-4.45"},  {"alasm443-typing", "alasm-4.43"},
    {"alasm443-edited", "alasm-4.43"}, {"alasm442-typing", "alasm-4.42"},  {"alasm442-edited", "alasm-4.42"},
    {"alasm38c-typing", "alasm-3.8c"}, {"alasm38c-edited", "alasm-3.8c"},
    {"xas418-typing", "xas-4.18"},     {"xas418-edited", "xas-4.18"},      {"xas505-typing", "xas-5.05"},
    {"xas505-edited", "xas-5.05"},     {"xas505se-typing", "xas-5.05"},    {"xas505se-edited", "xas-5.05"},
    {"xas743c-typing", "xas-7.43c"},   {"xas743c-edited", "xas-7.43c"},    {"xas7447-typing", "xas-7.447"},
    {"xas7447-edited", "xas-7.447"},   {"xas907m-typing", "xas-9.07m"},    {"xas910-typing", "xas-9.10"},
    {"xas907m-edited", "xas-9.07m"},   {"xas910-edited", "xas-9.10"},
    {"storm13-typing", "storm-1.3"},   {"storm13-edited", "storm-1.3"},    {"storm13i-typing", "storm-1.3"},
    {"storm13i-edited", "storm-1.3"},     {"storm10b-typing", "storm-1.0b"},  {"storm10b-edited", "storm-1.0b"},
    {"zasm315-typing", "zasm-3.15"},   {"zasm315-edited", "zasm-3.15"},    {"zasm315-big", "zasm-3.15"},
    {"zasm310-typing", "zasm-3.10"},   {"zasm310-edited", "zasm-3.10"},    {"zasm310-big", "zasm-3.10"},
    {"zasm32x-typing", "zasm-3.2x"},   {"zasm32x-edited", "zasm-3.2x"},    {"zasm32x-big", "zasm-3.2x"},
    {"zlite107-typing", "zasm-lite-1.07"}, {"zlite107-edited", "zasm-lite-1.07"}, {"zlite107-big", "zasm-lite-1.07"},
    {"zasm3302-typing", "zasm-3.3.02"}, {"zasm3302-edited", "zasm-3.3.02"}, {"zasm3302-big", "zasm-3.3.02"},
    {"zasm3351-typing", "zasm-3.3.51"}, {"zasm3351-edited", "zasm-3.3.51"}, {"zasm3351-big", "zasm-3.3.51"},
    {"zasm33f-typing", "zasm-3.3.final"}, {"zasm33f-edited", "zasm-3.3.final"}, {"zasm33f-big", "zasm-3.3.final"},
    {"zasm384-typing", "zasm-3.80.4"}, {"zasm384-edited", "zasm-3.80.4"}, {"zasm384-big", "zasm-3.80.4"},
    {"zasm420-typing", "zasm-4.20"}, {"zasm420-edited", "zasm-4.20"}, {"zasm420-big", "zasm-4.20"},
    {"zasm34-typing", "zasm-3.4"}, {"zasm34-edited", "zasm-3.4"}, {"zasm34-big", "zasm-3.4"},
    {"zasmx641-typing", "zasm-x64.1"}, {"zasmx641-edited", "zasm-x64.1"}, {"zasmx641-big", "zasm-x64.1"},
    {"zasm40x8-typing", "zasm-4.0x8"}, {"zasm40x8-edited", "zasm-4.0x8"}, {"zasm40x8-big", "zasm-4.0x8"},
    {"zasm4x64-typing", "zasm-4.x64"}, {"zasm4x64-edited", "zasm-4.x64"}, {"zasm4x64-big", "zasm-4.x64"},
    {"zasm24-typing", "zasm-2.4"}, {"zasm24-edited", "zasm-2.4"}, {"zasm24-big", "zasm-2.4"},
    {"zasm25-typing", "zasm-2.5"}, {"zasm25-edited", "zasm-2.5"}, {"zasm25-big", "zasm-2.5"},
    {"zasm26-typing", "zasm-2.6"}, {"zasm26-edited", "zasm-2.6"}, {"zasm26-big", "zasm-2.6"},
    {"zasm30-typing", "zasm-3.0"},     {"zasm30-edited", "zasm-3.0"},      {"zasm30-big", "zasm-3.0"},
    {"masm11-typing", "masm-1.1"},     {"masm11-edited", "masm-1.1"},      {"masm11-menu", "masm-1.1"},
    {"masm20-typing", "masm-2.0"},     {"masm20-edited", "masm-2.0"},      {"masm20-menu", "masm-2.0"},
    {"masm30-typing", "masm-3.0"},     {"masm30-edited", "masm-3.0"},      {"masm30-menu", "masm-3.0"},
    {"masm13-typing", "masm-1.1"},     {"masm13-edited", "masm-1.1"},      {"masm13-menu", "masm-1.1"},
    {"tasm40-typing", "tasm-4.0"},     {"tasm40-edited", "tasm-4.0"},      {"tasm40-command", "tasm-4.0"},
    {"tasm44-typing", "tasm-4.4"},     {"tasm44-edited", "tasm-4.4"},      {"tasm44-command", "tasm-4.4"},
    {"tasm30-typing", "tasm-3.0"},     {"tasm30-edited", "tasm-3.0"},      {"tasm30-command", "tasm-3.0"},
    {"tasm32-typing", "tasm-3.2"},     {"tasm32-edited", "tasm-3.2"},      {"tasm32-command", "tasm-3.2"},
};

/// The saved file without the editor-state first line SAVE adds (ZAsm's ";!...")
std::vector<uint8_t> WithoutStateLine(std::vector<uint8_t> file, const SyncDescriptor& d)
{
    const std::string& prefix = d.linear.stateLine;
    if (d.family == LayoutFamily::Linear && !prefix.empty() && file.size() >= prefix.size() &&
        std::equal(prefix.begin(), prefix.end(), file.begin()))
    {
        const auto end = std::find(file.begin(), file.end(), uint8_t('\r'));
        if (end != file.end())
            file.erase(file.begin(), end + 1);
    }
    return file;
}

/// The file without the header bytes SAVE rewrites from the editor's state (XAS: the cursor line and column)
std::vector<uint8_t> WithoutEditorState(std::vector<uint8_t> file, const SyncDescriptor& d)
{
    const bool gap = d.family == LayoutFamily::GapBuffer;
    const size_t at = gap ? d.gapBuffer.editorStateAt : d.fileImage.editorStateAt;
    const size_t length = gap ? d.gapBuffer.editorStateLength : d.fileImage.editorStateLength;
    for (size_t k = at; k < at + length && k < file.size(); ++k)
        file[k] = 0;
    return file;
}
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
        const std::vector<uint8_t> expected = WithoutStateLine(dump.expected, *descriptor);
        EXPECT_EQ(text.file.size(), expected.size());
        EXPECT_TRUE(WithoutEditorState(text.file, *descriptor) == WithoutEditorState(expected, *descriptor))
            << "the live file differs from the saved one";
        EXPECT_EQ(text.typing, dump.typing && descriptor->typing.rule == TypingRule::NotInText && descriptor->typing.flagMask);
        if (descriptor->family == LayoutFamily::GapBuffer)
        {
            // MASM 2.0 / 3.0 take the cursor line out of the text only while it is typed in
            const bool lineOut = descriptor->gapBuffer.nextLineAt ? dump.typing : dump.editor;
            EXPECT_EQ(text.editor, lineOut) << "the line buffer belongs in the file only in the editor";
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
        std::string ids;
        for (const ProbeCandidate& candidate : candidates)
            ids += " " + candidate.descriptor->id;
        ASSERT_EQ(candidates.size(), 1u) << "identified:" << ids;
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
