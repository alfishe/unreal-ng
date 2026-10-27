// The source an assembler holds in RAM (asm-synchronizer.md §10): golden dumps (testdata/sync) of ALASM 5.09 / 4.44
// and TASM 4.12 sessions in unreal-ng, each with the file the assembler itself saved at that moment. The reader must
// give that file from the pages byte for byte, and the probe must name the assembler and no other.

#include <gtest/gtest.h>

#include <algorithm>
#include <cctype>
#include <initializer_list>
#include <map>
#include <optional>

#include "codecs/alasm/alasmcodec.h"
#include "codecs/prometheus/prometheuscodec.h"
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
    {"pasm30-loaded", "pasm-3.0"},    {"pasm30-typing", "pasm-3.0"},     {"pasm30-edited", "pasm-3.0"},
    {"prom48-edited", "prometheus-48"}, {"prom48-typing", "prometheus-48"},
    {"lg104-loaded", "lasergenius-1.04"}, {"lg104-typing", "lasergenius-1.04"}, {"lg104-edited", "lasergenius-1.04"},
    {"zeus1983-loaded", "zeus-1983"}, {"zeus1983-typing", "zeus-1983"},
    {"zeus11-loaded", "zeus-1.1"}, {"zeus11-typing", "zeus-1.1"},
    {"zeusgg-loaded", "zeus-gg"}, {"zeusgg-typing", "zeus-gg"},
    {"zeus7e-loaded", "zeus-7e"}, {"zeus7e-typing", "zeus-7e"},
    {"primus29-loaded", "primus-2.9"}, {"primus29-typing", "primus-2.9"}, {"primus29-edited", "primus-2.9"},
    {"gens4-loaded", "gens-4"},        {"gens4-typing", "gens-4"},
    {"gens3m21-loaded", "gens-3m21"},  {"gens3m21-typing", "gens-3m21"},
    {"gens4b-typing", "gens-4b"},     {"gens4b-edited", "gens-4b"},
    {"gens43t-typing", "gens-4.3t"},   {"gens43t-edited", "gens-4.3t"},
    {"zasm24-typing", "zasm-2.4"}, {"zasm24-edited", "zasm-2.4"}, {"zasm24-big", "zasm-2.4"},
    {"zasm25-typing", "zasm-2.5"}, {"zasm25-edited", "zasm-2.5"}, {"zasm25-big", "zasm-2.5"},
    {"zasm26-typing", "zasm-2.6"}, {"zasm26-edited", "zasm-2.6"}, {"zasm26-big", "zasm-2.6"},
    {"zasm30-typing", "zasm-3.0"},     {"zasm30-edited", "zasm-3.0"},      {"zasm30-big", "zasm-3.0"},
    {"masm11-typing", "masm-1.1"},     {"masm11-edited", "masm-1.1"},      {"masm11-menu", "masm-1.1"},
    {"masm10-typing", "masm-1.0-demo"}, {"masm10-edited", "masm-1.0-demo"}, {"masm10-menu", "masm-1.0-demo"},
    {"masm20-typing", "masm-2.0"},     {"masm20-edited", "masm-2.0"},      {"masm20-menu", "masm-2.0"},
    {"masm30-typing", "masm-3.0"},     {"masm30-edited", "masm-3.0"},      {"masm30-menu", "masm-3.0"},
    {"masm13-typing", "masm-1.1"},     {"masm13-edited", "masm-1.1"},      {"masm13-menu", "masm-1.1"},
    {"tasm40-typing", "tasm-4.0"},     {"tasm40-edited", "tasm-4.0"},      {"tasm40-command", "tasm-4.0"},
    {"tasm44-typing", "tasm-4.4"},     {"tasm44-edited", "tasm-4.4"},      {"tasm44-command", "tasm-4.4"},
    {"tasm30-typing", "tasm-3.0"},     {"tasm30-edited", "tasm-3.0"},      {"tasm30-command", "tasm-3.0"},
    {"tasm20-typing", "tasm-2.0"},     {"tasm20-edited", "tasm-2.0"},      {"tasm20-command", "tasm-2.0"},
    {"tasm35-typing", "tasm-3.5"},  {"tasm35-edited", "tasm-3.5"},   {"tasm35-command", "tasm-3.5"},
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
    const bool gap = d.family == LayoutFamily::GapBuffer, linear = d.family == LayoutFamily::Linear;
    const size_t at = gap ? d.gapBuffer.editorStateAt : linear ? d.linear.editorStateAt : d.fileImage.editorStateAt;
    const size_t length = gap ? d.gapBuffer.editorStateLength : linear ? d.linear.editorStateLength : d.fileImage.editorStateLength;
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

namespace
{
std::vector<uint8_t> XasFile(const std::string& text, const SyncDescriptor& d)
{
    const ISourceCodec* xas = CodecRegistry::Builtin().Find("xas");
    EncodeOptions options;
    options.subversion = d.version;
    const EncodeResult encoded = xas->Encode(SourceDocument::FromText(text, "xas"), options);
    EXPECT_TRUE(encoded.ok);
    return encoded.bytes;
}

bool CaseInsensitiveEqual(const std::string& a, const std::string& b)
{
    return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(), [](char x, char y) {
               return std::tolower(static_cast<unsigned char>(x)) == std::tolower(static_cast<unsigned char>(y));
           });
}

/// XAS keeps its labels in lower case
bool HasLabel(const BuildResult& built, const std::string& name, int64_t value)
{
    for (const symbols::Symbol& s : built.labels.symbols)
        if (CaseInsensitiveEqual(s.name, name) && s.location.offset == static_cast<uint32_t>(value))
            return true;
    return false;
}
}  // namespace

// Y5: the live text INCLUDEs another source; the build takes it from the project (asm-synchronizer.md §5.4)
TEST(SyncSession_Test, AnIncludedSourceComesFromTheProject)
{
    const SyncDescriptor* d = FindDescriptor("xas-7.447");
    ASSERT_NE(d, nullptr);
    BuildInput input;
    input.descriptor = d;
    input.name = "main";
    input.file = XasFile("        ORG   30000\n        LTEXT \"lib\"\nSTART   LD    HL,VALUE\n        RET", *d);

    const BuildResult alone = SyncSession::Build(input);
    ASSERT_TRUE(alone.decoded);
    EXPECT_FALSE(alone.complete) << "lib is not there";

    const ISourceCodec* xas = CodecRegistry::Builtin().Find("xas");
    DecodeOptions decode;
    decode.subversion = d->version;
    const DecodeResult lib = xas->Decode(XasFile("VALUE   EQU   1234", *d), decode);
    ASSERT_TRUE(lib.ok);
    input.project.push_back({"lib", lib.document});
    // A stale copy of the main text on the disk: the live text wins
    const DecodeResult stale = xas->Decode(XasFile("        ORG   40000\nSTART   RET", *d), decode);
    input.project.push_back({"MAIN", stale.document});

    const BuildResult built = SyncSession::Build(input);
    ASSERT_TRUE(built.decoded);
    EXPECT_TRUE(built.complete);
    EXPECT_TRUE(HasLabel(built, "VALUE", 1234));
    EXPECT_TRUE(HasLabel(built, "START", 30000)) << "the live text, not the disk's copy";
}

// Y5: ALASM keeps one text per page; the other pages' texts are the project's sources, named by their headers
TEST(SyncSession_Test, AlasmsOtherTextsInMemoryJoinTheProject)
{
    Dump dump = Load("alasm509-loaded");
    const SyncDescriptor* d = FindDescriptor("alasm-5.09");
    ASSERT_NE(d, nullptr);
    const SyncText live = ReadText(dump.machine, *d);
    ASSERT_TRUE(live.ok);
    // A second text: the live page copied to page 1 under the name LIB
    std::vector<uint8_t> second(dump.machine.Page(live.page)->bytes.begin(), dump.machine.Page(live.page)->bytes.end());
    const std::string name = "LIB     ";
    std::copy(name.begin(), name.end(), second.begin());
    dump.machine.ram.push_back({1, second});
    EXPECT_TRUE(ReadOtherTexts(dump.machine, *d, -1).size() == 2u) << "both pages hold a text";
    const std::vector<SyncText> others = ReadOtherTexts(dump.machine, *d, live.page);
    ASSERT_EQ(others.size(), 1u);
    EXPECT_EQ(others[0].name, "LIB");
    EXPECT_EQ(others[0].page, 1);
    const std::vector<ProjectFile> project = SyncSession::MemoryProject(dump.machine, *d, live.page);
    ASSERT_EQ(project.size(), 1u);
    EXPECT_EQ(project[0].name, "LIB");
    EXPECT_FALSE(project[0].document.lines.empty());
    // A layout without texts per page has none
    EXPECT_TRUE(ReadOtherTexts(dump.machine, *FindDescriptor("tasm-4.12"), -1).empty());
}

// Part B (Z0): a file written over ALASM 5.09's text at its command line, then read back as the reader reads it
TEST(SyncInject_Test, AlasmTakesAFileAtItsCommandLine)
{
    Dump dump = Load("alasm509-loaded");
    const SyncDescriptor* d = FindDescriptor("alasm-5.09");
    ASSERT_NE(d, nullptr);
    SourceDocument document = SourceDocument::FromText("        ORG #6000\nSTART   LD A,1\n        RET\n", "alasm");
    document.name = "NEW";
    EncodeOptions encode;
    encode.subversion = d->version;
    const std::vector<uint8_t> file = codecs::AlasmCodec().Encode(document, encode).bytes;
    ASSERT_FALSE(file.empty());

    // No stack copied: whether ALASM waits for a command is not known
    EXPECT_FALSE(PlanInject(dump.machine, *d, file).ok);

    // ALASM's command line: the line input's return (#91E3) and the command loop's (#9707) above SP
    auto withStack = [&](uint16_t loop) {
        std::vector<uint8_t> page5(0x4000, 0);
        const std::vector<uint16_t> words = {0x5C78, 0x0042, 0x9084, 0x9043, 0x8EF2, 0xBF15, 0x7FFD, 0x9199, 0x91E3, loop};
        for (size_t k = 0; k < words.size(); ++k)
        {
            page5[0x1FBE + 2 * k] = static_cast<uint8_t>(words[k]);
            page5[0x1FBE + 2 * k + 1] = static_cast<uint8_t>(words[k] >> 8);
        }
        return page5;
    };
    const std::vector<uint8_t> busyStack = withStack(0x9733);   // a command running (the editor, ASSEMBLE)
    dump.machine.ram.push_back({5, busyStack});
    dump.machine.sp = 0x5FBE;
    const InjectPlan busy = PlanInject(dump.machine, *d, file);
    EXPECT_FALSE(busy.ok);
    EXPECT_NE(busy.error.find("not waiting for a command"), std::string::npos) << busy.error;

    const std::vector<uint8_t> idleStack = withStack(0x9707);
    dump.machine.ram.back() = {5, idleStack};
    const InjectPlan plan = PlanInject(dump.machine, *d, file);
    ASSERT_TRUE(plan.ok) << plan.error;
    EXPECT_EQ(plan.page, 6);
    EXPECT_TRUE(plan.previous.ok);
    EXPECT_EQ(plan.previous.name, "SNAKE");

    // Written: the reader gives the file back, marked changed since saved
    for (symbols::MemoryPage& page : dump.machine.ram)
        if (page.page == 6)
            page.bytes = plan.bytes;
    const SyncText back = ReadText(dump.machine, *d);
    ASSERT_TRUE(back.ok);
    EXPECT_EQ(back.file, file);
    EXPECT_EQ(back.name, "NEW");
    EXPECT_TRUE(back.changed);

    // That text now has changes not saved: another write needs force
    EXPECT_FALSE(PlanInject(dump.machine, *d, file).ok);
    InjectOptions force;
    force.force = true;
    EXPECT_TRUE(PlanInject(dump.machine, *d, file, force).ok);
}

