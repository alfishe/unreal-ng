// S4: a composite's guest file changes written back into its host folder layers (multi-source
// phases/c8-commit-writeback.md §3; flatten-strategies.md DT-10 to DT-12). FatGuest plays the guest.

#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <string>
#include <vector>

#include "_helpers/fatguest.h"
#include "_helpers/scratchfolder.h"
#include "_helpers/sessionspillguard.h"
#include "emulator/io/storage/fat/fatvolumereader.h"
#include "emulator/media/mediamanager.h"
#include "emulator/media/writeback.h"

namespace
{
    std::string Text(const std::filesystem::path& path)
    {
        std::ifstream in(path, std::ios::binary);
        return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    }

    bool Has(const std::vector<std::string>& lines, const std::string& text)
    {
        return std::any_of(lines.begin(), lines.end(), [&text](const std::string& l) { return l.find(text) != std::string::npos; });
    }

    std::string Join(const std::vector<std::string>& lines)
    {
        std::string all;
        for (const std::string& l : lines)
            all += l + "\n";
        return all;
    }

    class CardSlot : public IMediaSlot
    {
    public:
        CardSlot()
        {
            _d.id = "sd.zc";
            _d.kind = MediaKind::Block;
            _d.label = "SD card";
            _d.tags = {"sd"};
        }
        const SlotDescriptor& Descriptor() const override { return _d; }
        void Attach(Medium& m) override { attached = &m; }
        void Detach() override { attached = nullptr; }
        void SourceChanged(Medium&) override {}
        Medium* attached = nullptr;

    private:
        SlotDescriptor _d;
    };

    /// Layers: `base` read-only at /, `work` writable at /WORK (onDelete as given), `up` writable at / (writes.upper)
    class ComposeWriteBack_Test : public ::testing::TestWithParam<bool>
    {
    protected:
        SessionSpillGuard _spill{GetParam()};  // Spilled: the guest's writes go through the spill file
        ScratchFolder _folder{"compose-writeback"};
        MediaManager _manager{nullptr};
        CardSlot _slot;
        std::filesystem::path _descriptor;

        void SetUp() override
        {
            _manager.RegisterSlot(_slot);
            _folder.File("base/README.TXT", "base readme");
            _folder.File("base/DOC/A.TXT", "doc a");
            _folder.File("work/TOOL.TXT", "tool v1");
            _folder.File("work/OLD.TXT", "old");
            std::filesystem::create_directories(_folder.Path() / "up");
        }
        void TearDown() override
        {
            EjectOptions discard;
            discard.disposition = Disposition::Discard;
            _manager.Eject("sd.zc", discard);
            _manager.UnregisterSlot("sd.zc");
        }

        void Insert(const std::string& onDelete = "delete")
        {
            _descriptor = _folder.File("card.ucompose.yaml",
                                       "version: 1\ntarget: {free: 1MiB}\nwrites: {upper: up}\nlayers:\n"
                                       "  - {name: base, source: {folder: base}}\n"
                                       "  - {name: work, source: {folder: work}, mount: /WORK, writable: true, onDelete: " + onDelete +
                                           ", deletedFolder: deleted}\n"
                                       "  - {name: up, source: {folder: up}, writable: true}\n");
            MediaSource source;
            source.path = FileHelper::FromFsPath(_descriptor);
            const MediaResult r = _manager.Insert("sd.zc", source, {});
            ASSERT_TRUE(r.Ok()) << r.message;
        }

        FatGuest Guest() { return FatGuest(*_slot.attached->Block()); }

        MediaResult WriteBack(bool plan = false, bool keepBoth = false)
        {
            _manager.ApplyPending();
            SaveOptions options;
            options.strategy = "write-back";
            options.plan = plan;
            options.keepBoth = keepBoth;
            return _manager.Save("sd.zc", options);
        }

        std::string Read(const std::string& path)
        {
            FatVolumeReader reader;
            std::vector<uint8_t> data;
            if (!reader.Open(*_slot.attached->Block()) || !reader.ReadFile(path, data))
                return "<none>";
            return std::string(data.begin(), data.end());
        }
    };
}  // namespace

