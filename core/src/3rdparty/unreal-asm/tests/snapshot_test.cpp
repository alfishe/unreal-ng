// Snapshot containers (Z1): the ALASM 5.09 idle templates read in all three formats, a host source written into each
// and read back, files kept byte for byte where nothing changed; the Z80 run-length coding and SZX's zlib pages.

#include <gtest/gtest.h>

#include <map>

#include "testdata.h"
#include "unrealasm/dialect.h"
#include "unrealasm/registry.h"
#include "unrealasm/snapshot.h"
#include "unrealasm/sync/reader.h"

using namespace unrealasm;
using namespace unrealasm::containers;
using unrealasm::testing::ReadTestData;
using unrealasm::testing::ReadTestText;

namespace
{
Snapshot Template(const std::string& format)
{
    Snapshot snapshot;
    std::string error;
    EXPECT_TRUE(ReadSnapshot(ReadTestData("templates/alasm-5.09." + format), format, snapshot, error)) << format << ": " << error;
    return snapshot;
}

/// oracle1.asm as ALASM 5.09's own file
std::vector<uint8_t> OracleFile()
{
    SourceDocument document = SourceDocument::FromText(ReadTestText("dialects/alasm-backend/oracle1.asm"), "sjasmplus");
    document.name = "ORACLE1";
    const WriteAsResult written = WriteAs(document, "alasm", "5.07");
    EXPECT_TRUE(written.ok) << written.error;
    return written.bytes;
}
}  // namespace

TEST(Snapshot_Test, AlasmTemplatesReadAlikeInEveryFormat)
{
    const Snapshot szx = Template("szx");
    const Snapshot sna = Template("sna");
    const Snapshot z80 = Template("z80");
    EXPECT_EQ(szx.machine, "Pentagon 128");
    EXPECT_EQ(z80.machine, "Pentagon 128");
    EXPECT_EQ(sna.machine, "128K") << "an SNA does not say which 128K";
    for (const Snapshot* s : {&szx, &sna, &z80})
    {
        EXPECT_EQ(s->ramPages, 8);
        EXPECT_EQ(s->pages.size(), 8u);
        EXPECT_EQ(s->windows[1], 5);
        EXPECT_EQ(s->windows[2], 2);
        EXPECT_EQ(s->windows[3], s->port7FFD & 7);
        EXPECT_EQ(s->sp, szx.sp);
        EXPECT_EQ(s->port7FFD, szx.port7FFD);
        EXPECT_EQ(s->pages, szx.pages) << SnapshotFormatName(s->format) << ": saved one after another at the prompt, the same RAM";
    }
    // SZX keeps its pages zlib-compressed: they inflated to whole pages
    const sync::MachineView view = sync::ViewOf(szx);
    const sync::SyncDescriptor* alasm = sync::FindDescriptor("alasm-5.09");
    ASSERT_NE(alasm, nullptr);
    std::string why;
    EXPECT_TRUE(sync::Idle(view, *alasm, why)) << why;
    const std::vector<sync::ProbeCandidate> candidates = sync::Probe(view);
    ASSERT_FALSE(candidates.empty());
    EXPECT_EQ(candidates[0].descriptor->id.rfind("alasm-5.0", 0), 0u) << candidates[0].descriptor->id;
}

TEST(Snapshot_Test, NothingChangedWritesTheSameFile)
{
    for (const std::string format : {"sna", "z80", "szx"})
    {
        const Snapshot s = Template(format);
        std::vector<uint8_t> out;
        std::string error;
        ASSERT_TRUE(WriteSnapshot(s, {}, out, error)) << error;
        EXPECT_EQ(out, s.bytes) << format;
    }
}

TEST(Snapshot_Test, ASourceGoesIntoEachFormatAndReadsBack)
{
    const std::vector<uint8_t> file = OracleFile();
    const sync::SyncDescriptor* alasm = sync::FindDescriptor("alasm-5.09");
    ASSERT_NE(alasm, nullptr);
    for (const std::string format : {"sna", "z80", "szx"})
    {
        const Snapshot s = Template(format);
        std::vector<uint8_t> out;
        const sync::InjectPlan plan = sync::InjectIntoSnapshot(s, *alasm, file, {}, out);
        ASSERT_TRUE(plan.ok) << format << ": " << plan.error;
        EXPECT_EQ(plan.page, 6) << "ALASM's text page in the 128K template";

        Snapshot back;
        std::string error;
        ASSERT_TRUE(ReadSnapshot(out, format, back, error)) << format << ": " << error;
        EXPECT_EQ(back.pages.at(plan.page), plan.bytes) << format;
        for (const auto& [page, bytes] : s.pages)
        {
            if (page != plan.page)
            {
                EXPECT_EQ(back.pages.at(page), bytes) << format << ": page " << page << " untouched";
            }
        }
        const sync::SyncText text = sync::ReadText(sync::ViewOf(back), *alasm);
        ASSERT_TRUE(text.ok) << format << ": " << text.state;
        EXPECT_EQ(text.name, "ORACLE1");
        EXPECT_EQ(text.file, file) << format;
        EXPECT_TRUE(text.changed) << "the header says changed since saved, as after an edit";
    }
}