// The idle rule of every ALASM build: the stack words read in unreal-ng at the A> prompt and in the editor (ALASM
// running from its own disk, research-sjasmplus-to-alasm.md §4). The prompt is idle, the editor is not
TEST(SyncInject_Test, EveryAlasmBuildIsIdleAtItsPromptAndBusyInItsEditor)
{
    struct Sample
    {
        const char* id;
        std::vector<uint16_t> prompt, editor;
    };
    const std::vector<Sample> samples = {
        {"alasm-5.09", {0x5C78, 0x0042, 0x9083, 0x9043, 0x8EF2, 0xBF15, 0x7FFD, 0x9199, 0x91E3, 0x9707, 0x2D2B},
         {0x5C78, 0x0042, 0x9083, 0x9043, 0x914A, 0xC000, 0x06FD, 0x9199, 0x914D, 0x944D, 0x9F97, 0x9733, 0x2D2B}},
        {"alasm-5.08", {0x5C78, 0x0042, 0x9084, 0x9043, 0x8EF2, 0xBF15, 0x7FFD, 0x919E, 0x91E8, 0x970C, 0x2D2B},
         {0x5C78, 0x0042, 0x9083, 0x9043, 0x914F, 0xC000, 0x06FD, 0x919E, 0x9152, 0x9452, 0x9F9C, 0x9738, 0x2D2B}},
        {"alasm-5.07", {0x5C78, 0x0042, 0x9084, 0x9043, 0x8EF2, 0xBF15, 0x7FFD, 0x9190, 0x91DA, 0x96FE, 0x2D2B},
         {0x5C78, 0x0042, 0x9083, 0x9043, 0x9141, 0xC000, 0x06FD, 0x9190, 0x9144, 0x9444, 0x9F8E, 0x972A, 0x2D2B}},
        {"alasm-5.05", {0x5C78, 0x0042, 0x9084, 0x9043, 0x8EF2, 0xBF15, 0x7FFD, 0x9190, 0x91DA, 0x96FE, 0x2D2B},
         {0x5C78, 0x0042, 0x9084, 0x9043, 0x9141, 0xC000, 0x06FD, 0x9190, 0x9144, 0x9444, 0x9F8D, 0x972A, 0x2D2B}},
        {"alasm-5.00", {0x5C78, 0x0042, 0x9929, 0x98E8, 0xA0EE, 0xBF15, 0x7FFD, 0xA844, 0x99EA, 0x8B93, 0x2D2B},
         {0x5C78, 0x0042, 0x9928, 0x98E8, 0x95D0, 0xC000, 0x06FD, 0xA844, 0x95D3, 0x9DB8, 0xA3F1, 0x8BBF, 0x2D2B}},
        {"alasm-4.5", {0x985A, 0xA0F9, 0xBF15, 0x0F01, 0xA810, 0x998F, 0x8B6B, 0x2D2B},
         {0x985A, 0x954A, 0xC000, 0x0101, 0xA810, 0x954D, 0x9D8B, 0xA394, 0x8B98, 0x2D2B}},
        {"alasm-4.46", {0x98EC, 0xA15F, 0xBF15, 0x7FFD, 0xA869, 0x9A07, 0x8B92, 0x2D2B},
         {0x98EC, 0x95BC, 0xC000, 0x06FD, 0xA869, 0x95BF, 0x9DEA, 0xA406, 0x8BBE, 0x2D2B}},
        {"alasm-4.45", {0x98C8, 0xA147, 0xBF15, 0x7FFD, 0xA875, 0x99E1, 0x8B93, 0x2D2B},
         {0x98C8, 0x95BE, 0xC000, 0x06FD, 0xA875, 0x95C1, 0x9DD8, 0xA3EF, 0x8BC0, 0x2D2B}},
        {"alasm-4.44", {0x98EF, 0xA16E, 0xBF15, 0x7FFD, 0xA89E, 0x9A08, 0x8BA8, 0x2D2B},
         {0x98EF, 0x95E2, 0xC000, 0x06FD, 0xA89E, 0x95E5, 0x9E03, 0xA418, 0x8BD5, 0x2D2B}},
        {"alasm-4.43", {0x986F, 0xA10D, 0xBF15, 0x7FFD, 0xA81A, 0x99A4, 0x8B99, 0x2D2B},
         {0x986F, 0x955F, 0xC000, 0x06FD, 0xA81A, 0x9562, 0x9DA0, 0xA3A8, 0x8BC6, 0x2D2B}},
        {"alasm-4.42", {0x7FFD, 0x987D, 0xA121, 0xBA97, 0x7FFD, 0xA8A0, 0x99B2, 0x8B8F, 0x2D2B},
         {0x987D, 0x955D, 0xC000, 0x06FD, 0xA8A0, 0x9560, 0x9DB4, 0xA3C8, 0x8BBC, 0x2D2B}},
        {"alasm-4.2", {0x970E, 0x9F49, 0x9F23, 0x7FFD, 0xA5FE, 0x983F, 0x8B84, 0x5BEF, 0x2D2B},
         {0x970E, 0xB627, 0xB627, 0x0000, 0xA5FE, 0xA5C3, 0xA1FC, 0x8BAD, 0x5BEF, 0x2D2B}},
        {"alasm-3.8c", {0x0000, 0x9175, 0x9B9C, 0x9B74, 0x0000, 0xA1D1, 0x943A, 0x8149, 0x1B76, 0x1303},
         {0x0600, 0x9175, 0x9883, 0xC000, 0x0600, 0xA1D1, 0x8DD9, 0x9837, 0x9DBE, 0x816D, 0x1B76}},
    };
    for (const Sample& sample : samples)
    {
        const SyncDescriptor* d = FindDescriptor(sample.id);
        ASSERT_NE(d, nullptr) << sample.id;
        ASSERT_FALSE(d->idleChain.empty()) << sample.id << " has an idle rule";
        for (const bool atPrompt : {true, false})
        {
            const std::vector<uint16_t>& words = atPrompt ? sample.prompt : sample.editor;
            std::vector<uint8_t> page5(0x4000, 0);
            for (size_t k = 0; k < words.size(); ++k)
            {
                page5[0x1FB0 + 2 * k] = static_cast<uint8_t>(words[k]);
                page5[0x1FB0 + 2 * k + 1] = static_cast<uint8_t>(words[k] >> 8);
            }
            MachineView machine;
            machine.ram.push_back({5, page5});
            machine.windows = {-1, 5, 2, 0};
            machine.sp = 0x5FB0;
            std::string why;
            EXPECT_EQ(Idle(machine, *d, why), atPrompt) << sample.id << (atPrompt ? " at its prompt: " : " in its editor: ") << why;
        }
    }
}

namespace
{
/// A project on the host, in memory: path -> text
DataFileReader HostFiles(const std::map<std::string, std::string>& files, std::vector<std::string>* asked = nullptr)
{
    return [files, asked](const std::string& path) {
        if (asked)
            asked->push_back(path);
        const auto it = files.find(path);
        return it == files.end() ? std::vector<uint8_t>() : std::vector<uint8_t>(it->second.begin(), it->second.end());
    };
}

bool HasLine(const SourceDocument& document, const std::string& text)
{
    return std::any_of(document.lines.begin(), document.lines.end(), [&](const SourceLine& l) { return l.text.find(text) != std::string::npos; });
}
}  // namespace

// The sources a host file INCLUDEs, each read relative to the folder of the file that INCLUDEs it
TEST(SyncInject_Test, AHostProjectReadsTheIncludedSourcesFromTheirFolders)
{
    SourceDocument main = SourceDocument::FromText("        INCLUDE \"lib/one.asm\"\n        INCLUDE \"two.inc\"\n        INCLUDE \"gone.asm\"\n", "sjasmplus");
    main.name = "MAIN";
    std::vector<std::string> asked;
    Diagnostics diagnostics;
    const std::vector<ProjectFile> project =
        HostProject(main, HostFiles({{"lib/one.asm", " INCLUDE \"deep.asm\"\n NOP\n"}, {"lib/deep.asm", " RET\n"}, {"two.inc", " HALT\n"}}, &asked), diagnostics);
    ASSERT_EQ(project.size(), 4u);
    EXPECT_EQ(project[0].name, "MAIN");
    EXPECT_EQ(project[1].name, "lib/one");   // as the IR names it (sjasmplus: no .asm)
    EXPECT_EQ(project[2].name, "two.inc");
    EXPECT_EQ(project[3].name, "deep");
    EXPECT_EQ(project[3].document.dialect, "sjasmplus");
    EXPECT_NE(std::find(asked.begin(), asked.end(), "lib/deep.asm"), asked.end()) << "deep.asm is beside the file that INCLUDEs it";
    ASSERT_EQ(diagnostics.size(), 1u);
    EXPECT_NE(diagnostics[0].message.find("gone.asm"), std::string::npos) << diagnostics[0].message;
    EXPECT_EQ(ProjectTextNames(project, "MAIN"), (std::vector<std::string>{"MAIN", "ONE", "TWO", "DEEP"}));
}