TEST_P(ComposeWriteBack_Test, ModifyCreateAndCopyUp)
{
    Insert();
    {
        FatGuest g = Guest();
        ASSERT_TRUE(g.Write("/WORK/TOOL.TXT", std::vector<uint8_t>{'t', 'o', 'o', 'l', ' ', 'v', '2'}));  // writable owner
        ASSERT_TRUE(g.Write("/README.TXT", std::vector<uint8_t>{'n', 'e', 'w'}));                         // read-only owner
        ASSERT_TRUE(g.Create("/WORK/NEW.TXT", std::vector<uint8_t>(5, 'n')));                             // into work
        ASSERT_TRUE(g.Create("/ROOT.TXT", std::vector<uint8_t>(3, 'r')));                                 // into up
        ASSERT_TRUE(g.Mkdir("/WORK/SUB"));
        ASSERT_TRUE(g.Create("/WORK/SUB/IN.TXT", std::vector<uint8_t>(2, 'i')));
    }
    const MediaResult result = WriteBack();
    ASSERT_TRUE(result.Ok()) << result.message << "\n" << Join(result.report);

    EXPECT_EQ(Text(_folder.Path() / "work/TOOL.TXT"), "tool v2");
    EXPECT_EQ(Text(_folder.Path() / "base/README.TXT"), "base readme") << "a read-only layer is never written";
    EXPECT_EQ(Text(_folder.Path() / "up/README.TXT"), "new") << "copied up";
    EXPECT_EQ(Text(_folder.Path() / "work/NEW.TXT"), "nnnnn");
    EXPECT_EQ(Text(_folder.Path() / "up/ROOT.TXT"), "rrr");
    EXPECT_EQ(Text(_folder.Path() / "work/SUB/IN.TXT"), "ii");

    // Rebuilt from the layers: the same files, nothing unsaved
    EXPECT_FALSE(_manager.Info("sd.zc")->dirty);
    EXPECT_EQ(Read("/README.TXT"), "new");
    EXPECT_EQ(Read("/WORK/TOOL.TXT"), "tool v2");
    EXPECT_EQ(Read("/WORK/SUB/IN.TXT"), "ii");
    EXPECT_TRUE(std::none_of(std::filesystem::directory_iterator(_folder.Path() / "work"), std::filesystem::directory_iterator(),
                             [](const auto& e) { return e.path().filename().string().rfind(".unreal-staging", 0) == 0; }))
        << "no staged file left";
    EXPECT_FALSE(std::filesystem::exists(WriteBack::JournalFor(_descriptor)));
    if (_spill.Active())
        EXPECT_TRUE(_spill.Spilled()) << "the guest's files were attributed and written back from a spill file";
}

TEST_P(ComposeWriteBack_Test, DeletesFollowTheirPolicies)
{
    Insert("delete");
    const std::string before = Text(_descriptor);
    {
        FatGuest g = Guest();
        ASSERT_TRUE(g.Delete("/WORK/OLD.TXT"));   // work: onDelete delete
        ASSERT_TRUE(g.Delete("/DOC/A.TXT"));      // base, read-only: a whiteout
    }
    const MediaResult result = WriteBack();
    ASSERT_TRUE(result.Ok()) << result.message << "\n" << Join(result.report);
    EXPECT_FALSE(std::filesystem::exists(_folder.Path() / "work/OLD.TXT"));
    EXPECT_TRUE(std::filesystem::exists(_folder.Path() / "base/DOC/A.TXT")) << "the read-only source is untouched";
    EXPECT_NE(Text(_folder.Path() / "card.ucompose.yaml.whiteout").find("/DOC/A.TXT"), std::string::npos);
    EXPECT_EQ(Text(_descriptor), before) << "the descriptor itself is never rewritten";
    EXPECT_EQ(Read("/DOC/A.TXT"), "<none>") << "the rebuild leaves it out";
    EXPECT_EQ(Read("/WORK/OLD.TXT"), "<none>");
}

TEST_P(ComposeWriteBack_Test, MoveKeepAndIgnore)
{
    Insert("move");
    ASSERT_TRUE(Guest().Delete("/WORK/OLD.TXT"));
    ASSERT_TRUE(WriteBack().Ok());
    EXPECT_FALSE(std::filesystem::exists(_folder.Path() / "work/OLD.TXT"));
    bool moved = false;
    for (const auto& e : std::filesystem::recursive_directory_iterator(_folder.Path() / "deleted"))
        moved = moved || e.path().filename() == "OLD.TXT";
    EXPECT_TRUE(moved) << "into deleted/<UTC time>/work/";

    TearDown();
    _manager.RegisterSlot(_slot);
    _folder.File("work/OLD.TXT", "old");
    Insert("ignore");
    ASSERT_TRUE(Guest().Delete("/WORK/OLD.TXT"));
    const MediaResult ignored = WriteBack();
    ASSERT_TRUE(ignored.Ok()) << ignored.message;
    EXPECT_TRUE(Has(ignored.report, "onDelete: ignore")) << Join(ignored.report);
    EXPECT_EQ(Read("/WORK/OLD.TXT"), "old") << "it comes back on the rebuild";

    TearDown();
    _manager.RegisterSlot(_slot);
    Insert("keep");
    ASSERT_TRUE(Guest().Delete("/WORK/OLD.TXT"));
    ASSERT_TRUE(WriteBack().Ok());
    EXPECT_TRUE(std::filesystem::exists(_folder.Path() / "work/OLD.TXT")) << "keep: the host file stays";
    EXPECT_EQ(Read("/WORK/OLD.TXT"), "<none>") << "and a whiteout hides it";
}

