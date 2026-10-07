// MediaTargets: which slots of a machine take a file (media-drop-targets design §4, §7)

#include <gtest/gtest.h>

#include <filesystem>
#include <string>
#include <vector>

#include "_helpers/emulatortesthelper.h"
#include "_helpers/scratchfolder.h"
#include "_helpers/testpathhelper.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/media/mediamanager.h"
#include "emulator/media/mediatargets.h"

namespace
{
    std::string Utf8(const std::filesystem::path& path)
    {
        const auto u8 = path.u8string();
        return std::string(u8.begin(), u8.end());
    }

    std::string Fixture(const char* relative)
    {
        return Utf8(TestPathHelper::FindProjectRoot() / relative);
    }

    /// An ISO 9660 image: the system area, then the primary volume descriptor
    std::string Iso(size_t size = 0x8000 + 2048)
    {
        std::string iso(size, '\0');
        iso.replace(0x8001, 5, "CD001");
        return iso;
    }

    /// A FAT16 volume: a jump, the "FAT" marker of the BPB, the boot signature
    std::string FatCard(size_t size = 64 * 1024)
    {
        std::string card(size, '\0');
        card[0] = static_cast<char>(0xEB);
        card.replace(0x36, 5, "FAT16");
        card[510] = 0x55;
        card[511] = static_cast<char>(0xAA);
        return card;
    }

    /// An MBR with one FAT16 partition
    std::string PartitionedDisk(size_t size = 64 * 1024)
    {
        std::string disk(size, '\0');
        disk[446 + 4] = 0x06;
        disk[446 + 8] = 1;     // starts at sector 1
        disk[446 + 12] = 100;  // 100 sectors
        disk[510] = 0x55;
        disk[511] = static_cast<char>(0xAA);
        return disk;
    }

    std::string Hdf()
    {
        std::string hdf(0x16 + 64 * 512, '\0');
        hdf.replace(0, 7, "RS-IDE\x1A");
        hdf[8] = 0;
        hdf[9] = 0x16;  // data offset
        return hdf;
    }

    std::vector<std::string> Slots(const MediaPlan& plan)
    {
        std::vector<std::string> slots;
        for (const MediaTarget& target : plan.targets)
            slots.push_back(target.action == MediaTarget::Action::Insert ? target.slotId : std::string("<action>"));
        return slots;
    }

    class MediaTargets_Test : public ::testing::Test
    {
    protected:
        Emulator* _emulator = nullptr;
        EmulatorContext* _context = nullptr;
        ScratchFolder _folder{"media-targets"};

        void Create(const char* model)
        {
            if (_emulator)
                EmulatorTestHelper::CleanupEmulator(_emulator);
            _emulator = EmulatorTestHelper::CreateStandardEmulator(model, LoggerLevel::LogError);
            ASSERT_NE(_emulator, nullptr);
            _context = _emulator->GetContext();
        }

        bool HasSlot(const char* id) const { return _context->pMediaManager->HasSlot(id); }

        std::string File(const char* name, const std::string& contents) { return Utf8(_folder.File(name, contents)); }

        MediaPlan PlanFor(const std::string& path) { return MediaTargets::Plan(_context, MediaTargets::Classify(path)); }

        void TearDown() override
        {
            if (_emulator)
                EmulatorTestHelper::CleanupEmulator(_emulator);
        }
    };
}  // namespace