// A project into ALASM 5.09: the main source over its text, each INCLUDEd source into a page ALASM leaves free (4, 0),
// the INCLUDEs naming those pages, so ALASM finds the texts loaded and never reads the disk
TEST(SyncInject_Test, AProjectPutsEachIncludedSourceInAPageOfItsOwn)
{
    Dump dump = Load("alasm509-loaded");
    const SyncDescriptor* d = FindDescriptor("alasm-5.09");
    ASSERT_NE(d, nullptr);
    std::vector<uint8_t> page5(0x4000, 0);   // the A> prompt's stack (EveryAlasmBuildIsIdleAtItsPromptAndBusyInItsEditor)
    const std::vector<uint16_t> words = {0x9199, 0x91E3, 0x9707, 0x2D2B};
    for (size_t k = 0; k < words.size(); ++k)
    {
        page5[0x1FB0 + 2 * k] = static_cast<uint8_t>(words[k]);
        page5[0x1FB0 + 2 * k + 1] = static_cast<uint8_t>(words[k] >> 8);
    }
    const std::vector<uint8_t> page4(0x4000, 0), page0(0x4000, 0xFF);
    dump.machine.ram.push_back({5, page5});
    dump.machine.ram.push_back({4, page4});
    dump.machine.ram.push_back({0, page0});
    dump.machine.sp = 0x5FB0;

    SourceDocument main = SourceDocument::FromText(
        "        ORG #6000\n        LD A,1\n        INCLUDE \"lib/inc1.asm\"\n        INCLUDE \"inc2.asm\"\n        DB 9\nEND_    NOP\n", "sjasmplus");
    main.name = "MAIN";
    Diagnostics diagnostics;
    const std::vector<ProjectFile> project =
        HostProject(main, HostFiles({{"lib/inc1.asm", "        LD B,2\nL1      DW END_\n"}, {"inc2.asm", "        LD C,3\n        JP L1\n"}}), diagnostics);
    ASSERT_EQ(project.size(), 3u);
    const ProjectInjectPlan plan = PlanInjectProject(dump.machine, *d, project, "MAIN");
    ASSERT_TRUE(plan.ok) << plan.error;
    ASSERT_EQ(plan.pages.size(), 3u);
    EXPECT_EQ(plan.pages[0].page, 6);
    EXPECT_EQ(plan.pages[1].page, 4);
    EXPECT_EQ(plan.pages[2].page, 0);
    EXPECT_TRUE(HasLine(plan.texts[0].document, "INCLUDE \"INC1\",4"));
    EXPECT_TRUE(HasLine(plan.texts[0].document, "INCLUDE \"INC2\",0"));
    EXPECT_TRUE(HasLine(plan.texts[2].document, "JP L1"));

    // Written: each page holds its text under its name, marked changed since saved
    for (const InjectPlan& page : plan.pages)
        for (symbols::MemoryPage& ram : dump.machine.ram)
            if (ram.page == page.page)
                ram.bytes = page.bytes;
    const std::vector<SyncText> others = ReadOtherTexts(dump.machine, *d, 6);
    ASSERT_EQ(others.size(), 2u);
    for (const SyncText& text : others)
    {
        EXPECT_EQ(text.name, text.page == 4 ? "INC1" : "INC2");
        EXPECT_EQ(text.file, plan.texts[text.page == 4 ? 1 : 2].bytes);
        EXPECT_TRUE(text.changed);
    }

    // Those texts now have changes not saved: another write needs force
    const ProjectInjectPlan again = PlanInjectProject(dump.machine, *d, project, "MAIN");
    EXPECT_FALSE(again.ok);
    InjectOptions force;
    force.force = true;
    EXPECT_TRUE(PlanInjectProject(dump.machine, *d, project, "MAIN", force).ok);

    // More sources than free pages, or a build without project pages: refused before anything is written
    std::vector<ProjectFile> three = project;
    three.push_back({"inc3", three[2].document});
    const ProjectInjectPlan tooMany = PlanInjectProject(dump.machine, *d, three, "MAIN", force);
    EXPECT_FALSE(tooMany.ok);
    EXPECT_NE(tooMany.error.find("keeps 2 more text(s) in memory (pages 4, 0)"), std::string::npos) << tooMany.error;
    SyncDescriptor withoutPages = *d;
    withoutPages.projectPages.clear();
    const ProjectInjectPlan none = PlanInjectProject(dump.machine, withoutPages, project, "MAIN", force);
    EXPECT_FALSE(none.ok);
    EXPECT_NE(none.error.find("keeps no other text"), std::string::npos) << none.error;
}

// Part B for XAS (Z3): at its command line the file goes over the text at #C000, and the editor variables its LOAD
// sets (the cursor line, the top line, a flag, the text end: operands in its code) are found in the code and set
TEST(SyncInject_Test, XasTakesAFileAndItsEditorVariablesAtItsCommandLine)
{
    Dump dump = Load("xas7447-edited");
    const SyncDescriptor* d = FindDescriptor("xas-7.447");
    ASSERT_NE(d, nullptr);
    std::vector<uint8_t> page5(0x4000, 0);
    const std::vector<uint16_t> words = {0x8EF8, 0x090A, 0x9DDC, 0x8E5B, 0xD2F1, 0x2D2B};   // its command line (EXT)
    for (size_t k = 0; k < words.size(); ++k)
    {
        page5[0x1FB0 + 2 * k] = static_cast<uint8_t>(words[k]);
        page5[0x1FB0 + 2 * k + 1] = static_cast<uint8_t>(words[k] >> 8);
    }
    dump.machine.ram.push_back({5, page5});
    dump.machine.windows[1] = 5;
    dump.machine.sp = 0x5FB0;
    std::string why;
    ASSERT_TRUE(Idle(dump.machine, *d, why)) << why;

    const WriteAsResult written = WriteAs(SourceDocument::FromText("        ORG #6000\nstart   LD A,1\n        JR start\n", "sjasmplus"), "xas", d->version, "NEW");
    ASSERT_TRUE(written.ok) << written.error;
    EXPECT_FALSE(PlanInject(dump.machine, *d, written.bytes).ok) << "the edited text has changes not saved";
    InjectOptions force;
    force.force = true;
    const InjectPlan plan = PlanInject(dump.machine, *d, written.bytes, force);
    ASSERT_TRUE(plan.ok) << plan.error;
    EXPECT_EQ(plan.page, 3);
    // #8097 (the cursor line) = the header's #C023, #84E0 / #8D19 from the header, #8495 = the address of the #00 end
    std::map<uint16_t, uint8_t> pokes;
    for (const InjectPlan::Poke& p : plan.pokes)
    {
        EXPECT_EQ(p.page, 2);
        pokes[static_cast<uint16_t>(0x8000 + p.offset)] = p.value;
    }
    const auto word = [&](uint16_t at) { return pokes.count(at) && pokes.count(at + 1) ? pokes[at] | (pokes[at + 1] << 8) : -1; };
    EXPECT_EQ(word(0x8097), 0xC023);
    EXPECT_EQ(word(0x84E0), written.bytes[31] | (written.bytes[32] << 8));
    EXPECT_EQ(pokes.count(0x8D19), 1u);
    const size_t end = static_cast<size_t>(std::find(written.bytes.begin() + 36, written.bytes.end(), uint8_t(0)) - written.bytes.begin());
    EXPECT_EQ(word(0x8495), static_cast<int>(0xC000 + end));

    // Written: the reader gives the file back
    for (symbols::MemoryPage& page : dump.machine.ram)
        if (page.page == 3)
            page.bytes = plan.bytes;
    const SyncText back = ReadText(dump.machine, *d);
    ASSERT_TRUE(back.ok);
    EXPECT_EQ(std::vector<uint8_t>(back.file.begin() + 36, back.file.begin() + static_cast<std::ptrdiff_t>(end)),
              std::vector<uint8_t>(written.bytes.begin() + 36, written.bytes.begin() + static_cast<std::ptrdiff_t>(end)));

    // Code without the routines (another program at #C000): refused
    Dump other = Load("xas7447-edited");
    std::fill(other.pages[0].begin(), other.pages[0].end(), uint8_t{0});
    other.machine.ram.push_back({5, page5});
    other.machine.windows[1] = 5;
    other.machine.sp = 0x5FB0;
    const InjectPlan none = PlanInject(other.machine, *d, written.bytes, force);
    EXPECT_FALSE(none.ok);
    EXPECT_NE(none.error.find("editor variables were not found"), std::string::npos) << none.error;
}

TEST(SyncInject_Test, Zasm315TakesAFileAtItsCommandPromptAsLoadLeavesIt)
{
    Dump dump = Load("zasm315-edited");
    const SyncDescriptor* d = FindDescriptor("zasm-3.15");
    ASSERT_NE(d, nullptr);
    // its COMMAND prompt: the key wait returns through #C404, #E630, #E6F0
    const std::vector<uint16_t> words = {0x5CDD, 0xC421, 0xC41D, 0xC404, 0xE630, 0xE6F0, 0xDFF0, 0x7A16};
    for (size_t k = 0; k < dump.pages.size(); ++k)
        if (dump.machine.ram[k].page == 5)
            for (size_t w = 0; w < words.size(); ++w)
            {
                dump.pages[k][0x1FF0 + 2 * w] = static_cast<uint8_t>(words[w]);
                dump.pages[k][0x1FF0 + 2 * w + 1] = static_cast<uint8_t>(words[w] >> 8);
            }
    dump.machine.sp = 0x5FF0;
    std::string why;
    ASSERT_TRUE(Idle(dump.machine, *d, why)) << why;

    const WriteAsResult written = WriteAs(SourceDocument::FromText("        ORG #6000\nstart   LD A,1\n        JR start\n", "sjasmplus"), "zxasm", d->version, "NEW");
    ASSERT_TRUE(written.ok) << written.error;
    // ZAsm keeps no "changed" flag: a text it holds is replaced by force (or when it is the caller's own) only
    const InjectPlan refused = PlanInject(dump.machine, *d, written.bytes);
    EXPECT_FALSE(refused.ok);
    InjectOptions force;
    force.force = true;
    const InjectPlan plan = PlanInject(dump.machine, *d, written.bytes, force);
    ASSERT_TRUE(plan.ok) << plan.error;
    std::map<uint16_t, uint8_t> pokes;
    for (const InjectPlan::Poke& p : plan.pokes)
        pokes[static_cast<uint16_t>((p.page == 2 ? 0x8000 : p.page == 5 ? 0x4000 : 0xC000) + p.offset)] = p.value;
    for (size_t k = 0; k < written.bytes.size(); ++k)
        EXPECT_EQ(pokes[static_cast<uint16_t>(0x884C + k)], written.bytes[k]) << k;
    const auto word = [&](uint16_t at) { return pokes[at] | (pokes[at + 1] << 8); };
    EXPECT_EQ(word(0x8837), static_cast<int>(0x884C + written.bytes.size()));   // the end
    for (uint16_t at = 0x882B; at < 0x8837; at += 2)
        EXPECT_EQ(word(at), 0x884C) << std::hex << at;   // the cursor line and the marks at the start
    EXPECT_TRUE(pokes.count(0x8827) && word(0x8827) == 0);   // the cursor's line number
    EXPECT_TRUE(pokes.count(0x8630) && pokes[0x8630] == 0);  // and its column

    for (size_t k = 0; k < dump.pages.size(); ++k)
        for (const InjectPlan::Poke& p : plan.pokes)
            if (dump.machine.ram[k].page == p.page)
                dump.pages[k][p.offset] = p.value;
    const SyncText back = ReadText(dump.machine, *d);
    ASSERT_TRUE(back.ok) << back.state;
    EXPECT_EQ(back.file, written.bytes);
    InjectOptions own;
    own.written = {written.bytes};
    EXPECT_TRUE(PlanInject(dump.machine, *d, written.bytes, own).ok) << "its own text: replaced without force";
}

