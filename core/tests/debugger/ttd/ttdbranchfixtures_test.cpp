/// @file ttdbranchfixtures_test.cpp
/// @brief Fixtures of branched sessions for the Python maintenance tools (P7, docs/inprogress/2026-11-08-ttd-branches/
/// tools.md §3) and the check of the tools' outputs (test-strategy.md T-F1 oracle).
///
/// DISABLED_WriteBranchFixtures writes testdata/ttd/branches/: save-all files of small engine sessions with branches
/// (two branches, a deep lineage, forks inside one frame, many short branches), a recording folder written by the
/// controller (a file per line, branches.tdx), the same folder cut by a crash, damaged copies, and expected.json:
/// per file and line the content CRC of every checkpoint as this reader loads it, the C++ verdict on each damaged
/// copy, the C++ join of each folder and the C++ rebuild of its branches.tdx. Run by hand after a format change:
///
///   core-tests --gtest_also_run_disabled_tests --gtest_filter='TTDBranchFixtures_Test.DISABLED_WriteBranchFixtures'
///
/// then the tool outputs again (tools/python/ttd/README.md, "Fixtures").
///
/// ToolOutputsRestoreAsTheOriginals loads the outputs of tools/python/ttd/ttdtool.py kept in
/// testdata/ttd/branches/tool-outputs/ (an extract by lineage and by subtree, a prune, the joins of both folders):
/// every line they hold restores every checkpoint exactly as the same line of the original (positions, CPU, every
/// piece of every region), they hold the lines the tool was asked to keep, and a join equals the C++ join byte for
/// byte.
///
/// A checkpoint's content CRC (the tools compute the same): CRC32C over the CPU state (48 bytes), then for every
/// region and piece holding a version, ascending: region (u32), piece (u32), the version's content CRC (u32).
///
/// Runtime: the enabled test loads five tool outputs, their originals and two C++ joins: about 25 ms.

#include <gtest/gtest.h>

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iterator>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "_helpers/emulatortesthelper.h"
#include "_helpers/testpathhelper.h"
#include "_helpers/ttdsyntheticsession.h"
#include "base/featuremanager.h"
#include "common/filehelper.h"
#include "debugger/debugmanager.h"
#include "debugger/keyboard/debugkeyboardmanager.h"
#include "debugger/ttd/engine/ttdbytes.h"
#include "debugger/ttd/engine/ttdcontainer.h"
#include "debugger/ttd/engine/ttdrecordingwriter.h"
#include "debugger/ttd/engine/ttdsessionfile.h"
#include "debugger/ttd/timetravelcontroller.h"
#include "debugger/ttd/timetravelengine.h"
#include "debugger/ttd/ttdcompression.h"
#include "debugger/ttd/ttdrecordingfolders.h"
#include "emulator/cpu/core.h"
#include "emulator/cpu/z80.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/memory/memory.h"