/// §4.1: one case per evidence row. Writes a 720 KB and a 688 KB image (the size
/// rules need them): slower than 50 ms
TEST_F(MediaTargets_Test, ClassifyByContentThenExtension)
{
    struct Case
    {
        std::string path;
        std::vector<FileKind> kinds;
        std::string format;
    };

    // An ISO whose system area happens to carry the TR-DOS id byte (#10 at #8E7)
    std::string trickyIso = Iso();
    trickyIso[0x8E7] = 0x10;
    // An image with the TR-DOS id byte, one sector bigger than any TR-DOS disk (2 x 86 x 16 x 256)
    std::string bigImage(2 * 86 * 16 * 256 + 256, '\0');
    bigImage[0x8E7] = 0x10;

    const std::vector<Case> cases = {
        {File("disc.iso", Iso()), {FileKind::Optical}, "iso"},
        {File("tricky.iso", trickyIso), {FileKind::Optical}, "iso"},
        {File("misnamed.img", Iso()), {FileKind::Optical}, "iso"},
        {File("card.img", FatCard()), {FileKind::SdCard, FileKind::Hdd}, "fat"},
        {File("card.hdd", FatCard()), {FileKind::Hdd, FileKind::SdCard}, "fat"},
        {File("disk.img", PartitionedDisk()), {FileKind::SdCard, FileKind::Hdd}, "mbr"},
        {File("system.hdf", Hdf()), {FileKind::Hdd}, "hdf"},
        {Fixture("testdata/media/chd/mixed-default.chd"), {FileKind::Hdd, FileKind::SdCard}, "chd"},
        {File("blank.sd", std::string(64 * 1024, '\0')), {FileKind::SdCard, FileKind::Hdd}, "raw"},
        {File("big.img", bigImage), {FileKind::SdCard, FileKind::Hdd}, "raw"},
        {File("pc.img", std::string(737280, '\0')), {FileKind::Floppy}, "rawpc"},
        {Fixture("testdata/loaders/trd/EyeAche.trd"), {FileKind::Floppy}, "trd"},
        {Fixture("testdata/loaders/scl/insult.scl"), {FileKind::Floppy}, "scl"},
        {Fixture("testdata/loaders/tap/AYtest_v0.2.tap"), {FileKind::Tape}, "tap"},
        {File("game.z80", std::string(100, '\0')), {FileKind::Snapshot}, "z80"},
        {File("state.bin", std::string("ZXST") + std::string(60, '\0')), {FileKind::Snapshot}, "szx"},
        {File("demo.rzx", std::string("RZX!") + std::string(60, '\0')), {FileKind::Rzx}, "rzx"},
        {File("labels.map", "x"), {FileKind::Symbols}, "map"},
        {File("group.zxp", "x"), {FileKind::ZxPoly}, "zxp"},
        {File("notes.txt", "not a medium"), {}, ""},
        {Utf8(_folder.Path()), {FileKind::Floppy, FileKind::SdCard, FileKind::Hdd, FileKind::Tape}, "folder"},
    };
    for (const Case& c : cases)
    {
        const FileClass file = MediaTargets::Classify(c.path);
        EXPECT_EQ(file.kinds, c.kinds) << c.path;
        EXPECT_EQ(file.format, c.format) << c.path;
        if (!c.kinds.empty())
            EXPECT_FALSE(file.evidence.empty()) << c.path;
    }
}

/// §3 worked examples on the shipped machines
TEST_F(MediaTargets_Test, PlanOnThePentagon)
{
    Create("PENTAGON");
    const MediaPlan trd = PlanFor(Fixture("testdata/loaders/trd/EyeAche.trd"));
    EXPECT_EQ(Slots(trd), (std::vector<std::string>{"fdd.a", "fdd.b", "fdd.c", "fdd.d"}));
    ASSERT_EQ(trd.defaultTarget, 0) << "the floppy shortcut: drive A";
    EXPECT_TRUE(trd.targets[0].autostart);
    EXPECT_FALSE(trd.targets[1].autostart);
    EXPECT_EQ(trd.targets[0].label, "Drive A");

    const MediaPlan iso = PlanFor(File("disc.iso", Iso()));
    EXPECT_TRUE(iso.Refused());
    EXPECT_NE(iso.refusal.find("no CD-ROM drive"), std::string::npos) << iso.refusal;

    if (HasSlot("ide0.master") && HasSlot("ide0.slave"))
    {
        const MediaPlan hdf = PlanFor(File("system.hdf", Hdf()));
        EXPECT_EQ(Slots(hdf), (std::vector<std::string>{"ide0.master", "ide0.slave"}));
        EXPECT_EQ(hdf.defaultTarget, -1) << "two hard-disk units: the user chooses";
    }
}