TEST(SyncInject_Test, Primus29TakesAFileAtItsPrompt)
{
    Dump dump = Load("primus29-loaded");
    const SyncDescriptor* d = FindDescriptor("primus-2.9");
    ASSERT_NE(d, nullptr);
    // its prompt: the key wait returns through #6857, then #6836 (page 5 holds the stack at #617E)
    const std::vector<uint16_t> words = {0x6CE7, 0x4702, 0x6857, 0x6836, 0x67C8};
    for (size_t k = 0; k < dump.pages.size(); ++k)
        if (dump.machine.ram[k].page == 5)
            for (size_t w = 0; w < words.size(); ++w)
            {
                dump.pages[k][0x217E + 2 * w] = static_cast<uint8_t>(words[w]);
                dump.pages[k][0x217E + 2 * w + 1] = static_cast<uint8_t>(words[w] >> 8);
            }
    dump.machine.sp = 0x617E;
    std::string why;
    ASSERT_TRUE(Idle(dump.machine, *d, why)) << why;
    const WriteAsResult written = WriteAs(SourceDocument::FromText("        ORG #6000\nstart   LD A,1\n        JR start\n", "sjasmplus"), "zeus", d->version, "NEW");
    ASSERT_TRUE(written.ok) << written.error;
    EXPECT_FALSE(PlanInject(dump.machine, *d, written.bytes).ok) << "no changed flag: its loaded text needs force";
    InjectOptions force;
    force.force = true;
    const InjectPlan plan = PlanInject(dump.machine, *d, written.bytes, force);
    ASSERT_TRUE(plan.ok) << plan.error;
    for (size_t k = 0; k < dump.pages.size(); ++k)
        for (const InjectPlan::Poke& p : plan.pokes)
            if (dump.machine.ram[k].page == p.page)
                dump.pages[k][p.offset] = p.value;
    const SyncText back = ReadText(dump.machine, *d);
    ASSERT_TRUE(back.ok) << back.state;
    EXPECT_EQ(back.file, written.bytes);
    EXPECT_EQ(*dump.machine.Word(0x61B3), static_cast<uint16_t>(0x8254 + written.bytes.size() - 1));   // the last FF
}

TEST(SyncInject_Test, Zeus1983TakesAFileAtItsPromptAsOLeavesIt)
{
    Dump dump = Load("zeus1983-loaded");
    const SyncDescriptor* d = FindDescriptor("zeus-1983");
    ASSERT_NE(d, nullptr);
    // its prompt: the line input returns through #F6E5, #E0D5 (page 0 holds the stack at #FF26)
    const std::vector<uint16_t> words = {0xF680, 0x0000, 0xE0F0, 0xF6E5, 0xE0D5, 0x0044};
    for (size_t k = 0; k < dump.pages.size(); ++k)
        if (dump.machine.ram[k].page == 0)
            for (size_t w = 0; w < words.size(); ++w)
            {
                dump.pages[k][0x3F26 + 2 * w] = static_cast<uint8_t>(words[w]);
                dump.pages[k][0x3F26 + 2 * w + 1] = static_cast<uint8_t>(words[w] >> 8);
            }
    dump.machine.sp = 0xFF26;
    std::string why;
    ASSERT_TRUE(Idle(dump.machine, *d, why)) << why;

    const WriteAsResult written = WriteAs(SourceDocument::FromText("        ORG #6000\nstart   LD A,1\n        JR start\n", "sjasmplus"), "zeus", d->version, "NEW");
    ASSERT_TRUE(written.ok) << written.error;
    // no "changed" flag: the loaded text is replaced by force (or when it is the caller's own) only
    EXPECT_FALSE(PlanInject(dump.machine, *d, written.bytes).ok);
    InjectOptions force;
    force.force = true;
    const InjectPlan plan = PlanInject(dump.machine, *d, written.bytes, force);
    ASSERT_TRUE(plan.ok) << plan.error;
    const uint16_t start = static_cast<uint16_t>(*dump.machine.Word(0xFD00));
    std::map<uint16_t, uint8_t> pokes;
    for (const InjectPlan::Poke& p : plan.pokes)
        pokes[static_cast<uint16_t>((p.page == 2 ? 0x8000 : p.page == 5 ? 0x4000 : 0xC000) + p.offset)] = p.value;
    for (size_t k = 0; k < written.bytes.size(); ++k)
        EXPECT_EQ(pokes[static_cast<uint16_t>(start + k)], written.bytes[k]) << k;
    EXPECT_EQ(pokes[0xFD02] | (pokes[0xFD03] << 8), static_cast<int>(start + written.bytes.size() - 1));   // the last FF

    for (size_t k = 0; k < dump.pages.size(); ++k)
        for (const InjectPlan::Poke& p : plan.pokes)
            if (dump.machine.ram[k].page == p.page)
                dump.pages[k][p.offset] = p.value;
    const SyncText back = ReadText(dump.machine, *d);
    ASSERT_TRUE(back.ok) << back.state;
    EXPECT_EQ(back.file, written.bytes);
}

TEST(SyncInject_Test, LaserGeniusTakesItsParagraphsInItsEditor)
{
    Dump dump = Load("lg104-loaded");
    const SyncDescriptor* d = FindDescriptor("lasergenius-1.04");
    ASSERT_NE(d, nullptr);
    // its editor: the key wait returns to #AB04, then the ROM's #1303 (page 5 holds the stack at #638C, an interrupt
    // frame on top)
    const std::vector<uint16_t> words = {0xB0A7, 0x0044, 0xB07A, 0x0000, 0x4800, 0xAB04, 0x1303};
    for (size_t k = 0; k < dump.pages.size(); ++k)
        if (dump.machine.ram[k].page == 5)
            for (size_t w = 0; w < words.size(); ++w)
            {
                dump.pages[k][0x238C + 2 * w] = static_cast<uint8_t>(words[w]);
                dump.pages[k][0x238C + 2 * w + 1] = static_cast<uint8_t>(words[w] >> 8);
            }
    dump.machine.sp = 0x638C;
    std::string why;
    ASSERT_TRUE(Idle(dump.machine, *d, why)) << why;

    const WriteAsResult written = WriteAs(SourceDocument::FromText("        ORG #6900\nstart   LD A,1\n        JR start\n", "sjasmplus"), "lasergenius", d->version, "NEW");
    ASSERT_TRUE(written.ok) << written.error;
    // no "changed" flag: the loaded text is replaced by force (or when it is the caller's own) only
    EXPECT_FALSE(PlanInject(dump.machine, *d, written.bytes).ok);
    InjectOptions force;
    force.force = true;
    const InjectPlan plan = PlanInject(dump.machine, *d, written.bytes, force);
    ASSERT_TRUE(plan.ok) << plan.error;
    std::map<uint16_t, uint8_t> pokes;
    for (const InjectPlan::Poke& p : plan.pokes)
        pokes[static_cast<uint16_t>((p.page == 2 ? 0x8000 : p.page == 5 ? 0x4000 : 0xC000) + p.offset)] = p.value;
    // memory holds the paragraphs (the codec's bytes) and FF FF from (#A975) = #7F2E; the end word after them
    const std::vector<uint8_t>& paragraphs = written.bytes;
    for (size_t k = 0; k < paragraphs.size(); ++k)
        EXPECT_EQ(pokes[static_cast<uint16_t>(0x7F2E + k)], paragraphs[k]) << k;
    EXPECT_EQ(pokes[static_cast<uint16_t>(0x7F2E + paragraphs.size())], 0xFF);
    EXPECT_EQ(pokes[static_cast<uint16_t>(0x7F2E + paragraphs.size() + 1)], 0xFF);
    const uint16_t end = static_cast<uint16_t>(0x7F2E + paragraphs.size() + 2);
    EXPECT_EQ(pokes[0xA973] | (pokes[0xA974] << 8), end);
    // its screen lines (FF, the lines, FF at (#A9DC)) move behind the new text, the pointers into them too
    const uint16_t oldEnd = *dump.machine.Word(0xA973), oldLast = *dump.machine.Word(0xA9DC);
    std::vector<uint8_t> screen;
    ASSERT_TRUE(dump.machine.Read(oldEnd, static_cast<size_t>(oldLast - oldEnd + 1), screen));
    EXPECT_EQ(screen.front(), 0xFF);
    EXPECT_EQ(screen.back(), 0xFF);
    for (size_t k = 0; k < screen.size(); ++k)
        EXPECT_EQ(pokes[static_cast<uint16_t>(end + k)], screen[k]) << k;
    EXPECT_EQ(pokes[0xA9DC] | (pokes[0xA9DD] << 8), oldLast - oldEnd + end);

    for (size_t k = 0; k < dump.pages.size(); ++k)
        for (const InjectPlan::Poke& p : plan.pokes)
            if (dump.machine.ram[k].page == p.page)
                dump.pages[k][p.offset] = p.value;
    const SyncText back = ReadText(dump.machine, *d);
    ASSERT_TRUE(back.ok) << back.state;
    // the disk file: #AF, the length, the start, four characters of the name (blanks: SAVE gives them), the paragraphs
    std::vector<uint8_t> expected = {0xAF, static_cast<uint8_t>(paragraphs.size()), static_cast<uint8_t>(paragraphs.size() >> 8), 0x2E, 0x7F, ' ', ' ', ' ', ' '};
    expected.insert(expected.end(), paragraphs.begin(), paragraphs.end());
    EXPECT_EQ(back.file, expected);

    // a disk file (with its header) goes in the same way
    const InjectPlan fromDisk = PlanInject(dump.machine, *d, expected, force);
    ASSERT_TRUE(fromDisk.ok) << fromDisk.error;
    EXPECT_EQ(fromDisk.pokes.size(), plan.pokes.size());
    // its text space ends at (#A971) (#9F2E: 8K; SET SPACE changes it)
    const InjectPlan tooLong = PlanInject(dump.machine, *d, std::vector<uint8_t>(0x9F2E - 0x7F2E, 0x00), force);
    EXPECT_FALSE(tooLong.ok);
    EXPECT_NE(tooLong.error.find("text space"), std::string::npos) << tooLong.error;
}

