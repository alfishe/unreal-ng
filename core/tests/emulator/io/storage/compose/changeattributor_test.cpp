// ChangeAttributor (multi-source phases/c6-provenance-flatten.md §3, DT-8): a guest's writes to a composite (or any
// FAT medium with session writes) as file operations with the layer each touched. FatGuest plays the guest.

#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

#include "_helpers/fatguest.h"
#include "_helpers/fatsourceimage.h"
#include "_helpers/isoimagebuilder.h"
#include "_helpers/scratchfolder.h"
#include "_helpers/sessionspillguard.h"
#include "emulator/io/storage/compose/changeattributor.h"
#include "emulator/io/storage/compose/composedlayout.h"
#include "emulator/io/storage/sessionwritemap.h"
#include "emulator/media/compositemediumfactory.h"

namespace
{
    std::vector<uint8_t> Bytes(size_t size, uint8_t fill) { return std::vector<uint8_t>(size, fill); }

    /// Three layers: a system folder at the root, a games folder at /GAMES, an ISO's /DEMOS at /DEMOS;
    /// the composite under a change layer, as a slot holds it
    struct Session
    {
        ScratchFolder root{"change-attributor"};
        std::unique_ptr<SessionWriteMap> session;
        const IComposedLayout* layout = nullptr;
        CompositeInfo info;

        void Make(const std::string& build = "rebuild", bool imageBase = false)
        {
            root.File("sys/DSS/COMMAND.COM", std::string(3000, 'c'));
            root.File("sys/README.TXT", "readme");
            root.File("sys/AUTOEXEC.BAT", "ver");
            root.File("games/ELITE.TRD", std::string(5000, 'e'));
            root.File("games/EXOLON.SCL", std::string(2000, 'x'));
            IsoImageBuilder iso;
            iso.Add("/DEMOS/LYRA.TRD", std::string(4000, 'l'));
            const std::vector<uint8_t> bytes = iso.Build();
            std::ofstream(root.Path() / "demos.iso", std::ios::binary)
                .write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
            std::string base = "{name: sys, source: {folder: sys}}";
            if (imageBase)
            {
                const FatSourceImage image = FolderToFatDisk(root.Path() / "sys", FatType::Fat16);
                ASSERT_TRUE(image.ok()) << image.error;
                ASSERT_TRUE(SaveSparse(image, root.Path() / "sys.img"));
                base = "{name: sys, source: {image: sys.img}}";
            }
            root.File("disk.ucompose.yaml", "version: 1\ntarget: {build: " + build + ", free: 1MiB, fixedTime: 1767268800}\n"
                                            "layers:\n  - " + base + "\n"
                                            "  - {name: games, source: {folder: games}, mount: /GAMES}\n"
                                            "  - {name: demos, source: {iso: demos.iso}, from: /DEMOS, mount: /DEMOS}\n");
            std::unique_ptr<IBlockDevice> volume;
            const MediaResult result =
                CompositeMediumFactory::Build(ComposeDescriptor::Load(root.Path() / "disk.ucompose.yaml"), {}, volume, info);
            ASSERT_TRUE(result.Ok()) << result.message;
            ASSERT_EQ(info.build, build);
            session = std::make_unique<SessionWriteMap>(std::move(volume));
            layout = dynamic_cast<const IComposedLayout*>(&session->Base());
            ASSERT_NE(layout, nullptr);
        }

        ChangeSet Attribute(bool withLayout = true)
        {
            ChangeSet set;
            std::string error;
            EXPECT_TRUE(ChangeAttributor::Attribute(session->Base(), *session, *session, withLayout ? layout : nullptr, set,
                                                    &error))
                << error;
            return set;
        }

        std::string Layer(const FileChange& change) const
        {
            return change.layer < 0 ? std::string("-") : info.layers[static_cast<size_t>(change.layer)].name;
        }
    };