namespace
{
namespace fs = std::filesystem;
using namespace ttd;

fs::path FixtureDir()
{
    return TestPathHelper::FindProjectRoot() / "testdata" / "ttd" / "branches";
}

std::vector<uint8_t> ReadBytes(const fs::path& path)
{
    std::ifstream f(path, std::ios::binary);
    return std::vector<uint8_t>((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}

void WriteBytes(const fs::path& path, const std::vector<uint8_t>& bytes)
{
    fs::create_directories(path.parent_path());
    std::ofstream(path, std::ios::binary | std::ios::trunc)
        .write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
}

/// The controller's branch record (ttdcontrollersessionfile.cpp WriteBranchRecord, version 1): the holder bytes of a
/// create record
struct Meta
{
    std::string name;
    uint64_t created = 0;
    std::string by;
    std::string run;
    std::string reason;
    std::vector<std::string> tags;
    std::string note;
    bool pinned = false;
};

std::vector<uint8_t> ControllerRecord(uint8_t fixture, uint32_t line, const Meta& m, uint64_t endFrame)
{
    TTDByteWriter w;
    w.U8(1);
    for (uint8_t k = 0; k < 16; ++k)
        w.U8(k == 0 ? 0xB7 : k == 1 ? fixture : k == 2 ? static_cast<uint8_t>(line) : static_cast<uint8_t>(k * 17));
    w.Str(m.name);
    w.U64(m.created);
    w.Str(m.by);
    w.Str(m.run);
    w.Str(m.reason);
    w.Varint(m.tags.size());
    for (const std::string& t : m.tags)
        w.Str(t);
    w.Str(m.note);
    w.U8(m.pinned ? 1 : 0);
    w.Varint(endFrame);
    w.Varint(0);
    return std::move(w.bytes);
}

/// A checkpoint's content CRC (the file comment)
uint32_t ContentCrc(const TimeTravelEngine& e, size_t index)
{
    const TTDEngineCheckpoint* cp = e.Checkpoint(index);
    TTDByteWriter w;
    w.Raw(&cp->cpu, sizeof(cp->cpu));
    for (uint32_t r = 0; r < e.Regions().size(); ++r)
        for (uint32_t p = 0; p < e.Regions()[r].pieces; ++p)
        {
            const uint32_t id = e.VersionAt(index, r, p);
            if (id == TimeTravelEngine::kAbsent || id == TTDPieceStore::kNone)
                continue;
            w.U32(r);
            w.U32(p);
            w.U32(e.PieceStore().CrcOf(id));
        }
    return codec::Crc32C(w.bytes.data(), w.bytes.size());
}

/// Every line of a loaded session: parent, fork, (frame, content CRC) of each of its own checkpoints
std::string LinesJson(TimeTravelEngine& e, const std::string& indent)
{
    static_assert(sizeof(TTDCpuState) == 48, "the tools read 48 bytes of CPU state");
    std::string json = "{";
    bool firstLine = true;
    for (uint32_t line = 0; line < e.LineIds(); ++line)
    {
        if (!e.HasLine(line) || !e.ViewLine(line))
            continue;
        const TTDPosition fork = e.LineFork(line);
        json += std::string(firstLine ? "" : ",") + "\n" + indent + "  \"" + std::to_string(line) + "\": {\"parent\": " +
                (line == 0 ? std::string("-1") : std::to_string(e.LineParent(line))) +
                ", \"fork_frame\": " + std::to_string(line == 0 ? 0 : fork.frame) +
                ", \"fork_t\": " + std::to_string(line == 0 ? 0 : fork.tInFrame) + ", \"checkpoints\": [";
        firstLine = false;
        for (size_t i = e.LineFirstCheckpoint(line); i < e.CheckpointCount(); ++i)
            json += std::string(i == e.LineFirstCheckpoint(line) ? "" : ", ") + "[" +
                    std::to_string(e.Checkpoint(i)->position.frame) + ", " + std::to_string(ContentCrc(e, i)) + "]";
        json += "]}";
    }
    e.ViewLine(0);
    return json + "\n" + indent + "}";
}

std::string Escape(const std::string& s)
{
    std::string out;
    for (char c : s)
    {
        if (c == '"' || c == '\\')
            out += '\\';
        out += c;
    }
    return out;
}

/// The C++ reader's verdict on @p bytes: refused (and why), or the lines it loaded
std::string VerdictJson(const std::vector<uint8_t>& bytes, const std::string& indent)
{
    TimeTravelEngine e;
    TTDMemorySource source(bytes);
    std::string error;
    TTDSessionLoadReport report;
    if (!TTDSessionFile::Load(e, source, error, &report))
        return "{\"loads\": false, \"error\": \"" + Escape(error) + "\"}";
    TTDContainerReader reader;
    std::string ignored;
    const bool finalized = reader.Open(source, ignored, TTDSessionFile::KnownStream) && reader.Finalized();
    return "{\"loads\": true, \"finalized\": " + std::string(finalized ? "true" : "false") +
           ", \"parts\": " + std::to_string(report.partsInFile) + ", \"lines\": " + LinesJson(e, indent) + "}";
}

/// A tree of branches over the synthetic session (four pieces, one changing per frame): each line's memory apart
/// from its parent's from the fork on
struct Tree
{
    ttdtest::Session s{ttdtest::Growable(10)};
    std::map<uint32_t, std::map<uint64_t, std::vector<uint8_t>>> memoryAt;   // line, frame -> after the frame
    std::map<uint32_t, uint64_t> ends;
    std::map<uint32_t, Meta> metas;

    void Run(uint32_t line, int frames)
    {
        for (int i = 0; i < frames; ++i)
        {
            s.Frame();
            memoryAt[line][s.frame - 1] = s.memory;
        }
        ends[line] = s.frame - 1;
    }

    uint32_t Fork(uint32_t from, uint64_t frame, uint32_t t, uint8_t mark, Meta meta)
    {
        std::string error;
        uint32_t line = 0;
        EXPECT_TRUE(s.engine.ViewLine(from));
        EXPECT_TRUE(s.engine.ForkAt(static_cast<size_t>(frame), {0, frame, t}, line, error)) << error;
        s.memory = memoryAt[from][frame];
        s.memory[17 + mark] ^= mark;
        s.frame = frame + 1;
        metas[line] = std::move(meta);
        return line;
    }

    std::vector<uint8_t> Save(uint8_t fixture, uint32_t active)
    {
        TTDSessionSaveParams params;
        params.checkpointsPerPart = 4;
        params.createdMicros = 1'763'200'000'000'000ull;
        params.uuid[0] = 0xB7;
        params.uuid[1] = fixture;
        for (const auto& [line, meta] : metas)
            params.lineMeta[line] = ControllerRecord(fixture, line, meta, ends[line]);
        // The session's branch state (holderstream::kBranchState): the active branch, the trunk's record
        TTDByteWriter state;
        state.U8(1);
        state.Varint(active);
        const std::vector<uint8_t> trunk = ControllerRecord(fixture, 0, Meta{"trunk", 1'763'100'000'000'000ull, "qt"},
                                                            ends[0]);
        state.Bytes(trunk.data(), trunk.size());
        params.holderStreams.push_back({18, "branch-state", std::move(state.bytes)});
        TTDMemorySink sink;
        std::string error;
        EXPECT_TRUE(TTDSessionFile::Save(s.engine, sink, error, params)) << error;
        return sink.bytes;
    }
};

constexpr uint64_t kDay = 86'400'000'000ull;   // host micros
constexpr uint64_t kBase = 1'763'000'000'000'000ull;

std::vector<uint8_t> TwoBranches()
{
    Tree t;
    t.Run(0, 30);
    t.Run(t.Fork(0, 12, 1500, 1, {"resume 1", kBase, "qt", "", "resume"}), 10);
    return t.Save(1, 1);
}

/// A chain of six from the trunk, each from the previous two frames after its fork, and a side branch from the second
std::vector<uint8_t> DeepLineage()
{
    Tree t;
    t.Run(0, 20);
    uint32_t parent = 0;
    uint64_t at = 5;
    for (uint32_t k = 1; k <= 6; ++k)
    {
        Meta m{"edit " + std::to_string(k), kBase + k * kDay, "mcp:claude", k <= 3 ? "run-a" : "run-b", "edit-memory",
               {"deep"}, k == 3 ? "the third" : "", k == 3};
        parent = t.Fork(parent, at, 100 * k, static_cast<uint8_t>(k), m);
        t.Run(parent, 5);
        at += 2;
    }
    t.Run(t.Fork(2, 8, 700, 9, {"side", kBase + 9 * kDay, "lua", "", "create", {}, "", false}), 4);
    return t.Save(2, 6);
}

/// Three forks in one frame: at its first T, inside it and at its last T
std::vector<uint8_t> MidFrame()
{
    Tree t;
    t.Run(0, 20);
    const uint32_t ts[] = {1, 34'944, 69'887};
    const char* names[] = {"t first", "t mid", "t last"};
    for (uint32_t k = 0; k < 3; ++k)
        t.Run(t.Fork(0, 8, ts[k], static_cast<uint8_t>(k + 1), {names[k], kBase, "gdb", "", "resume"}), 4);
    return t.Save(3, 0);
}

/// 24 branches of two frames each, the analyzer's way: two runs, a few kept (tags, pins), ages over a month
std::vector<uint8_t> ManyShort()
{
    Tree t;
    t.Run(0, 40);
    for (uint32_t k = 0; k < 24; ++k)
    {
        Meta m{"experiment " + std::to_string(k + 1), kBase + k * 2 * kDay, k % 5 == 4 ? "qt" : "analyzer",
               k < 16 ? "run-1" : "run-2", "create"};
        if (k == 3 || k == 10 || k == 20)
            m.tags = {"keep"};
        m.pinned = k == 5 || k == 17;
        t.Run(t.Fork(0, 1 + k * 3 / 2, 2000 + k, static_cast<uint8_t>(k + 1), m), k % 3 == 0 ? 3 : 2);
    }
    return t.Save(4, 0);
}

/// @p file with @p edit applied to every create record of the branch table it accepts (re-encoded: its CRC valid)
std::vector<uint8_t> WithCreateEdited(const std::vector<uint8_t>& file,
                                      const std::function<bool(std::vector<uint8_t>&)>& edit)
{
    TTDMemorySource source(file);
    TTDContainerReader reader;
    std::string error;
    EXPECT_TRUE(reader.Open(source, error, TTDSessionFile::KnownStream)) << error;
    TTDMemorySink sink;
    TTDContainerWriter writer;
    writer.Begin(sink, reader.Header(), &error);
    for (const TTDPartRef& part : reader.Parts())
    {
        for (const TTDRecordRef& r : part.records)
        {
            std::vector<uint8_t> bytes;
            if (r.streamId == sessionstream::kBranchTable && reader.ReadRecord(r, bytes) && edit(bytes))
            {
                writer.AddRecord(r.streamId, bytes);
                continue;
            }
            std::vector<uint8_t> stored;
            reader.ReadStored(r, stored);
            writer.AddStoredRecord(r.streamId, r.flags, stored.data(), stored.size(), r.rawSize);
        }
        writer.EndPart({part.firstFrame, part.frameCount, part.branch, part.dependencies, part.extra});
    }
    writer.Finalize(reader.IndexExtra());
    return sink.bytes;
}

struct Create
{
    uint32_t local = 0, parent = 0, t = 0;
    uint64_t frame = 0;
    std::vector<uint8_t> meta;
};

bool ParseCreate(const std::vector<uint8_t>& bytes, Create& c)
{
    TTDByteReader r(bytes);
    uint8_t kind = 0;
    uint64_t size = 0;
    return r.U8(kind) && kind == 1 && r.VarintAs(c.local) && r.VarintAs(c.parent) && r.Varint(c.frame) &&
           r.VarintAs(c.t) && r.Varint(size) && size <= r.Left() && r.Bytes(c.meta, static_cast<size_t>(size));
}

/// The middle of the first record of stream @p stream (0: any) in part @p partOfLine of @p line's parts
size_t PayloadOffset(const std::vector<uint8_t>& file, uint16_t line, size_t partOfLine, uint16_t stream)
{
    TTDMemorySource source(file);
    TTDContainerReader reader;
    std::string error;
    EXPECT_TRUE(reader.Open(source, error, TTDSessionFile::KnownStream)) << error;
    size_t seen = 0;
    for (const TTDPartRef& part : reader.Parts())
    {
        if (part.branch != line || seen++ != partOfLine)
            continue;
        for (const TTDRecordRef& r : part.records)
            if (stream == 0 || r.streamId == stream)
                return static_cast<size_t>(r.offset + kRecordHeaderSize + r.storedSize / 2);
    }
    ADD_FAILURE() << "no such record";
    return 0;
}

/// The segment files of a recording folder, in order (TTDRecordingFolder::Segments)
std::vector<std::string> FolderSegments(const fs::path& folder)
{
    const auto opened = TTDRecordingFolder::Open(FileHelper::FromFsPath(folder));
    EXPECT_NE(opened, nullptr) << folder;
    return opened ? opened->Segments() : std::vector<std::string>{};
}

/// The C++ join of a folder's files at @p target
std::vector<uint8_t> JoinFolder(const fs::path& folder, const std::string& target)
{
    std::string error;
    EXPECT_TRUE(JoinSessionFiles(FolderSegments(folder), target, true, error)) << error;
    return ReadBytes(FileHelper::ToFsPath(target));
}

}  // namespace

/// A recording folder written by the controller (the realistic one: a file per line, branches.tdx), as
/// TimeTravelController_BranchFolder_Test records it, shorter
class TTDBranchFixtures_Test : public ::testing::Test
{
protected:
    Emulator* _emulator = nullptr;
    EmulatorContext* _context = nullptr;
    TimeTravelController* _ttd = nullptr;
    uint64_t _start = 0;

    void TearDown() override
    {
        if (_emulator)
            EmulatorTestHelper::CleanupEmulator(_emulator);
    }

    void StartMachine(const std::string& root)
    {
        _emulator = EmulatorTestHelper::CreateStandardEmulator("PENTAGON", LoggerLevel::LogError);
        ASSERT_NE(_emulator, nullptr);
        _context = _emulator->GetContext();
        _ttd = _context->pTimeTravelController;
        FeatureManager* features = _emulator->GetFeatureManager();
        features->setFeature(Features::kDebugMode, true);
        features->setFeature(Features::kTimeTravel, true);
        _context->pMemory->UpdateFeatureCache();
        _ttd->SetShadowRecordingRoot(root);
        // The keyboard port folded into RAM #6000-#7FFF: a key held in one history makes its memory differ
        static const uint8_t program[] = {0xF3, 0x21, 0x00, 0x60, 0xDB, 0xFE, 0x86, 0x77, 0x23,
                                          0x7C, 0xE6, 0x1F, 0xF6, 0x60, 0x67, 0x18, 0xF3};
        _emulator->RunNFrames(5);
        Z80* z80 = _context->pCore->GetZ80();
        for (size_t i = 0; i < sizeof(program); i++)
            z80->DirectWrite(static_cast<uint16_t>(0x8000 + i), program[i]);
        z80->pc = 0x8000;
    }

    void RecordBranchFrom(uint64_t at, int frames)
    {
        ASSERT_TRUE(_ttd->SeekTo({at, 0}));
        ASSERT_TRUE(_ttd->ResumeRecordingFrom({at, 0}));
        auto* keys = _context->pDebugManager->GetKeyboardManager();
        keys->PressKey(ZXKEY_A);
        _emulator->RunNFrames(1);
        keys->ReleaseKey(ZXKEY_A);
        _emulator->RunNFrames(frames - 1);
        _ttd->StopRecording();
    }

    /// Trunk 12 frames, branch 1 from +4 (6 frames), branch 2 from branch 1 at +7 (4 frames), the trunk again from its
    /// end (3 frames): segment-0000.ttd, segment-0001-b1.ttd, segment-0002-b2.ttd, segment-0003.ttd
    std::string RecordFolder(const std::string& root)
    {
        StartMachine(root);
        if (HasFatalFailure())
            return {};
        EXPECT_TRUE(_ttd->StartRecording());
        _start = _context->emulatorState.frame_counter;
        _emulator->RunNFrames(12);
        _ttd->StopRecording();
        RecordBranchFrom(_start + 4, 6);
        RecordBranchFrom(_start + 7, 4);
        std::string error;
        const TTDBranchInfo* trunk = _ttd->Branches().Get(kTTDTrunk);
        EXPECT_TRUE(_ttd->SwitchBranch(kTTDTrunk, TTDTimePoint{trunk->end.frame, 0}, error)) << error;
        const TTDCheckpoint* last = _ttd->GetCheckpoint(_ttd->GetCheckpointCount() - 1);
        EXPECT_TRUE(_ttd->SeekTo(last->time) && _ttd->ResumeRecordingFrom(last->time));
        _emulator->RunNFrames(3);
        _ttd->StopRecording();
        EXPECT_EQ(_ttd->Branches().Count(), 3u);
        return _ttd->ShadowRecordingFolder();
    }
};

TEST_F(TTDBranchFixtures_Test, DISABLED_WriteBranchFixtures)
{
    const fs::path dir = FixtureDir();
    fs::create_directories(dir);
    std::map<std::string, std::vector<uint8_t>> files;
    files["two-branches.ttd"] = TwoBranches();
    files["deep-lineage.ttd"] = DeepLineage();
    files["mid-frame.ttd"] = MidFrame();
    files["many-short.ttd"] = ManyShort();
    for (const auto& [name, bytes] : files)
        WriteBytes(dir / name, bytes);

    // The recording folder, and a crash's copy of it: the last file cut inside its last part, branches.tdx cut
    // inside the record that file would have got
    const std::string root = TestPathHelper::GetUniqueTestScratchPath("ttd-branch-fixtures");
    const fs::path recorded = FileHelper::ToFsPath(RecordFolder(root));
    ASSERT_FALSE(HasFatalFailure());
    ASSERT_FALSE(recorded.empty());
    const fs::path folder = dir / "folder";
    const fs::path crashed = dir / "folder-crashed";
    fs::remove_all(folder);
    fs::remove_all(crashed);
    fs::create_directories(folder);
    fs::create_directories(crashed);
    for (const auto& entry : fs::directory_iterator(recorded))
    {
        const std::string name = FileHelper::FromFsPath(entry.path().filename());
        // owner.pid names no process (0): a crashed recording, never a live one
        std::vector<uint8_t> bytes = name == kRecordingOwnerFile ? std::vector<uint8_t>{'0', '\n'} : ReadBytes(entry.path());
        WriteBytes(folder / name, bytes);
        if (name == "segment-0003.ttd")
        {
            TTDMemorySource source(bytes);
            TTDContainerReader reader;
            std::string error;
            ASSERT_TRUE(reader.Open(source, error, TTDSessionFile::KnownStream)) << error;
            bytes.resize(static_cast<size_t>(reader.Parts().back().records.back().offset) + 10);
        }
        else if (name == kBranchIndexFile)
        {
            TTDMemorySource source(bytes);
            TTDContainerReader reader;
            std::string error;
            ASSERT_TRUE(reader.Open(source, error, [](uint16_t id) { return id == sessionstream::kBranchTable; }));
            bytes.resize(static_cast<size_t>(reader.Parts().back().records.back().offset) + 20);
        }
        WriteBytes(crashed / name, bytes);
    }

    // Damaged copies with the C++ verdict
    std::map<std::string, std::vector<uint8_t>> damaged;
    {
        std::vector<uint8_t> b = files["two-branches.ttd"];
        b[PayloadOffset(b, 1, 1, sessionstream::BranchTwin(sessionstream::kCheckpoints))] ^= 0x10;
        damaged["two-branches-branch-part.ttd"] = b;
        b = files["two-branches.ttd"];
        b[PayloadOffset(b, 1, 0, sessionstream::BranchTwin(sessionstream::kPieces))] ^= 0x10;
        damaged["two-branches-branch-first.ttd"] = b;
        b = files["two-branches.ttd"];
        b[PayloadOffset(b, 0, 3, sessionstream::kCheckpoints)] ^= 0x10;
        damaged["two-branches-trunk-part.ttd"] = b;
        b = files["two-branches.ttd"];
        b.resize(PayloadOffset(b, 1, 2, 0));
        damaged["two-branches-cut.ttd"] = b;
        b = files["many-short.ttd"];
        b[b.size() - 8] ^= 0x01;   // the trailer's magic
        damaged["many-short-trailer.ttd"] = b;
    }
    damaged["deep-lineage-create-parent.ttd"] = WithCreateEdited(files["deep-lineage.ttd"], [](std::vector<uint8_t>& r) {
        Create c;
        if (!ParseCreate(r, c) || c.local != 3)
            return false;
        r = TTDBranchCreateRecord(c.local, 5, c.frame, c.t, c.meta);
        return true;
    });
    damaged["mid-frame-create-frame.ttd"] = WithCreateEdited(files["mid-frame.ttd"], [](std::vector<uint8_t>& r) {
        Create c;
        if (!ParseCreate(r, c) || c.local != 2)
            return false;
        r = TTDBranchCreateRecord(c.local, c.parent, c.frame + 1, c.t, c.meta);
        return true;
    });
    for (const auto& [name, bytes] : damaged)
        WriteBytes(dir / "damaged" / name, bytes);

    // expected.json
    std::string json = "{\n  \"files\": {";
    bool first = true;
    for (const auto& [name, bytes] : files)
    {
        json += std::string(first ? "" : ",") + "\n    \"" + name + "\": " + VerdictJson(bytes, "    ");
        first = false;
    }
    for (const fs::path& f : {folder, crashed})
    {
        const std::string name = FileHelper::FromFsPath(f.filename());
        const std::string target = TestPathHelper::GetUniqueTestScratchPath("ttd-branch-fixtures-join-" + name) + ".ttd";
        const std::vector<uint8_t> joined = JoinFolder(f, target);
        ASSERT_FALSE(joined.empty()) << name;
        std::string verdict = VerdictJson(joined, "    ");
        verdict.insert(1, "\"joined_bytes\": " + std::to_string(joined.size()) + ", \"joined_crc32c\": " +
                              std::to_string(codec::Crc32C(joined.data(), joined.size())) + ", ");
        json += ",\n    \"" + name + "\": " + verdict;
    }
    json += "\n  },\n  \"damaged\": {";
    first = true;
    for (const auto& [name, bytes] : damaged)
    {
        json += std::string(first ? "" : ",") + "\n    \"" + name + "\": " + VerdictJson(bytes, "    ");
        first = false;
    }
    json += "\n  },\n  \"index_rebuilt\": {";
    first = true;
    for (const fs::path& f : {folder, crashed})
    {
        // RebuildBranchIndex on a copy (it writes in place)
        const std::string name = FileHelper::FromFsPath(f.filename());
        const fs::path copy = FileHelper::ToFsPath(TestPathHelper::GetUniqueTestScratchPath("ttd-branch-fixtures-tdx-" + name));
        fs::create_directories(copy);
        for (const auto& entry : fs::directory_iterator(f))
            fs::copy_file(entry.path(), copy / entry.path().filename(), fs::copy_options::overwrite_existing);
        const auto opened = TTDRecordingFolder::Open(FileHelper::FromFsPath(copy));
        ASSERT_NE(opened, nullptr);
        std::string error;
        ASSERT_TRUE(opened->RebuildBranchIndex(&error)) << error;
        const std::vector<uint8_t> tdx = ReadBytes(copy / kBranchIndexFile);
        json += std::string(first ? "" : ",") + "\n    \"" + name + "\": {\"bytes\": " + std::to_string(tdx.size()) +
                ", \"crc32c\": " + std::to_string(codec::Crc32C(tdx.data(), tdx.size())) + "}";
        first = false;
    }
    json += "\n  }\n}\n";
    std::ofstream(dir / "expected.json") << json;
}

namespace
{
/// Where @p loaded's lines differ from the same lines of @p original: empty when every checkpoint of every line
/// restores the same (position, CPU, the content CRC and every region's bytes)
std::string Difference(TimeTravelEngine& loaded, TimeTravelEngine& original)
{
    for (uint32_t line = 0; line < loaded.LineIds(); ++line)
    {
        if (!loaded.HasLine(line))
            continue;
        const std::string at = "line " + std::to_string(line) + ": ";
        if (!original.HasLine(line))
            return at + "not in the original";
        if (loaded.LineParent(line) != original.LineParent(line) || !(loaded.LineFork(line) == original.LineFork(line)))
            return at + "another parent or fork";
        if (!loaded.ViewLine(line) || !original.ViewLine(line))
            return at + "not viewable";
        if (loaded.LineFirstCheckpoint(line) != original.LineFirstCheckpoint(line) ||
            loaded.CheckpointCount() != original.CheckpointCount())
            return at + "checkpoints " + std::to_string(loaded.LineFirstCheckpoint(line)) + ".." +
                   std::to_string(loaded.CheckpointCount()) + ", the original " +
                   std::to_string(original.LineFirstCheckpoint(line)) + ".." + std::to_string(original.CheckpointCount());
        std::vector<uint8_t> a, b;
        for (size_t i = loaded.LineFirstCheckpoint(line); i < loaded.CheckpointCount(); ++i)
        {
            const std::string cpAt = at + "checkpoint " + std::to_string(i) + ": ";
            if (!(loaded.Checkpoint(i)->position == original.Checkpoint(i)->position))
                return cpAt + "position";
            if (ContentCrc(loaded, i) != ContentCrc(original, i))
                return cpAt + "content CRC";
            for (uint32_t r = 0; r < loaded.Regions().size(); ++r)
            {
                a.assign(size_t(loaded.Regions()[r].pieces) * kTTDPieceSize, 0);
                b.assign(a.size(), 0);
                if (!loaded.RestoreRegion(i, r, a.data()).Ok() || !original.RestoreRegion(i, r, b.data()).Ok() || a != b)
                    return cpAt + "region " + std::to_string(r);
            }
        }
    }
    return {};
}

bool LoadBytes(TimeTravelEngine& e, const std::vector<uint8_t>& bytes, std::string& error)
{
    TTDMemorySource source(bytes);
    TTDSessionLoadReport report;
    return TTDSessionFile::Load(e, source, error, &report) && report.complete;
}
}  // namespace

/// T-F1 for the tools (tools.md §3): every output of ttdtool.py kept as a fixture restores as its original
TEST(TTDBranchToolOutputs_Test, ToolOutputsRestoreAsTheOriginals)
{
    struct Case
    {
        const char* output;
        const char* original;   ///< a file, or a folder (joined here by the C++ join)
        std::set<uint32_t> lines;
    };
    const Case cases[] = {
        {"extract-deep-side-lineage.ttd", "deep-lineage.ttd", {0, 1, 2, 7}},
        {"extract-deep-edit3-subtree.ttd", "deep-lineage.ttd", {0, 1, 2, 3, 4, 5, 6}},
        {"prune-many-run1.ttd", "many-short.ttd", {0, 6, 17, 18, 19, 20, 21, 22, 23, 24}},
        {"join-folder.ttd", "folder", {0, 1, 2}},
        {"join-folder-crashed.ttd", "folder-crashed", {0, 1, 2}},
    };
    const fs::path dir = FixtureDir();
    for (const Case& c : cases)
    {
        SCOPED_TRACE(c.output);
        const std::vector<uint8_t> output = ReadBytes(dir / "tool-outputs" / c.output);
        ASSERT_FALSE(output.empty()) << "testdata/ttd/branches/tool-outputs/" << c.output;
        std::vector<uint8_t> original;
        if (fs::is_directory(dir / c.original))
        {
            original = JoinFolder(dir / c.original,
                                  TestPathHelper::GetUniqueTestScratchPath(std::string("ttd-tool-join-") + c.original) +
                                      ".ttd");
            EXPECT_TRUE(output == original) << "the tool's join differs from the C++ join";
        }
        else
            original = ReadBytes(dir / c.original);
        TimeTravelEngine loaded, reference;
        std::string error;
        ASSERT_TRUE(LoadBytes(loaded, output, error)) << error;
        ASSERT_TRUE(LoadBytes(reference, original, error)) << error;
        std::set<uint32_t> lines;
        for (uint32_t line = 0; line < loaded.LineIds(); ++line)
            if (loaded.HasLine(line))
                lines.insert(line);
        EXPECT_EQ(lines, c.lines);
        EXPECT_EQ(Difference(loaded, reference), "");
    }
}