TEST(SyncInject_Test, PrometheusTakesASaveInItsEditor)
{
    Dump dump = Load("prom48-edited");
    const SyncDescriptor* d = FindDescriptor("prometheus-48");
    ASSERT_NE(d, nullptr);
    // its editor waits for a key: the ROM's key scan returns to #86B2, a word of its own, then #8629 and #7CFD (page 2
    // holds the stack at #8B99)
    const std::vector<uint16_t> words = {0x86B2, 0x01BA, 0x8629, 0x7CFD};
    for (size_t k = 0; k < dump.pages.size(); ++k)
        if (dump.machine.ram[k].page == 2)
            for (size_t w = 0; w < words.size(); ++w)
            {
                dump.pages[k][0x0B99 + 2 * w] = static_cast<uint8_t>(words[w]);
                dump.pages[k][0x0B99 + 2 * w + 1] = static_cast<uint8_t>(words[w] >> 8);
            }
    dump.machine.sp = 0x8B99;
    std::string why;
    ASSERT_TRUE(Idle(dump.machine, *d, why)) << why;

    const WriteAsResult written = WriteAs(SourceDocument::FromText("        ORG 60000\nstart   LD A,1\n        JR start\n", "sjasmplus"), "prometheus", d->version, "NEW");
    ASSERT_TRUE(written.ok) << written.error;
    // no "changed" flag: the typed text is replaced by force (or when it is the caller's own) only
    EXPECT_FALSE(PlanInject(dump.machine, *d, written.bytes).ok);
    InjectOptions force;
    force.force = true;
    const InjectPlan plan = PlanInject(dump.machine, *d, written.bytes, force);
    ASSERT_TRUE(plan.ok) << plan.error;
    std::map<uint16_t, uint8_t> pokes;
    for (const InjectPlan::Poke& p : plan.pokes)
        pokes[static_cast<uint16_t>((p.page == 5 ? 0x4000 : p.page == 2 ? 0x8000 : 0xC000) + p.offset)] = p.value;
    // the records from the access line (#9C28), six empty records, the symbol table; the words after them
    size_t length = 0;
    ASSERT_TRUE(codecs::PrometheusCodec::FindSourceLength(written.bytes, length));
    for (size_t k = 0; k < length; ++k)
        EXPECT_EQ(pokes[static_cast<uint16_t>(0x9C28 + k)], written.bytes[k]) << k;
    for (size_t k = 0; k < 12; ++k)
        EXPECT_EQ(pokes[static_cast<uint16_t>(0x9C28 + length + k)], k % 2 ? 0x30 : 0x00) << k;
    const uint16_t table = static_cast<uint16_t>(0x9C28 + length + 12);
    const size_t tableLength = written.bytes.size() - length - 2;
    for (size_t k = 0; k < tableLength; ++k)
        EXPECT_EQ(pokes[static_cast<uint16_t>(table + k)], written.bytes[length + 2 + k]) << k;
    EXPECT_EQ(pokes[0x87D7] | (pokes[0x87D8] << 8), table);
    EXPECT_EQ(static_cast<size_t>(pokes[0x881A] | (pokes[0x881B] << 8)), table + tableLength);
    // the active line and the block margins at the start, as CLEAR leaves them
    for (const uint16_t at : std::initializer_list<uint16_t>{0x826D, 0x80E8, 0x80EB})
        EXPECT_EQ(pokes[at] | (pokes[static_cast<uint16_t>(at + 1)] << 8), 0x9C28) << at;

    for (size_t k = 0; k < dump.pages.size(); ++k)
        for (const InjectPlan::Poke& p : plan.pokes)
            if (dump.machine.ram[k].page == p.page)
                dump.pages[k][p.offset] = p.value;
    // read back as SAVE writes it: the file written
    const SyncText back = ReadText(dump.machine, *d);
    ASSERT_TRUE(back.ok) << back.state;
    EXPECT_EQ(back.file, written.bytes);

    // the text and its names stay below U-TOP (#7946)
    for (size_t k = 0; k < dump.pages.size(); ++k)
        if (dump.machine.ram[k].page == 5)
        {
            dump.pages[k][0x3946] = 0x30;
            dump.pages[k][0x3947] = 0x9C;
        }
    const InjectPlan tooLong = PlanInject(dump.machine, *d, written.bytes, force);
    EXPECT_FALSE(tooLong.ok);
    EXPECT_NE(tooLong.error.find("U-TOP"), std::string::npos) << tooLong.error;
}

TEST(SyncInject_Test, PasmTakesATextInItsEditorAsGetLeavesIt)
{
    Dump dump = Load("pasm30-edited");
    const Dump loaded = Load("pasm30-loaded");
    const SyncDescriptor* d = FindDescriptor("pasm-3.0");
    ASSERT_NE(d, nullptr);
    // its editor waits for a key: the ROM's key scan, then #73CF (the key wait called at #73CC) and #61A6 (page 5 holds
    // the stack at #5FD3)
    const std::vector<uint16_t> words = {0x0E6A, 0x6E5C, 0x6635, 0x660D, 0x69B9, 0x5800, 0x0000, 0x0007, 0x73CF, 0x61A6};
    for (size_t k = 0; k < dump.pages.size(); ++k)
        if (dump.machine.ram[k].page == 5)
            for (size_t w = 0; w < words.size(); ++w)
            {
                dump.pages[k][0x1FD3 + 2 * w] = static_cast<uint8_t>(words[w]);
                dump.pages[k][0x1FD3 + 2 * w + 1] = static_cast<uint8_t>(words[w] >> 8);
            }
    dump.machine.sp = 0x5FD3;
    std::string why;
    ASSERT_TRUE(Idle(dump.machine, *d, why)) << why;

    // the text PASM saved after loading TEST: its first line packed into TABs
    const std::vector<uint8_t>& file = loaded.expected;
    // no "changed" flag: the edited text is replaced by force (or when it is the caller's own) only
    EXPECT_FALSE(PlanInject(dump.machine, *d, file).ok);
    InjectOptions force;
    force.force = true;
    force.name = "INJECTED";
    const InjectPlan plan = PlanInject(dump.machine, *d, file, force);
    ASSERT_TRUE(plan.ok) << plan.error;
    for (size_t k = 0; k < dump.pages.size(); ++k)
        for (const InjectPlan::Poke& p : plan.pokes)
            if (dump.machine.ram[k].page == p.page)
                dump.pages[k][p.offset] = p.value;
    const auto byte = [](const Dump& from, uint16_t at) {
        std::vector<uint8_t> b;
        EXPECT_TRUE(from.machine.Read(at, 1, b));
        return b.empty() ? 0 : b[0];
    };
    const auto word = [&](const Dump& from, uint16_t at) { return byte(from, at) | (byte(from, static_cast<uint16_t>(at + 1)) << 8); };
    // the text and its #1A from #79AF
    for (size_t k = 0; k < file.size(); ++k)
        EXPECT_EQ(byte(dump, static_cast<uint16_t>(0x79AF + k)), file[k]) << k;
    EXPECT_EQ(byte(dump, static_cast<uint16_t>(0x79AF + file.size())), 0x1A);
    EXPECT_EQ(word(dump, 0x7747), static_cast<int>(file.size()));   // its length
    EXPECT_EQ(word(dump, 0x7802), 10);                              // the cursor line's: "\tORG #6000"
    // as GET left the editor: the last line's number, the cursor on the first line, its line buffer, the marks
    for (const uint16_t at : std::initializer_list<uint16_t>{0x7752, 0x775B, 0x7383, 0x6525, 0x63CA, 0x6C54, 0x6DFE, 0x6E00, 0x74E9, 0x76E9})
        EXPECT_EQ(word(dump, at), word(loaded, at)) << std::hex << at;
    for (uint16_t at = 0x7342; at < 0x7342 + 64; ++at)
        EXPECT_EQ(byte(dump, at), byte(loaded, at)) << std::hex << at;
    for (const uint16_t at : std::initializer_list<uint16_t>{0x6CD0, 0x6D31, 0x6B48})
        EXPECT_EQ(byte(dump, at), 0) << std::hex << at;
    EXPECT_EQ(byte(dump, 0x73B6), 0xFF);   // redrawn on the editor's next entry
    std::vector<uint8_t> name;
    ASSERT_TRUE(dump.machine.Read(0x7391, 8, name));
    EXPECT_EQ(std::string(name.begin(), name.end()), "INJECTED");
    // read back as SAVE writes it: the file written
    const SyncText back = ReadText(dump.machine, *d);
    ASSERT_TRUE(back.ok) << back.state;
    EXPECT_EQ(back.file, file);
    // now the caller's own: replaced without force
    InjectOptions own;
    own.written = {file};
    EXPECT_TRUE(PlanInject(dump.machine, *d, file, own).ok);

    // the text stays below #C000
    const std::string line = "        NOP\r\n";
    std::vector<uint8_t> large;
    while (large.size() < 0xC000 - 0x79AF + 16)
        large.insert(large.end(), line.begin(), line.end());
    const InjectPlan tooLong = PlanInject(dump.machine, *d, large, force);
    EXPECT_FALSE(tooLong.ok);
    EXPECT_NE(tooLong.error.find("#C000"), std::string::npos) << tooLong.error;
}

