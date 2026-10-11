// The source an assembler holds in RAM (asm-synchronizer.md §10): golden dumps (testdata/sync) of ALASM 5.09 / 4.44
// and TASM 4.12 sessions in unreal-ng, each with the file the assembler itself saved at that moment. The reader must
// give that file from the pages byte for byte, and the probe must name the assembler and no other.

#include <gtest/gtest.h>

#include <algorithm>
#include <cctype>
#include <map>

#include "codecs/alasm/alasmcodec.h"
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
    for (const uint16_t at : {0x8F5B, 0x8F5D, 0x8F5F, 0x94D9, 0x94CE})
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