TEST_F(MediaTargets_Test, PlanOnTheZxEvo)
{
    Create("ATM3");  // NemoIDE: a hard disk on the master, the CD-ROM drive on the slave; the Z-Controller SD slot
    ASSERT_TRUE(HasSlot("ide0.slave"));

    const MediaPlan iso = PlanFor(File("disc.iso", Iso()));
    EXPECT_EQ(Slots(iso), (std::vector<std::string>{"ide0.slave"}));
    EXPECT_EQ(iso.defaultTarget, 0) << "one target: no question";

    const MediaPlan hdf = PlanFor(File("system.hdf", Hdf()));
    EXPECT_EQ(Slots(hdf), (std::vector<std::string>{"ide0.master"})) << "the slave is the CD-ROM drive";
    EXPECT_EQ(hdf.defaultTarget, 0);

    // A card image: the SD slots first (the Z-Controller before an add-on's), then the hard disk
    const MediaPlan card = PlanFor(File("card.img", FatCard()));
    std::vector<std::string> expected = {"sd.zc"};
    if (HasSlot("sd.ngs"))
        expected.push_back("sd.ngs");
    expected.push_back("ide0.master");
    EXPECT_EQ(Slots(card), expected);
    EXPECT_EQ(card.defaultTarget, -1) << "several targets: the user chooses";
    EXPECT_EQ(card.SlotList().find("ide0.slave"), std::string::npos);
}

/// A composition descriptor goes where its target says: a CD drive for an ISO target, the SD and IDE disks for a
/// FAT one, all of them (disks first, or the CD first when every layer is an ISO) when it does not say
TEST_F(MediaTargets_Test, DescriptorFollowsItsTarget)
{
    Create("ATM3");
    ASSERT_TRUE(HasSlot("ide0.slave"));
    std::vector<std::string> disks = {"sd.zc"};
    if (HasSlot("sd.ngs"))
        disks.push_back("sd.ngs");
    disks.push_back("ide0.master");

    const MediaPlan cd = PlanFor(File("cd.ucompose.yaml", "version: 1\ntarget: {kind: optical}\nlayers: [{source: {folder: .}}]\n"));
    EXPECT_EQ(Slots(cd), (std::vector<std::string>{"ide0.slave"}));
    EXPECT_EQ(cd.defaultTarget, 0);

    const MediaPlan fat = PlanFor(File("sd.ucompose.yaml", "version: 1\ntarget: {fs: fat32}\nlayers: [{source: {folder: .}}]\n"));
    EXPECT_EQ(Slots(fat), disks);

    std::vector<std::string> any = disks;
    any.push_back("ide0.slave");
    const MediaPlan either = PlanFor(File("any.ucompose.yaml", "version: 1\nlayers: [{source: {folder: .}}]\n"));
    EXPECT_EQ(Slots(either), any);

    const MediaPlan isos = PlanFor(File("isos.ucompose.yaml", "version: 1\nlayers: [{source: {iso: a.iso}}]\n"));
    ASSERT_FALSE(Slots(isos).empty());
    EXPECT_EQ(Slots(isos).front(), "ide0.slave") << "every layer an ISO: the CD drive first";
}