TEST(SyncInject_Test, Gens4TakesAFileAtItsPromptAsGLeavesIt)
{
    Dump dump = Load("gens4-loaded");
    const SyncDescriptor* d = FindDescriptor("gens-4");
    ASSERT_NE(d, nullptr);
    // its > prompt: the key wait returns through #685D, #68DD, #67D9 (page 2 holds the stack at #89DB)
    const std::vector<uint16_t> words = {0x5F54, 0x8927, 0x0100, 0x1F40, 0x685D, 0x68DD, 0x67D9, 0xA203};
    for (size_t k = 0; k < dump.pages.size(); ++k)
        if (dump.machine.ram[k].page == 2)
            for (size_t w = 0; w < words.size(); ++w)
            {
                dump.pages[k][0x09DB + 2 * w] = static_cast<uint8_t>(words[w]);
                dump.pages[k][0x09DB + 2 * w + 1] = static_cast<uint8_t>(words[w] >> 8);
            }
    dump.machine.sp = 0x89DB;
    std::string why;
    ASSERT_TRUE(Idle(dump.machine, *d, why)) << why;

    const WriteAsResult written = WriteAs(SourceDocument::FromText("        ORG #6000\nstart   LD A,1\n        JR start\n", "sjasmplus"), "gens", d->version, "NEW");
    ASSERT_TRUE(written.ok) << written.error;
    // GENS keeps no "changed" flag: the loaded text is replaced by force (or when it is the caller's own) only
    EXPECT_FALSE(PlanInject(dump.machine, *d, written.bytes).ok);
    InjectOptions force;
    force.force = true;
    const InjectPlan plan = PlanInject(dump.machine, *d, written.bytes, force);
    ASSERT_TRUE(plan.ok) << plan.error;
    const uint16_t start = static_cast<uint16_t>(*dump.machine.Word(0x67A8));
    std::map<uint16_t, uint8_t> pokes;
    for (const InjectPlan::Poke& p : plan.pokes)
        pokes[static_cast<uint16_t>((p.page == 2 ? 0x8000 : p.page == 5 ? 0x4000 : 0xC000) + p.offset)] = p.value;
    for (size_t k = 0; k < written.bytes.size(); ++k)
        EXPECT_EQ(pokes[static_cast<uint16_t>(start + k)], written.bytes[k]) << k;
    EXPECT_EQ(pokes[0x65C6] | (pokes[0x65C7] << 8), static_cast<int>(start + written.bytes.size()));   // the end word

    for (size_t k = 0; k < dump.pages.size(); ++k)
        for (const InjectPlan::Poke& p : plan.pokes)
            if (dump.machine.ram[k].page == p.page)
                dump.pages[k][p.offset] = p.value;
    const SyncText back = ReadText(dump.machine, *d);
    ASSERT_TRUE(back.ok) << back.state;
    EXPECT_EQ(back.file, written.bytes);
}

TEST(SyncInject_Test, Masm11TakesAFileAtItsPromptAsLoadLeavesIt)
{
    const auto place = [](Dump& dump) {
        // the key wait's return #85C8 on the stack (page 5)
        for (size_t k = 0; k < dump.pages.size(); ++k)
            if (dump.machine.ram[k].page == 5)
            {
                dump.pages[k][0x1FEE] = 0xC8;
                dump.pages[k][0x1FEF] = 0x85;
            }
        dump.machine.sp = 0x5FEE;
    };
    const SyncDescriptor* d = FindDescriptor("masm-1.1");
    ASSERT_NE(d, nullptr);
    Dump editor = Load("masm11-edited");
    place(editor);
    std::string why;
    EXPECT_FALSE(Idle(editor.machine, *d, why)) << "the line buffer says the editor";
    Dump dump = Load("masm11-menu");
    place(dump);
    ASSERT_TRUE(Idle(dump.machine, *d, why)) << why;

    const WriteAsResult written = WriteAs(SourceDocument::FromText("        ORG #6000\nstart   LD A,1\n        JR start\n", "sjasmplus"), "masm", d->version, "NEW");
    ASSERT_TRUE(written.ok) << written.error;
    // MASM keeps no "changed" flag: the text it holds is replaced by force (or when it is the caller's own) only
    EXPECT_FALSE(PlanInject(dump.machine, *d, written.bytes).ok);
    InjectOptions force;
    force.force = true;
    const InjectPlan plan = PlanInject(dump.machine, *d, written.bytes, force);
    ASSERT_TRUE(plan.ok) << plan.error;
    std::map<uint16_t, uint8_t> pokes;
    for (const InjectPlan::Poke& p : plan.pokes)
        pokes[static_cast<uint16_t>((p.page == 2 ? 0x8000 : p.page == 5 ? 0x4000 : 0xC000) + p.offset)] = p.value;
    for (size_t k = 0; k < written.bytes.size(); ++k)
        EXPECT_EQ(pokes[static_cast<uint16_t>(0x970B + k)], written.bytes[k]) << k;
    EXPECT_EQ(pokes[0x96CC] | (pokes[0x96CD] << 8), 0x970B);   // the gap start at the start
    EXPECT_EQ(pokes[0x96CE] | (pokes[0x96CF] << 8), 0xFFFF);   // the gap end at #FFFF

    for (size_t k = 0; k < dump.pages.size(); ++k)
        for (const InjectPlan::Poke& p : plan.pokes)
            if (dump.machine.ram[k].page == p.page)
                dump.pages[k][p.offset] = p.value;
    const SyncText back = ReadText(dump.machine, *d);
    ASSERT_TRUE(back.ok) << back.state;
    EXPECT_EQ(back.file, written.bytes);
    InjectOptions own;
    own.written = {written.bytes};
    EXPECT_TRUE(PlanInject(dump.machine, *d, written.bytes, own).ok) << "its own text: replaced without force";
}

TEST(SyncInject_Test, Tasm412TakesAFileAtItsCommandLineAsLoadLeavesIt)
{
    Dump dump = Load("tasm412-command");
    const SyncDescriptor* d = FindDescriptor("tasm-4.12");
    ASSERT_NE(d, nullptr);
    const std::vector<uint8_t> page5(0x4000, 0);
    std::vector<uint8_t> stack = page5;
    // its command line: the key wait returns through #9CE9, #80BC
    const std::vector<uint16_t> words = {0x88FB, 0x8F63, 0x008C, 0x0526, 0x811A, 0x9CE9, 0x80BC, 0x2758};
    for (size_t k = 0; k < words.size(); ++k)
    {
        stack[0x217C + 2 * k] = static_cast<uint8_t>(words[k]);
        stack[0x217C + 2 * k + 1] = static_cast<uint8_t>(words[k] >> 8);
    }
    dump.machine.ram.push_back({5, stack});
    dump.machine.windows[1] = 5;
    dump.machine.sp = 0x617C;
    std::string why;
    ASSERT_TRUE(Idle(dump.machine, *d, why)) << why;

    const WriteAsResult written = WriteAs(SourceDocument::FromText("        ORG #6200\nstart   LD A,1\n        JR start\n", "sjasmplus"), "tasm", d->version, "NEW");
    ASSERT_TRUE(written.ok) << written.error;
    // the dump's text was saved: not changed (#9BA6 = 0), written without force
    const InjectPlan plan = PlanInject(dump.machine, *d, written.bytes);
    ASSERT_TRUE(plan.ok) << plan.error;
    EXPECT_EQ(plan.page, -1);
    std::map<uint16_t, uint8_t> pokes;   // by CPU address (page 2 at #8000, page 6 at #C000)
    for (const InjectPlan::Poke& p : plan.pokes)
        pokes[static_cast<uint16_t>((p.page == 2 ? 0x8000 : 0xC000) + p.offset)] = p.value;
    for (size_t k = 0; k < written.bytes.size(); ++k)
        EXPECT_EQ(pokes[static_cast<uint16_t>(0xA6EE + k)], written.bytes[k]) << k;
    for (const uint16_t at : std::initializer_list<uint16_t>{0x8F5B, 0x8F5D, 0x8F5F, 0x94D9, 0x94CE})
        EXPECT_EQ(pokes.count(at) && pokes[at] == 0 && pokes[at + 1] == 0, true) << std::hex << at;
    EXPECT_EQ(pokes[0x9BA6], 0xFF);   // changed: TASM asks before it drops the text
    EXPECT_EQ(pokes.count(0x9C10), 0u) << "no name given: the work name stays";
    InjectOptions named;
    named.name = "NEW";
    const InjectPlan withName = PlanInject(dump.machine, *d, written.bytes, named);
    ASSERT_TRUE(withName.ok);
    std::string work;
    for (const InjectPlan::Poke& p : withName.pokes)
        if (p.page == 2 && p.offset >= 0x1C10 && p.offset < 0x1C18)
            work.push_back(static_cast<char>(p.value));
    EXPECT_EQ(work, "NEW     ");   // the work name Assemble takes the text under

    // Written: the reader takes the form LOAD leaves
    for (size_t k = 0; k < dump.pages.size(); ++k)
        for (const InjectPlan::Poke& p : plan.pokes)
            if (dump.machine.ram[k].page == p.page)
                dump.pages[k][p.offset] = p.value;
    const SyncText back = ReadText(dump.machine, *d);
    ASSERT_TRUE(back.ok) << back.state;
    EXPECT_EQ(back.file, written.bytes);
    // now changed and its own: the next write is refused without the caller's earlier file, taken with it
    EXPECT_FALSE(PlanInject(dump.machine, *d, written.bytes).ok);
    InjectOptions own;
    own.written = {written.bytes};
    EXPECT_TRUE(PlanInject(dump.machine, *d, written.bytes, own).ok);

    // In the editor: refused
    dump.machine.sp = 0x617E;
    EXPECT_FALSE(PlanInject(dump.machine, *d, written.bytes).ok);
}

/// Every assembler the synchronizer reads takes a text too: an idle rule (where it waits) and a layout an inject writes
TEST(SyncInject_Test, EveryDescriptorHasAnIdleRuleAndAnInject)
{
    for (const SyncDescriptor& d : Descriptors())
    {
        SCOPED_TRACE(d.id);
        EXPECT_FALSE(d.idleChain.empty()) << "no idle rule";
        const bool injectable = d.family == LayoutFamily::FileImage || (d.family == LayoutFamily::Linear && d.linear.injectable) ||
                                (d.family == LayoutFamily::GapBuffer && (d.gapBuffer.loadedForm || d.gapBuffer.commandLineForm));
        EXPECT_TRUE(injectable) << "no inject";
    }
}