TEST_P(ComposeWriteBack_Test, RenameInsideALayerAndAcross)
{
    Insert();
    {
        FatGuest g = Guest();
        ASSERT_TRUE(g.Rename("/WORK/TOOL.TXT", "/WORK/TOOL2.TXT"));  // inside work: a host rename
        ASSERT_TRUE(g.Rename("/README.TXT", "/READ.TXT"));            // read-only owner: copy-up + whiteout
    }
    const MediaResult result = WriteBack();
    ASSERT_TRUE(result.Ok()) << result.message << "\n" << Join(result.report);
    EXPECT_FALSE(std::filesystem::exists(_folder.Path() / "work/TOOL.TXT"));
    EXPECT_EQ(Text(_folder.Path() / "work/TOOL2.TXT"), "tool v1");
    EXPECT_EQ(Text(_folder.Path() / "up/READ.TXT"), "base readme");
    EXPECT_EQ(Read("/README.TXT"), "<none>");
    EXPECT_EQ(Read("/READ.TXT"), "base readme");
}

TEST_P(ComposeWriteBack_Test, ConflictsRefuseOrKeepBoth)
{
    Insert();
    _folder.File("work/TOOL.TXT", "edited on the host", 1767272400);  // changed after the build
    ASSERT_TRUE(Guest().Write("/WORK/TOOL.TXT", std::vector<uint8_t>{'g'}));
    const MediaResult plan = WriteBack(/*plan*/ true);
    ASSERT_TRUE(plan.Ok());
    EXPECT_TRUE(Has(plan.report, "conflict")) << Join(plan.report);
    const MediaResult refused = WriteBack();
    EXPECT_EQ(refused.error, MediaError::Dirty) << refused.message;
    EXPECT_EQ(Text(_folder.Path() / "work/TOOL.TXT"), "edited on the host");

    const MediaResult both = WriteBack(false, /*keepBoth*/ true);
    ASSERT_TRUE(both.Ok()) << both.message;
    EXPECT_EQ(Text(_folder.Path() / "work/TOOL.TXT"), "edited on the host");
    EXPECT_EQ(Text(_folder.Path() / "work/TOOL (guest).TXT"), "g");
}

TEST_P(ComposeWriteBack_Test, PlanWritesNothingAndTrashIsAnError)
{
    Insert("trash");
    {
        FatGuest g = Guest();
        ASSERT_TRUE(g.Create("/WORK/NEW.TXT", std::vector<uint8_t>(5, 'n')));
        ASSERT_TRUE(g.Delete("/WORK/OLD.TXT"));
    }
    const MediaResult plan = WriteBack(/*plan*/ true);
    ASSERT_TRUE(plan.Ok()) << plan.message;
    EXPECT_TRUE(Has(plan.report, "write /WORK/NEW.TXT [work]")) << Join(plan.report);
    EXPECT_TRUE(Has(plan.report, "trash")) << Join(plan.report);
    EXPECT_FALSE(std::filesystem::exists(_folder.Path() / "work/NEW.TXT"));
    EXPECT_TRUE(_manager.Info("sd.zc")->dirty);
}

/// A write-back cut short after its journal: the next insert of the descriptor finishes it
TEST_P(ComposeWriteBack_Test, InterruptedApplyIsFinishedOnInsert)
{
    Insert();
    TearDown();
    _manager.RegisterSlot(_slot);
    const auto staged = _folder.File("work/.unreal-staging-0", "finished later");
    {
        std::ofstream journal(WriteBack::JournalFor(_descriptor));
        journal << "write\t" << FileHelper::FromFsPath(staged) << "\t" << FileHelper::FromFsPath(_folder.Path() / "work/LATE.TXT") << "\n";
        journal << "whiteout\t\t/DOC/A.TXT\nend\n";
    }
    MediaSource source;
    source.path = FileHelper::FromFsPath(_descriptor);
    const MediaResult r = _manager.Insert("sd.zc", source, {});
    ASSERT_TRUE(r.Ok()) << r.message;
    EXPECT_TRUE(Has(r.report, "interrupted write-back was completed")) << Join(r.report);
    EXPECT_EQ(Text(_folder.Path() / "work/LATE.TXT"), "finished later");
    EXPECT_EQ(Read("/WORK/LATE.TXT"), "finished later");
    EXPECT_EQ(Read("/DOC/A.TXT"), "<none>");
}

INSTANTIATE_TEST_SUITE_P(Tiers, ComposeWriteBack_Test, ::testing::Bool(), SessionSpillGuard::TierName);