TEST(Snapshot_Test, BusyAssemblerIsRefused)
{
    Snapshot s = Template("szx");
    s.sp = 0xFF00;   // nowhere near ALASM's command loop: not at its prompt
    std::vector<uint8_t> out;
    const sync::InjectPlan plan = sync::InjectIntoSnapshot(s, *sync::FindDescriptor("alasm-5.09"), OracleFile(), {}, out);
    EXPECT_FALSE(plan.ok);
    EXPECT_TRUE(out.empty());
    EXPECT_NE(plan.error.find("not waiting for a command"), std::string::npos) << plan.error;
    // force overrides unsaved changes and a missing idle rule, never a known rule that says busy
    sync::InjectOptions force;
    force.force = true;
    EXPECT_FALSE(sync::InjectIntoSnapshot(s, *sync::FindDescriptor("alasm-5.09"), OracleFile(), force, out).ok);
}

TEST(Snapshot_Test, Z80PagesCompressAndExpand)
{
    // Runs of 5 and more, short EDs runs, a single ED followed by a run: what the coding treats apart
    Snapshot s = Template("z80");
    std::vector<uint8_t> page(16384, 0);
    for (size_t i = 0; i < page.size(); ++i)
        page[i] = static_cast<uint8_t>(i % 7 == 0 ? 0xED : i % 3 == 0 ? i / 3 : 0x55);
    page[100] = 0xED;
    page[101] = page[102] = page[103] = page[104] = page[105] = page[106] = 0x11;
    page[200] = page[201] = 0xED;
    std::vector<uint8_t> out;
    std::string error;
    ASSERT_TRUE(WriteSnapshot(s, {{3, page}}, out, error)) << error;
    Snapshot back;
    ASSERT_TRUE(ReadSnapshot(out, "z80", back, error)) << error;
    EXPECT_EQ(back.pages.at(3), page);
    EXPECT_EQ(back.pages.at(7), s.pages.at(7));
}

TEST(Snapshot_Test, BrokenFilesSayWhy)
{
    Snapshot s;
    std::string error;
    EXPECT_FALSE(ReadSnapshot(std::vector<uint8_t>(1000, 0), "sna", s, error));
    EXPECT_NE(error.find("49179"), std::string::npos) << error;
    // The last byte of a compressed RAMP block is its Adler-32's: the page inflates, the checksum disagrees
    std::vector<uint8_t> szx = ReadTestData("templates/alasm-5.09.szx");
    size_t at = 8;
    while (at + 8 <= szx.size() && !(std::equal(szx.begin() + at, szx.begin() + at + 4, "RAMP") && (szx[at + 8] & 1)))
        at += 8 + (szx[at + 4] | (szx[at + 5] << 8) | (szx[at + 6] << 16));
    ASSERT_LT(at + 8, szx.size()) << "the template has compressed pages";
    szx[at + 8 + (szx[at + 4] | (szx[at + 5] << 8) | (szx[at + 6] << 16)) - 1] ^= 0xFF;
    EXPECT_FALSE(ReadSnapshot(szx, "szx", s, error));
    EXPECT_NE(error.find("checksum"), std::string::npos) << error;
    const Snapshot good = Template("z80");
    EXPECT_FALSE(WriteSnapshot(good, {{9, std::vector<uint8_t>(16384)}}, szx, error));
    EXPECT_NE(error.find("no RAM page 9"), std::string::npos) << error;
}