    /// "op path [layer]" lines, the rename's old path after an arrow
    std::vector<std::string> Lines(const Session& s, const ChangeSet& set)
    {
        std::vector<std::string> lines;
        for (const FileChange& c : set.changes)
            lines.push_back(std::string(FileChange::OpName(c.op)) + " " + (c.oldPath.empty() ? "" : c.oldPath + " -> ") + c.path +
                            " [" + s.Layer(c) + "]");
        return lines;
    }

    using Lines_t = std::vector<std::string>;

    /// Every case twice: the change layer in memory, and spilled to a file but for two sectors
    class ChangeAttributor_Test : public ::testing::TestWithParam<bool>
    {
    protected:
        SessionSpillGuard _spill{GetParam()};
    };
}  // namespace

TEST_P(ChangeAttributor_Test, NothingWrittenNothingReported)
{
    Session s;
    s.Make();
    const ChangeSet set = s.Attribute();
    EXPECT_TRUE(set.changes.empty());
    EXPECT_TRUE(set.warnings.empty());
    EXPECT_EQ(set.directoriesRead, 0u);
}

TEST_P(ChangeAttributor_Test, CreateModifyAppendTruncateDelete)
{
    Session s;
    s.Make();
    FatGuest guest(*s.session);
    ASSERT_TRUE(guest.ok());
    ASSERT_TRUE(guest.Create("/GAMES/NEW.TRD", Bytes(3000, 'n')));
    ASSERT_TRUE(guest.Poke("/README.TXT", 2, 'X'));                // modify in place
    ASSERT_TRUE(guest.Write("/GAMES/ELITE.TRD", Bytes(9000, 'E')));  // append
    ASSERT_TRUE(guest.Write("/DSS/COMMAND.COM", Bytes(100, 'c')));   // truncate
    ASSERT_TRUE(guest.Delete("/GAMES/EXOLON.SCL"));
    ASSERT_TRUE(guest.Poke("/DEMOS/LYRA.TRD", 0, 'Y'));            // the ISO layer's file

    const ChangeSet set = s.Attribute();
    EXPECT_EQ(Lines(s, set), (Lines_t{"modify /DEMOS/LYRA.TRD [demos]", "modify /DSS/COMMAND.COM [sys]",
                                      "modify /GAMES/ELITE.TRD [games]", "delete /GAMES/EXOLON.SCL [games]",
                                      "create /GAMES/NEW.TRD [-]", "modify /README.TXT [sys]"}));
    EXPECT_TRUE(set.warnings.empty()) << set.warnings.front();
    EXPECT_FALSE(set.fullScan);
    if (_spill.Active())
        EXPECT_TRUE(_spill.Spilled()) << "attributed from a spill file";
    const auto elite = std::find_if(set.changes.begin(), set.changes.end(), [](const FileChange& c) { return c.path == "/GAMES/ELITE.TRD"; });
    EXPECT_EQ(elite->sizeBefore, 5000u);
    EXPECT_EQ(elite->sizeAfter, 9000u);
}

TEST_P(ChangeAttributor_Test, RenameMoveMkdirRmdirAttributes)
{
    Session s;
    s.Make();
    FatGuest guest(*s.session);
    ASSERT_TRUE(guest.Rename("/AUTOEXEC.BAT", "/START.BAT"));
    ASSERT_TRUE(guest.Rename("/GAMES/EXOLON.SCL", "/DSS/EXOLON.SCL"));  // a move
    ASSERT_TRUE(guest.Mkdir("/WORK"));
    ASSERT_TRUE(guest.Create("/WORK/NOTE.TXT", Bytes(10, 'w')));
    ASSERT_TRUE(guest.Delete("/DEMOS/LYRA.TRD"));
    ASSERT_TRUE(guest.Rmdir("/DEMOS"));
    ASSERT_TRUE(guest.SetAttributes("/README.TXT", 0x01));

    const ChangeSet set = s.Attribute();
    EXPECT_EQ(Lines(s, set), (Lines_t{"rmdir /DEMOS [demos]", "delete /DEMOS/LYRA.TRD [demos]",
                                      "rename /GAMES/EXOLON.SCL -> /DSS/EXOLON.SCL [games]", "attributes /README.TXT [sys]",
                                      "rename /AUTOEXEC.BAT -> /START.BAT [sys]", "mkdir /WORK [-]", "create /WORK/NOTE.TXT [-]"}));
    EXPECT_TRUE(set.warnings.empty()) << set.warnings.front();
}