/// The rules added with the emulator checks (inject-oracle.py, 2026-10-16): on a dump of each, with its key wait put on
/// the stack, a text written by the inject reads back as written
TEST(SyncInject_Test, TheNewRulesWriteATextThatReadsBack)
{
    const Case cases[] = {
        {"masm20-menu", "masm-2.0"},       {"masm30-menu", "masm-3.0"},       {"masm10-menu", "masm-1.0-demo"},
        {"gens3m21-loaded", "gens-3m21"},  {"gens4b-edited", "gens-4b"},      {"gens43t-edited", "gens-4.3t"},
        {"zasm24-edited", "zasm-2.4"},     {"zasm25-edited", "zasm-2.5"},     {"zasm26-edited", "zasm-2.6"},
        {"zasm30-edited", "zasm-3.0"},     {"zasm310-edited", "zasm-3.10"},   {"zasm32x-edited", "zasm-3.2x"},
        {"zlite107-edited", "zasm-lite-1.07"}, {"zasm3302-edited", "zasm-3.3.02"}, {"zasm3351-edited", "zasm-3.3.51"},
        {"zasm33f-edited", "zasm-3.3.final"}, {"zasm384-edited", "zasm-3.80.4"}, {"zasm34-edited", "zasm-3.4"},
        {"zasmx641-edited", "zasm-x64.1"}, {"zasm40x8-edited", "zasm-4.0x8"},  {"zasm4x64-edited", "zasm-4.x64"},
        {"zasm420-edited", "zasm-4.20"},   {"zeusgg-loaded", "zeus-gg"},      {"zeus7e-loaded", "zeus-7e"},
        {"zeus11-loaded", "zeus-1.1"},
    };
    for (const Case& c : cases)
    {
        SCOPED_TRACE(c.folder);
        Dump dump = Load(c.folder);
        const SyncDescriptor* d = FindDescriptor(c.descriptor);
        ASSERT_NE(d, nullptr);
        // the key wait on a stack of its own in the page at #4000
        const int window = dump.machine.windows[1];
        std::vector<uint8_t>* stackPage = nullptr;
        for (size_t k = 0; k < dump.pages.size(); ++k)
            if (dump.machine.ram[k].page == window)
                stackPage = &dump.pages[k];
        ASSERT_NE(stackPage, nullptr);
        dump.machine.sp = 0x4800;
        for (size_t k = 0; k < d->idleChain.size(); ++k)
        {
            (*stackPage)[0x0800 + 2 * k] = static_cast<uint8_t>(d->idleChain[k]);
            (*stackPage)[0x0801 + 2 * k] = static_cast<uint8_t>(d->idleChain[k] >> 8);
        }
        if (d->idleZeroLength)
            for (size_t k = 0; k < dump.pages.size(); ++k)
                if (dump.machine.ram[k].page == dump.machine.windows[d->idleZeroAt >> 14])
                    std::fill_n(dump.pages[k].begin() + (d->idleZeroAt & 0x3FFF), d->idleZeroLength, uint8_t(0));
        std::string why;
        ASSERT_TRUE(Idle(dump.machine, *d, why)) << why;

        const WriteAsResult written = WriteAs(SourceDocument::FromText("        ORG #C000\nSTART   LD A,1\n        JR START\n", "sjasmplus"),
                                              d->codec, d->version, "NEW");
        ASSERT_TRUE(written.ok) << written.error;
        InjectOptions force;
        force.force = true;
        const InjectPlan plan = PlanInject(dump.machine, *d, written.bytes, force);
        ASSERT_TRUE(plan.ok) << plan.error;
        for (size_t k = 0; k < dump.pages.size(); ++k)
        {
            if (plan.page >= 0 && dump.machine.ram[k].page == plan.page)
                dump.pages[k] = plan.bytes;
            for (const InjectPlan::Poke& p : plan.pokes)
                if (dump.machine.ram[k].page == p.page)
                    dump.pages[k][p.offset] = p.value;
        }
        const SyncText back = ReadText(dump.machine, *d);
        ASSERT_TRUE(back.ok) << back.state;
        EXPECT_EQ(back.file, written.bytes);
        // ZAsm's editor starts over at the new text (seen in unreal-ng, 2026-10-17): its cursor line number 0 (or it
        // counts on from the old one and the cursor sticks), ZX-ASM 2.x's cursor line buffer blank (or the editor
        // writes the old line into the new text, beyond its end)
        static const std::map<std::string, uint16_t> lineAt = {
            {"zasm-2.4", 0x6282}, {"zasm-2.5", 0x6282}, {"zasm-2.6", 0x6012}, {"zasm-3.0", 0x61D6}, {"zasm-3.10", 0x868D},
            {"zasm-3.2x", 0x852E}, {"zasm-lite-1.07", 0x8593}, {"zasm-3.3.02", 0x8500}, {"zasm-3.3.51", 0x84F2},
            {"zasm-3.3.final", 0x83F7}, {"zasm-3.80.4", 0x6CF8}, {"zasm-3.4", 0x8516}, {"zasm-x64.1", 0x8523},
            {"zasm-4.0x8", 0x6921}, {"zasm-4.x64", 0x691F}, {"zasm-4.20", 0x68F1}};
        if (const auto found = lineAt.find(d->id); found != lineAt.end())
        {
            EXPECT_EQ(dump.machine.Word(found->second), std::optional<uint16_t>(0));
            // and its column: 3.0 #61D9, 3.10 #867F, the Pentagon 512 versions four bytes below the line number
            const uint16_t columnAt = d->id == "zasm-3.0" ? 0x61D9 : d->id == "zasm-3.10" ? 0x867F
                                    : d->id.rfind("zasm-2.", 0) == 0 ? static_cast<uint16_t>(found->second + 3)
                                                                       : static_cast<uint16_t>(found->second - 4);
            std::vector<uint8_t> column;
            ASSERT_TRUE(dump.machine.Read(columnAt, 1, column));
            EXPECT_EQ(column[0], 0) << std::hex << columnAt;
        }
        // the second end word (seen in unreal-ng: LOAD sets it; a line typed into the new text moved memory up to the
        // old one: 3.0 crashed, 2.x moved its own code)
        if (d->id == "zasm-3.0")
        {
            EXPECT_EQ(dump.machine.Word(0x61CA), dump.machine.Word(0x61C8));
        }
        if (d->id.rfind("zasm-2.", 0) == 0)
        {
            EXPECT_EQ(dump.machine.Word(static_cast<uint16_t>(d->linear.startAt + 4)), dump.machine.Word(d->linear.endAt));
        }
        if (d->id.rfind("zasm-2.", 0) == 0)
        {
            std::vector<uint8_t> line;
            ASSERT_TRUE(dump.machine.Read(static_cast<uint16_t>(d->linear.startAt + 0x17), d->id == "zasm-2.6" ? 133 : 145, line));
            EXPECT_EQ(line, std::vector<uint8_t>(line.size(), ' '));
        }
    }
}

TEST(SyncInject_Test, TheOlderTasmsTakeAFileAtTheirCommandLineAsTheEditorLeavesIt)
{
    struct Version
    {
        const char* folder;
        const char* descriptor;
        std::vector<uint16_t> chain;   // its key wait at the command line (find-idle.py)
        uint16_t pointers, start, changedAt, nameAt;
    };
    const Version versions[] = {
        {"tasm40-command", "tasm-4.0", {0x9865, 0x807C}, 0x8DD0, 0x9FA8, 0x975E, 0x9793},
        {"tasm44-command", "tasm-4.4", {0x9865, 0x807C}, 0x8DD0, 0x9FA8, 0x975E, 0x9793},
        {"tasm30-command", "tasm-3.0", {0x93D5, 0x807C}, 0x8910, 0x9935, 0x9322, 0x9346},
        {"tasm32-command", "tasm-3.2", {0x93D5, 0x807C}, 0x8910, 0x9935, 0x9322, 0x9346},
        {"tasm35-command", "tasm-3.5", {0x93D5, 0x807C}, 0x8910, 0x9935, 0x9322, 0x9346},
        {"tasm20-command", "tasm-2.0", {0x92CB, 0x807C}, 0x8847, 0x975D, 0x921D, 0x9241},
    };
    for (const Version& v : versions)
    {
        SCOPED_TRACE(v.descriptor);
        Dump dump = Load(v.folder);
        const SyncDescriptor* d = FindDescriptor(v.descriptor);
        ASSERT_NE(d, nullptr);
        const auto page = [&](int number) -> std::vector<uint8_t>& {
            for (size_t k = 0; k < dump.pages.size(); ++k)
                if (dump.machine.ram[k].page == number)
                    return dump.pages[k];
            ADD_FAILURE() << "no page " << number;
            return dump.pages[0];
        };
        const auto byte = [&](uint16_t at) -> uint8_t& { return page(dump.machine.windows[at >> 14])[at & 0x3FFF]; };
        std::string why;
        dump.machine.sp = 0x617C;
        for (size_t k = 0; k < v.chain.size(); ++k)
        {
            byte(static_cast<uint16_t>(0x6180 + 2 * k)) = static_cast<uint8_t>(v.chain[k]);
            byte(static_cast<uint16_t>(0x6181 + 2 * k)) = static_cast<uint8_t>(v.chain[k] >> 8);
        }
        ASSERT_TRUE(Idle(dump.machine, *d, why)) << why;
        byte(v.changedAt) = 0;   // as after SAVE

        const WriteAsResult written = WriteAs(SourceDocument::FromText("        ORG #6900\nstart   LD A,1\n        JR start\n", "sjasmplus"), "tasm", d->version, "NEW");
        ASSERT_TRUE(written.ok) << written.error;
        InjectOptions named;
        named.name = "NEW";
        const InjectPlan plan = PlanInject(dump.machine, *d, written.bytes, named);
        ASSERT_TRUE(plan.ok) << plan.error;
        for (const InjectPlan::Poke& p : plan.pokes)
            page(p.page)[p.offset] = p.value;
        const auto word = [&](uint16_t at) { return static_cast<uint16_t>(byte(at) | (byte(static_cast<uint16_t>(at + 1)) << 8)); };
        // the gap start on the end record (2.0: on the text's last byte), the gap end and the top #FFFF
        const bool plain = d->gapBuffer.plainTextFile;
        EXPECT_EQ(word(static_cast<uint16_t>(v.pointers + 4)), v.start + written.bytes.size() - (plain ? 0 : 2));
        EXPECT_EQ(word(static_cast<uint16_t>(v.pointers + 2)), 0xFFFF);
        EXPECT_EQ(word(static_cast<uint16_t>(v.pointers + 6)), 0xFFFF);
        EXPECT_EQ(byte(v.changedAt), 0xFF);
        std::string work;
        for (uint16_t k = 0; k < 8; ++k)
            work.push_back(static_cast<char>(byte(static_cast<uint16_t>(v.nameAt + k))));
        EXPECT_EQ(work, "NEW     ");
        const SyncText back = ReadText(dump.machine, *d);
        ASSERT_TRUE(back.ok) << back.state;
        EXPECT_FALSE(back.editor);
        EXPECT_EQ(back.file, written.bytes);
        // changed now: refused without force
        EXPECT_FALSE(PlanInject(dump.machine, *d, written.bytes).ok);
    }
}