/// Creating the 48K machine dominates (about 60 ms)
TEST_F(MediaTargets_Test, PlanOnAMachineWithoutIde)
{
    Create("48K");
    ASSERT_FALSE(HasSlot("ide0.master"));
    const MediaPlan iso = PlanFor(File("disc.iso", Iso()));
    EXPECT_TRUE(iso.Refused());
    EXPECT_EQ(iso.refusal, "no CD-ROM drive: this machine has no IDE board");

    const MediaPlan hdf = PlanFor(File("system.hdf", Hdf()));
    EXPECT_TRUE(hdf.Refused());
    EXPECT_EQ(hdf.refusal, "no hard-disk unit on this machine");

    const MediaPlan text = PlanFor(File("notes.txt", "not a medium"));
    EXPECT_TRUE(text.Refused());
    EXPECT_NE(text.refusal.find("is not a medium"), std::string::npos) << text.refusal;
}

/// Rule 4: with no machine, only a file that has always started one gets a target
TEST_F(MediaTargets_Test, PlanWithNoMachine)
{
    const MediaPlan iso = MediaTargets::Plan(nullptr, MediaTargets::Classify(File("disc.iso", Iso())));
    EXPECT_TRUE(iso.Refused());
    EXPECT_NE(iso.refusal.find("start a machine with a CD-ROM drive"), std::string::npos) << iso.refusal;

    const MediaPlan card = MediaTargets::Plan(nullptr, MediaTargets::Classify(File("card.img", FatCard())));
    EXPECT_TRUE(card.Refused());

    const MediaPlan trd = MediaTargets::Plan(nullptr, MediaTargets::Classify(Fixture("testdata/loaders/trd/EyeAche.trd")));
    ASSERT_EQ(trd.targets.size(), 1u);
    EXPECT_EQ(trd.targets[0].action, MediaTarget::Action::NewMachine);
    EXPECT_EQ(trd.targets[0].model, "PENTAGON");
    EXPECT_EQ(trd.defaultTarget, 0);

    const MediaPlan snapshot = MediaTargets::Plan(nullptr, MediaTargets::Classify(File("game.z80", std::string(100, '\0'))));
    ASSERT_EQ(snapshot.targets.size(), 1u);
    EXPECT_EQ(snapshot.targets[0].action, MediaTarget::Action::Load);
}

/// The order of the targets does not depend on what the slots hold: the tiles keep their place
TEST_F(MediaTargets_Test, PlanKeepsSlotOrderWhateverTheyHoldAndNamesTheOccupant)
{
    Create("PENTAGON");
    const std::string first = Fixture("testdata/loaders/trd/EyeAche.trd");
    ASSERT_TRUE(MediaControl(_context).Execute(MediaRequest{"insert", "fdd.a", first, {}}).result.Ok());

    const MediaPlan plan = PlanFor(Fixture("testdata/loaders/scl/insult.scl"));
    EXPECT_EQ(Slots(plan), (std::vector<std::string>{"fdd.a", "fdd.b", "fdd.c", "fdd.d"}));
    EXPECT_EQ(plan.targets.front().occupiedBy, first);
    ASSERT_GE(plan.defaultTarget, 0);
    EXPECT_EQ(plan.targets[static_cast<size_t>(plan.defaultTarget)].slotId, "fdd.a")
        << "the drop shortcut still names drive A";
}

TEST_F(MediaTargets_Test, ApplyInsertsTheChosenTarget)
{
    Create("ATM3");
    const MediaPlan iso = PlanFor(File("disc.iso", Iso()));
    ASSERT_EQ(iso.defaultTarget, 0);
    const MediaReply reply = MediaTargets::Apply(_context, iso, 0);
    ASSERT_TRUE(reply.result.Ok()) << reply.result.message;
    EXPECT_EQ(reply.slot, "ide0.slave");
    EXPECT_TRUE(_context->pMediaManager->Info("ide0.slave")->present);
    EXPECT_FALSE(_context->pMediaManager->Info("fdd.a")->present);

    EXPECT_EQ(MediaTargets::Apply(_context, iso, 5).result.error, MediaError::BadRequest);
    const MediaPlan snapshot = PlanFor(File("game.z80", std::string(100, '\0')));
    EXPECT_EQ(MediaTargets::Apply(_context, snapshot, 0).result.error, MediaError::NotSupported);
}