TEST(Snapshot_Test, EveryAssemblerTemplateTakesASourceWithoutAnEmulator)
{
    // The idle templates of make-template.py (each assembler at its prompt): probed idle, oracle1.asm written into
    // it by the inject (a page of its own, or the text byte by byte where the assembler keeps it in the 48K memory),
    // the written snapshot read back with the text there. Each one also assembled the text to sjasmplus' bytes in
    // unreal-ng (inject-oracle.py <profile> --template)
    const std::vector<std::pair<std::string, bool>> templates = {   // descriptor (or another build of it: builds below), force (no "changed" flag)
        {"alasm-5.09", false}, {"tasm-4.12", false}, {"xas-7.447", false}, {"storm-1.3", false}, {"masm-1.1", false},
        {"zasm-3.15", true},   {"gens-4", false},    {"zeus-1983", false}, {"primus-2.9", true}, {"lasergenius-1.04", true},
        {"prometheus-48", true}, {"pasm-3.0", true},
        {"zeus-gg", false},    {"zeus-7e", false},   {"zeus-1.1", false},
        {"tasm-4.0", false},   {"tasm-4.4", false},  {"tasm-3.0", false},  {"tasm-3.2", false},  {"tasm-3.5", false},
        {"tasm-2.0", false},   {"masm-3.0", true},   {"masm-2.0", true},   {"masm-1.0-demo", true},
        {"gens-3m21", false},  {"gens-4b", false},   {"gens-4.3t", false}, {"storm-1.0b", false},
        {"zasm-3.2x", true},   {"zasm-lite-1.07", true}, {"zasm-3.3.02", true}, {"zasm-3.3.51", true},
        {"zasm-3.3.final", true}, {"zasm-3.80.4", true}, {"zasm-3.4", true}, {"zasm-x64.1", true},
        {"zasm-4.0x8", true},  {"zasm-4.x64", true}, {"zasm-4.20", true},  {"zasm-3.10", true},  {"zasm-3.0", true},
        {"zasm-2.4", true},    {"zasm-2.5", true},   {"zasm-2.6", true},
        {"lasergenius-1.04-hash", true},   // with the hash extensions (Phoenix): the text from #737E, the same descriptor
        {"prometheus-48-trdos", true}, {"prometheus-128", true}, {"prometheus-128m", true}, {"prometheus-128s", true},
        {"zeus-2.2", false},   {"zeus-2.2si", false}, {"zeus-pro", false}, {"zeus-plus-2.16", false},
        {"storm-1.3i", false},   // STORM 1.3i: the 1.3 descriptor
        {"zeus-3.1", false},
        {"prometheus-48@30000", true}, {"prometheus-128@28000", true},   // installed elsewhere: the probe finds the move
    };
    SourceDocument source = SourceDocument::FromText(ReadTestText("dialects/alasm-backend/oracle1.asm"), "sjasmplus");
    source.name = "oracle1";
    for (const auto& [id, force] : templates)
    {
        Snapshot snapshot;
        std::string error;
        ASSERT_TRUE(ReadSnapshot(ReadTestData("templates/" + id + ".szx"), "szx", snapshot, error)) << id << ": " << error;
        // a template of another build of a descriptor's assembler
        static const std::map<std::string, std::string> builds = {{"lasergenius-1.04-hash", "lasergenius-1.04"}, {"storm-1.3i", "storm-1.3"}};
        const auto build = builds.find(id);
        const sync::SyncDescriptor* descriptor = sync::FindDescriptor(build != builds.end() ? build->second : id);
        ASSERT_NE(descriptor, nullptr) << id;
        std::string why;
        EXPECT_TRUE(sync::Idle(sync::ViewOf(snapshot), *descriptor, why)) << id << ": " << why;
        // the probe names it alone (ZEUS+ 3.1 carries 1983's title up to its second line)
        const std::vector<sync::ProbeCandidate> candidates = sync::Probe(sync::ViewOf(snapshot));
        ASSERT_FALSE(candidates.empty()) << id;
        EXPECT_EQ(candidates[0].descriptor, descriptor) << id << ": " << candidates[0].descriptor->id;
        EXPECT_TRUE(candidates.size() == 1 || candidates[1].score < candidates[0].score) << id << ": " << candidates[1].descriptor->id;

        sync::InjectOptions options;
        options.force = force;
        std::vector<uint8_t> out;
        const sync::ProjectInjectPlan plan = sync::InjectProjectIntoSnapshot(snapshot, *descriptor, {{"oracle1", source}}, "INJECTED", options, out);
        ASSERT_TRUE(plan.ok) << id << ": " << plan.error;
        Snapshot written;
        ASSERT_TRUE(ReadSnapshot(out, "szx", written, error)) << id << ": " << error;
        const sync::SyncText text = sync::ReadText(sync::ViewOf(written), *descriptor);
        ASSERT_TRUE(text.ok) << id << ": " << text.state;
        if (const sync::LinearParams& p = descriptor->linear; p.nameAt)
        {
            // the work name SAVE writes under (ZAsm 3.10 and later): the title, no more the template's STUB
            const sync::MachineView view = sync::ViewOf(written);
            const int page = p.namePage >= 0 ? p.namePage : view.windows[p.nameAt >> 14];
            const symbols::MemoryPage* bytes = view.Page(page);
            ASSERT_NE(bytes, nullptr) << id << ": page " << page;
            const size_t at = p.nameAt & 0x3FFF;
            EXPECT_EQ(std::string(bytes->bytes.begin() + static_cast<std::ptrdiff_t>(at), bytes->bytes.begin() + static_cast<std::ptrdiff_t>(at + 8)),
                      "INJECTED") << id;
            EXPECT_EQ(text.name, "INJECTED") << id << ": the reader gives the work name";
        }
        // the text in memory as the assembler's codec reads it (some keep a header of their own there): oracle1's lines
        const ISourceCodec* codec = CodecRegistry::Builtin().Find(descriptor->codec);
        ASSERT_NE(codec, nullptr) << id;
        DecodeOptions decode;
        decode.subversion = descriptor->version;
        std::string back = codec->Decode(text.file, decode).document.Text();
        for (char& c : back)
            c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        EXPECT_NE(back.find("COUNT"), std::string::npos) << id << ":\n" << back.substr(0, 400);
        EXPECT_NE(back.find("DJNZ"), std::string::npos) << id;
    }
}