/// A line of more than 96 characters left in the line buffer at the command line reaches into its tail: the editor is
/// known by the buffer's last byte only
TEST(Sync_Test, ALongLineLeftInTasmsLineBufferIsNotTheEditor)
{
    Dump dump = Load("tasm40-command");
    const SyncDescriptor* d = FindDescriptor("tasm-4.0");
    ASSERT_NE(d, nullptr);
    for (size_t k = 0; k < dump.pages.size(); ++k)
        if (dump.machine.ram[k].page == 2)
            std::fill(dump.pages[k].begin() + 0x114C, dump.pages[k].begin() + 0x114C + 110, uint8_t('x'));   // #914C: the line buffer
    const SyncText text = ReadText(dump.machine, *d);
    ASSERT_TRUE(text.ok) << text.state;
    EXPECT_FALSE(text.editor);
    EXPECT_EQ(text.file, dump.expected);
}

TEST(SyncInject_Test, StormTakesAFileWhileItsEditorWaitsForAKey)
{
    Dump dump = Load("storm13-edited");
    const SyncDescriptor* d = FindDescriptor("storm-1.3");
    ASSERT_NE(d, nullptr);
    const std::vector<uint8_t> page5(0x4000, 0);
    dump.machine.ram.push_back({5, page5});
    // the editor's main loop waits for a key: WTKPR returns to INP4 (#87F5)
    for (size_t k = 0; k < dump.pages.size(); ++k)
        if (dump.machine.ram[k].page == 2)
        {
            dump.pages[k][0x3F00] = 0xF5;
            dump.pages[k][0x3F01] = 0x87;
        }
    dump.machine.sp = 0xBF00;
    std::string why;
    ASSERT_TRUE(Idle(dump.machine, *d, why)) << why;

    const WriteAsResult written = WriteAs(SourceDocument::FromText("        ORG #6000\nstart   LD A,1\n        JR start\n", "sjasmplus"), "storm", d->version, "NEW");
    ASSERT_TRUE(written.ok) << written.error;
    const InjectPlan refused = PlanInject(dump.machine, *d, written.bytes);
    EXPECT_FALSE(refused.ok) << "a line was written since STORM loaded the text (MBSTAT bit 4)";
    EXPECT_NE(refused.error.find("changes not saved"), std::string::npos) << refused.error;
    InjectOptions force;
    force.force = true;
    const InjectPlan plan = PlanInject(dump.machine, *d, written.bytes, force);
    ASSERT_TRUE(plan.ok) << plan.error;
    EXPECT_EQ(plan.page, 6);
    // the text from #C00B, the #FF before and after it, (#C000) one past the end
    const size_t size = written.bytes.size();
    EXPECT_EQ(plan.bytes[0x0A], 0xFF);
    EXPECT_EQ(plan.bytes[0x0B + size], 0xFF);
    EXPECT_EQ(plan.bytes[0] | (plan.bytes[1] << 8), static_cast<int>(0xC00B + size + 1));
    // STORM 1.3's variables (its source's names): the second line under the cursor, line 1, PGUP pressed
    std::map<uint16_t, uint8_t> pokes;
    for (const InjectPlan::Poke& p : plan.pokes)
    {
        EXPECT_EQ(p.page, 2);
        pokes[static_cast<uint16_t>(0x8000 + p.offset)] = p.value;
    }
    const auto word = [&](uint16_t at) { return pokes.count(at) && pokes.count(at + 1) ? pokes[at] | (pokes[at + 1] << 8) : -1; };
    size_t second = size - 1;   // walked back from the end to the line that starts the file
    while (second - (written.bytes[second] & 0x3F) > 0)
        second -= (written.bytes[second] & 0x3F) + 1;
    EXPECT_EQ(word(0xB8EF), static_cast<int>(0xC00B + second + 1));   // ABLINE
    EXPECT_EQ(word(0xB8ED), 1);                                         // NLINE
    EXPECT_EQ(word(0xBF94), 0);                                         // CACHEST, MBSTAT
    EXPECT_EQ(pokes[0xB8E4], 1);                                        // PY
    EXPECT_EQ(pokes[0xBFC2], 4);                                        // LASTK: PGUP
    EXPECT_EQ(pokes[0xB8E2] & 1, 1);                                    // a key was pressed
    EXPECT_EQ(pokes[0xB8E6] & 2, 2);                                    // the line buffer is in the text
    for (int k = 0; k < 8; ++k)
        EXPECT_EQ(pokes[static_cast<uint16_t>(0xAB35 + k)], plan.bytes[2 + k]);   // WNAME = TXNAME

    // Written: the reader gives the file back
    for (symbols::MemoryPage& page : dump.machine.ram)
        if (page.page == 6)
            page.bytes = plan.bytes;
    const SyncText back = ReadText(dump.machine, *d);
    ASSERT_TRUE(back.ok);
    EXPECT_EQ(back.file, written.bytes);

    // Not in the editor's main loop (assembling, a menu): refused
    dump.machine.sp = 0xBF02;
    EXPECT_FALSE(PlanInject(dump.machine, *d, written.bytes, force).ok);
}

TEST(Sync_Test, ARelocatableDescriptorInstalledElsewhereMovesItsCodeAddresses)
{
    // PROMETHEUS' installer moves the whole code: "<id>@<address>" is that move (the probe finds it in memory)
    const SyncDescriptor* base = FindDescriptor("prometheus-48");
    const SyncDescriptor* moved = FindDescriptor("prometheus-48@30000");
    ASSERT_NE(base, nullptr);
    ASSERT_NE(moved, nullptr);
    EXPECT_EQ(moved, FindDescriptor("prometheus-48@30000"));   // one descriptor per move
    EXPECT_EQ(moved->installedAt, 30000);
    EXPECT_NE(moved->title.find("at 30000"), std::string::npos) << moved->title;
    EXPECT_EQ(moved->identify[0].address, base->identify[0].address + 6000);
    EXPECT_EQ(moved->linear.fixedStart, base->linear.fixedStart + 6000);
    EXPECT_EQ(moved->idleChain[0], base->idleChain[0] + 6000);
    // the signature's CALL operand moved too
    EXPECT_EQ(static_cast<uint8_t>(moved->identify[0].bytes[8]) | static_cast<uint8_t>(moved->identify[0].bytes[9]) << 8,
              (static_cast<uint8_t>(base->identify[0].bytes[8]) | static_cast<uint8_t>(base->identify[0].bytes[9]) << 8) + 6000);

    // PROMETHEUS 128's text stays in page 6 at #C01A
    const SyncDescriptor* moved128 = FindDescriptor("prometheus-128@28000");
    ASSERT_NE(moved128, nullptr);
    EXPECT_EQ(moved128->linear.fixedStart, 0xC01A);
    EXPECT_EQ(moved128->linear.prometheusTableAt, FindDescriptor("prometheus-128")->linear.prometheusTableAt + 4000);

    EXPECT_EQ(FindDescriptor("prometheus-48@24000"), base);
    EXPECT_EQ(FindDescriptor("zasm-3.15@30000"), nullptr);   // a fixed code
    EXPECT_EQ(FindDescriptor("prometheus-48@"), nullptr);
    EXPECT_EQ(FindDescriptor("prometheus-48@70000"), nullptr);
    EXPECT_EQ(FindDescriptor("prometheus-48@3x"), nullptr);
}

TEST(SyncInject_Test, XasTakesAProjectWithItsIncludesOntoItsDisk)
{
    // XAS holds one text and reads what LTEXT names from the disk: the main source into memory, the INCLUDEd ones as
    // its files (type X, start "AS", length 0) for the disk
    containers::Snapshot snapshot;
    std::string error;
    ASSERT_TRUE(containers::ReadSnapshot(ReadTestData("templates/xas-7.447.szx"), "szx", snapshot, error)) << error;
    const SyncDescriptor* d = FindDescriptor("xas-7.447");
    ASSERT_NE(d, nullptr);
    SourceDocument main = SourceDocument::FromText("        ORG #6000\n        LD A,1\n        INCLUDE \"lib/inc1.asm\"\nEND_    NOP\n", "sjasmplus");
    main.name = "MAIN";
    Diagnostics diagnostics;
    const std::vector<ProjectFile> project = HostProject(main, HostFiles({{"lib/inc1.asm", "        LD B,2\nL1      DW END_\n"}}), diagnostics);
    ASSERT_EQ(project.size(), 2u);
    const ProjectInjectPlan plan = PlanInjectProject(ViewOf(snapshot), *d, project, "MAIN");
    ASSERT_TRUE(plan.ok) << plan.error;
    ASSERT_EQ(plan.pages.size(), 1u);
    ASSERT_EQ(plan.diskFiles.size(), 1u);
    EXPECT_TRUE(HasLine(plan.texts[0].document, "LTEXT \"INC1\"")) << plan.texts[0].document.Text();
    const containers::TrdosFile& file = plan.diskFiles[0];
    EXPECT_EQ(file.FullName(), "INC1.X");
    EXPECT_EQ(file.start, 0x5341);
    EXPECT_EQ(file.length, 0);
    EXPECT_EQ(file.data, plan.texts[1].bytes);

    // a snapshot alone has no disk for them: InjectProjectIntoSnapshot gives them to the caller
    std::vector<uint8_t> out;
    const ProjectInjectPlan written = InjectProjectIntoSnapshot(snapshot, *d, project, "MAIN", {}, out);
    ASSERT_TRUE(written.ok) << written.error;
    EXPECT_EQ(written.diskFiles.size(), 1u);
}