TEST_P(ChangeAttributor_Test, MovedDirectoryIsOneRename)
{
    Session s;
    s.Make();
    FatGuest guest(*s.session);
    ASSERT_TRUE(guest.Rename("/DSS", "/GAMES/DSS"));
    const ChangeSet set = s.Attribute();
    EXPECT_EQ(Lines(s, set), (Lines_t{"rename /DSS -> /GAMES/DSS [sys]"}));
}

TEST_P(ChangeAttributor_Test, LostClustersWarned)
{
    Session s;
    s.Make();
    FatGuest guest(*s.session);
    ASSERT_TRUE(guest.Create("/A.TXT", Bytes(10, 'a')));
    ASSERT_TRUE(guest.LoseClusters(3));
    const ChangeSet set = s.Attribute();
    EXPECT_EQ(Lines(s, set), (Lines_t{"create /A.TXT [-]"}));
    ASSERT_EQ(set.warnings.size(), 1u);
    EXPECT_NE(set.warnings[0].find("3 cluster(s)"), std::string::npos) << set.warnings[0];
}

// The evidence names the directories: a write in /GAMES reads /GAMES (and the root to find its path), not the rest;
// without a layout every directory is compared, with the same result
TEST_P(ChangeAttributor_Test, UntouchedSubtreesNotRead)
{
    Session s;
    for (int i = 0; i < 20; i++)
        s.root.File("sys/D" + std::to_string(i) + "/F.TXT", "f");
    s.Make();
    FatGuest guest(*s.session);
    ASSERT_TRUE(guest.Poke("/GAMES/ELITE.TRD", 10, 'Z'));
    const ChangeSet set = s.Attribute();
    EXPECT_EQ(Lines(s, set), (Lines_t{"modify /GAMES/ELITE.TRD [games]"}));
    EXPECT_LE(set.directoriesRead, 4u) << "the directory before and after, and the root to name it";

    const ChangeSet full = s.Attribute(/*withLayout*/ false);
    EXPECT_TRUE(full.fullScan);
    EXPECT_GT(full.directoriesRead, 20u);
    ASSERT_EQ(full.changes.size(), 1u);
    EXPECT_EQ(full.changes[0].path, "/GAMES/ELITE.TRD");
    EXPECT_EQ(full.changes[0].layer, -1) << "no layout, no layer";
}

// A graft: the base image's files belong to its layer; a grafted folder's to the folder's
TEST_P(ChangeAttributor_Test, GraftLayers)
{
    Session s;
    s.Make("graft", /*imageBase*/ true);
    FatGuest guest(*s.session);
    ASSERT_TRUE(guest.Poke("/README.TXT", 0, 'R'));
    ASSERT_TRUE(guest.Poke("/GAMES/ELITE.TRD", 0, 'E'));
    ASSERT_TRUE(guest.Mkdir("/DSS/NEW"));
    const ChangeSet set = s.Attribute();
    EXPECT_EQ(Lines(s, set), (Lines_t{"mkdir /DSS/NEW [-]", "modify /GAMES/ELITE.TRD [games]", "modify /README.TXT [sys]"}));
    EXPECT_FALSE(set.fullScan);
}

INSTANTIATE_TEST_SUITE_P(Tiers, ChangeAttributor_Test, ::testing::Bool(), SessionSpillGuard::TierName);
