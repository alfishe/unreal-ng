// MediaControl: the media verbs every surface calls (media-control-design.md)

#include <gtest/gtest.h>

#include <atomic>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <thread>
#include <vector>

#include "_helpers/emulatortesthelper.h"
#include "_helpers/scratchfolder.h"
#include "_helpers/testpathhelper.h"
#include "common/filehelper.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/io/storage/fat/fatvolumereader.h"
#include "emulator/io/storage/memorydisk.h"
#include "emulator/media/mediacontrol.h"
#include "emulator/media/mediaformatregistry.h"

namespace
{
    std::string Utf8(const std::filesystem::path& path)
    {
        const auto u8 = path.u8string();
        return std::string(u8.begin(), u8.end());
    }

    std::string Slurp(const std::filesystem::path& path)
    {
        std::ifstream in(path, std::ios::binary);
        return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    }

    std::string Fixture(const char* relative)
    {
        return Utf8(TestPathHelper::FindProjectRoot() / relative);
    }

    MediaRequest Request(std::string verb, std::string selector, std::string path = {},
                         std::map<std::string, std::string> options = {})
    {
        return MediaRequest{std::move(verb), std::move(selector), std::move(path), std::move(options)};
    }

    /// A slot that is not part of any machine (detached media)
    class LooseSlot : public IMediaSlot
    {
    public:
        explicit LooseSlot(std::string id, std::vector<std::string> tags = {})
        {
            _descriptor.id = std::move(id);
            _descriptor.kind = MediaKind::Block;
            _descriptor.label = "add-on card";
            _descriptor.tags = std::move(tags);
        }
        const SlotDescriptor& Descriptor() const override { return _descriptor; }
        void Attach(Medium& medium) override { attached = &medium; }
        void Detach() override { attached = nullptr; }
        Medium* attached = nullptr;

    private:
        SlotDescriptor _descriptor;
    };

    class MediaControl_Test : public ::testing::Test
    {
    protected:
        Emulator* _emulator = nullptr;
        EmulatorContext* _context = nullptr;

        void Create(const char* model)
        {
            _emulator = EmulatorTestHelper::CreateStandardEmulator(model, LoggerLevel::LogError);
            ASSERT_NE(_emulator, nullptr);
            _context = _emulator->GetContext();
        }

        void TearDown() override
        {
            if (_emulator)
                EmulatorTestHelper::CleanupEmulator(_emulator);
        }

        MediaReply Run(const MediaRequest& request) { return MediaControl(_context).Execute(request); }

        std::string Resolve(const std::string& selector, MediaError* error = nullptr)
        {
            std::string slot;
            const MediaResult result = MediaControl::ResolveSelector(*_context->pMediaManager, selector, slot);
            if (error)
                *error = result.error;
            return result.Ok() ? slot : result.message;
        }

        void GuestWrite(const char* slot)
        {
            Medium* medium = _context->pMediaManager->GetMedium(slot);
            ASSERT_NE(medium, nullptr);
            if (DiskImage* disk = medium->Floppy())
            {
                const std::vector<uint8_t> sector(256, 0x5A);
                disk->getTrack(5)->writeSectorData(0, sector.data(), sector.size());
            }
            else
            {
                const std::vector<uint8_t> sector(512, 0x5A);
                ASSERT_TRUE(medium->Block()->WriteSector(7, sector.data()));
            }
        }
    };
}  // namespace

/// Selectors (§3.3): id, alias, kind:index, tag query; unknown and ambiguous
/// ones name the candidates
TEST_F(MediaControl_Test, SelectorsResolveEveryWay)
{
    Create("PENTAGON");
    EXPECT_EQ(Resolve("fdd.b"), "fdd.b");
    EXPECT_EQ(Resolve("B"), "fdd.b");
    EXPECT_EQ(Resolve("b:"), "fdd.b");
    EXPECT_EQ(Resolve("floppy:2"), "fdd.c");
    EXPECT_EQ(Resolve("tag:floppy+boot"), "fdd.a");
    EXPECT_EQ(Resolve("tag:trdos+removable+boot"), "fdd.a");

    MediaError error = MediaError::None;
    const std::string ambiguous = Resolve("tag:floppy", &error);
    EXPECT_EQ(error, MediaError::AmbiguousSlot);
    EXPECT_NE(ambiguous.find("fdd.a, fdd.b"), std::string::npos) << ambiguous;
    const std::string unknown = Resolve("Z", &error);
    EXPECT_EQ(error, MediaError::UnknownSlot);
    EXPECT_NE(unknown.find("fdd.a (A)"), std::string::npos) << "the error lists the machine's slots: " << unknown;
    Resolve("", &error);
    EXPECT_EQ(error, MediaError::BadRequest);
}

TEST_F(MediaControl_Test, APlus3HasNoDriveC)
{
    Create("PLUS3");
    MediaError error = MediaError::None;
    const std::string message = Resolve("C", &error);
    EXPECT_EQ(error, MediaError::UnknownSlot);
    EXPECT_NE(message.find("fdd.b (B)"), std::string::npos) << message;
    EXPECT_EQ(Resolve("tag:plus3dos+boot"), "fdd.a");
}

/// Option names and values are checked once, here, for every surface
TEST_F(MediaControl_Test, VerbsAndOptionsAreChecked)
{
    Create("PENTAGON");
    const std::string trd = Fixture("testdata/loaders/trd/EyeAche.trd");

    MediaReply reply = Run(Request("mount", "A", trd));
    EXPECT_EQ(reply.result.error, MediaError::BadRequest);
    EXPECT_NE(reply.result.message.find("verbs: list"), std::string::npos) << reply.result.message;

    reply = Run(Request("insert", "A", trd, {{"acess", "readonly"}}));
    EXPECT_EQ(reply.result.error, MediaError::BadRequest);
    EXPECT_NE(reply.result.message.find("options: access"), std::string::npos) << reply.result.message;

    reply = Run(Request("insert", "A", trd, {{"access", "sesion"}}));
    EXPECT_EQ(reply.result.error, MediaError::BadRequest);
    EXPECT_EQ(reply.HttpStatus(), 400);

    reply = Run(Request("eject", "A", {}, {{"save", ""}, {"discard", ""}}));
    EXPECT_EQ(reply.result.error, MediaError::BadRequest) << "one disposition at a time";

    reply = Run(Request("insert", "A"));
    EXPECT_EQ(reply.result.error, MediaError::BadRequest) << "insert needs a path";
}

/// insert auto (§3.5): the kind from the content, the first empty slot of it
TEST_F(MediaControl_Test, AutoPicksTheSlotFromTheContent)
{
    Create("ATM3");  // ZX-Evo: four floppy drives and the SD card
    const std::string trd = Fixture("testdata/loaders/trd/EyeAche.trd");
    const std::string scl = Fixture("testdata/loaders/scl/insult.scl");

    MediaReply reply = Run(Request("insert", "auto", trd));
    ASSERT_TRUE(reply.result.Ok()) << reply.result.message;
    EXPECT_EQ(reply.slot, "fdd.a");
    reply = Run(Request("insert", "auto", scl));
    ASSERT_TRUE(reply.result.Ok()) << reply.result.message;
    EXPECT_EQ(reply.slot, "fdd.b") << "A is taken: the next empty drive";

    ScratchFolder folder("control-auto");
    folder.File("card.img", std::string(64 * 512, '\0'));
    folder.File("notes.txt", "not a medium");
    reply = Run(Request("insert", "auto", Utf8(folder.Path() / "card.img")));
    EXPECT_EQ(reply.result.error, MediaError::AmbiguousSlot) << "the SD card or the hard disk: the caller names one";

    reply = Run(Request("insert", "auto", Utf8(folder.Path() / "notes.txt")));
    EXPECT_EQ(reply.result.error, MediaError::UnknownFormat);

    // A folder: a floppy on a machine with drives, unless the caller says block
    reply = Run(Request("insert", "auto", Utf8(folder.Path()), {{"kind", "floppy"}}));
    ASSERT_TRUE(reply.result.Ok()) << reply.result.message;
    EXPECT_EQ(reply.slot, "fdd.c");
}

/// insert auto with several slots for a file names them (in the chooser's
/// order: the Z-Controller before an add-on's slot, the SD slots before the
/// hard disk) and inserts nothing; one slot takes it at once
TEST_F(MediaControl_Test, AutoNamesTheSlotsWhenSeveralTakeAFile)
{
    Create("ATM3");
    MediaManager& manager = *_context->pMediaManager;
    LooseSlot addon("sd.addon", {"sd", "addon"});  // sorts before sd.zc, as sd.ngs does
    manager.RegisterSlot(addon);

    ScratchFolder folder("control-auto-several");
    const std::string card = Utf8(folder.File("card.img", std::string(64 * 512, '\0')));
    MediaReply reply = Run(Request("insert", "auto", card));
    EXPECT_EQ(reply.result.error, MediaError::AmbiguousSlot);
    const std::string& message = reply.result.message;
    EXPECT_NE(message.find("several slots take 'card.img'"), std::string::npos) << message;
    EXPECT_LT(message.find("sd.zc"), message.find("sd.addon")) << message;
    EXPECT_LT(message.find("sd.addon"), message.find("ide0.master")) << message;
    EXPECT_FALSE(manager.Info("sd.zc")->present);
    EXPECT_FALSE(manager.Info("sd.addon")->present);

    std::string hdf(0x16 + 64 * 512, '\0');
    hdf.replace(0, 7, "RS-IDE\x1A");
    hdf[9] = 0x16;
    reply = Run(Request("insert", "auto", Utf8(folder.File("system.hdf", hdf))));
    ASSERT_TRUE(reply.result.Ok()) << reply.result.message;
    EXPECT_EQ(reply.slot, "ide0.master") << "the slave is the CD-ROM drive: one target";
    manager.UnregisterSlot("sd.addon");
}

/// An ISO goes to a CD-ROM drive, never to a floppy drive; a machine without
/// one refuses it
TEST_F(MediaControl_Test, AnIsoGoesOnlyToACdRomDrive)
{
    ScratchFolder folder("control-auto-iso");
    std::string iso(0x8000 + 2048, '\0');  // the system area, then the primary volume descriptor
    iso.replace(0x8001, 5, "CD001");
    const std::string path = Utf8(folder.File("disc.iso", iso));

    Create("PENTAGON");  // floppy drives, no CD-ROM drive
    MediaReply reply = Run(Request("insert", "auto", path));
    EXPECT_EQ(reply.result.error, MediaError::KindMismatch) << reply.slot;
    reply = Run(Request("insert", "fdd.a", path));
    EXPECT_FALSE(reply.result.Ok()) << "a floppy drive does not take an ISO";
    EXPECT_FALSE(_context->pMediaManager->Info("fdd.a")->present);
    EmulatorTestHelper::CleanupEmulator(_emulator);
    _emulator = nullptr;

    Create("ATM3");  // ZX-Evo: the CD-ROM drive is the IDE slave
    reply = Run(Request("insert", "auto", path));
    ASSERT_TRUE(reply.result.Ok()) << reply.result.message;
    EXPECT_EQ(reply.slot, "ide0.slave");
    EXPECT_FALSE(_context->pMediaManager->Info("fdd.a")->present);
}

/// A dirty medium leaves only with a disposition (§3.6), given with the request.
/// Writes three 640 KB images: slower than 50 ms
TEST_F(MediaControl_Test, DirtyMediaLeaveOnlyWithADisposition)
{
    Create("PENTAGON");
    ScratchFolder folder("control-dispositions");
    const auto disk1 = folder.File("elite-1.trd", Slurp(FileHelper::ToFsPath(Fixture("testdata/loaders/trd/EyeAche.trd"))));
    const auto disk2 = folder.File("elite-2.trd", Slurp(FileHelper::ToFsPath(Fixture("testdata/loaders/trd/Satisfaction.trd"))));
    const std::string original = Slurp(disk1);

    ASSERT_TRUE(Run(Request("insert", "A", Utf8(disk1))).result.Ok());
    GuestWrite("fdd.a");

    MediaReply reply = Run(Request("swap", "A", Utf8(disk2)));
    EXPECT_EQ(reply.result.error, MediaError::Dirty);
    EXPECT_EQ(reply.HttpStatus(), 409);
    EXPECT_NE(reply.result.message.find("save, export <path> or discard"), std::string::npos) << reply.result.message;
    EXPECT_EQ(_context->pMediaManager->Info("fdd.a")->source, Utf8(disk1)) << "nothing changed";

    reply = Run(Request("swap", "A", Utf8(disk2), {{"save", ""}}));
    ASSERT_TRUE(reply.result.Ok()) << reply.result.message;
    EXPECT_NE(Slurp(disk1), original) << "disk 1's writes are in its file";
    EXPECT_EQ(_context->pMediaManager->Info("fdd.a")->source, Utf8(disk2));

    GuestWrite("fdd.a");
    const auto kept = folder.Path() / "elite-2-saved.trd";
    const std::string disk2Before = Slurp(disk2);
    reply = Run(Request("eject", "A", {}, {{"export", Utf8(kept)}}));
    ASSERT_TRUE(reply.result.Ok()) << reply.result.message;
    EXPECT_TRUE(std::filesystem::exists(kept)) << "the writes went to the new file";
    EXPECT_EQ(Slurp(disk2), disk2Before) << "the source is untouched";
    EXPECT_FALSE(_context->pMediaManager->Info("fdd.a")->present);

    ASSERT_TRUE(Run(Request("insert", "A", Utf8(disk2))).result.Ok());
    GuestWrite("fdd.a");
    reply = Run(Request("eject", "A", {}, {{"discard", "true"}}));
    ASSERT_TRUE(reply.result.Ok()) << reply.result.message;
    EXPECT_EQ(Slurp(disk2), disk2Before);
}

/// Sync (default) returns when the medium is in; async returns at once with
/// pending. The frames run on another thread here, as on a running machine
TEST_F(MediaControl_Test, SyncWaitsForTheSlotAsyncDoesNot)
{
    Create("PENTAGON");
    MediaManager& manager = *_context->pMediaManager;
    MediaSetEntry shortDelay;
    shortDelay.slotId = "fdd.a";
    shortDelay.swapDelayMs = 40;  // two frames
    manager.ApplyConfiguredMedia({shortDelay});
    const std::string trd = Fixture("testdata/loaders/trd/EyeAche.trd");
    const std::string scl = Fixture("testdata/loaders/scl/insult.scl");
    ASSERT_TRUE(Run(Request("insert", "A", trd)).result.Ok());

    std::atomic<bool> running{true};
    manager.SetApplyNowProbe([] { return false; });  // "the machine runs"
    std::thread frames([&] {
        while (running)
        {
            manager.ApplyPending();
            std::this_thread::yield();
        }
    });

    MediaReply reply = Run(Request("insert", "A", scl, {{"async", ""}}));
    ASSERT_TRUE(reply.result.Ok()) << reply.result.message;
    EXPECT_TRUE(reply.pending) << "async: queued, not applied";

    reply = Run(Request("insert", "A", trd));
    ASSERT_TRUE(reply.result.Ok()) << reply.result.message;
    EXPECT_FALSE(reply.pending) << "sync: the disk is in on return";
    EXPECT_EQ(manager.Info("fdd.a")->format, "trd");

    running = false;
    frames.join();
    manager.SetApplyNowProbe(nullptr);
}

/// A medium whose slot went away is listed as detached and can be exported
/// or discarded under that slot's id
TEST_F(MediaControl_Test, DetachedMediaAreListedExportedAndDiscarded)
{
    Create("PENTAGON");
    MediaManager& manager = *_context->pMediaManager;
    LooseSlot card("sd.addon");
    manager.RegisterSlot(card);
    MediaSource blank;
    blank.type = MediaSourceType::Blank;
    ASSERT_TRUE(manager.Insert("sd.addon", MediaFormatRegistry::WrapBlock(blank, AccessMode::Session, "memory",
                                                                          std::make_unique<MemoryDisk>(16))).Ok());
    GuestWrite("sd.addon");
    manager.UnregisterSlot("sd.addon");  // the add-on is removed

    MediaReply list = Run(Request("list", ""));
    const std::string json = list.ToJson();
    EXPECT_NE(json.find(R"("detached":[{"id":"sd.addon")"), std::string::npos) << json;
    EXPECT_NE(json.find(R"("state":"detached")"), std::string::npos) << json;

    ScratchFolder folder("control-detached");
    const auto image = folder.Path() / "addon.img";
    MediaReply exported = Run(Request("export", "sd.addon", Utf8(image)));
    ASSERT_TRUE(exported.result.Ok()) << exported.result.message;
    EXPECT_EQ(std::filesystem::file_size(image), 16u * 512);

    ASSERT_TRUE(Run(Request("discard", "sd.addon")).result.Ok());
    EXPECT_TRUE(manager.Detached().empty());
}

/// The reply every surface returns: the envelope and the slot fields (MC-1, MC-10)
TEST_F(MediaControl_Test, ReplyShape)
{
    Create("PENTAGON");
    ASSERT_TRUE(Run(Request("insert", "B", Fixture("testdata/loaders/trd/EyeAche.trd"))).result.Ok());

    const MediaReply info = Run(Request("info", "b"));
    ASSERT_TRUE(info.result.Ok()) << info.result.message;
    const StateNode value = info.ToValue();
    EXPECT_TRUE(value.find("ok")->b);
    EXPECT_EQ(value.find("slot")->s, "fdd.b");
    EXPECT_FALSE(value.find("pending")->b);
    EXPECT_GT(value.find("revision")->i, 0);
    const StateNode* slot = value.find("info");
    ASSERT_NE(slot, nullptr);
    EXPECT_EQ(slot->find("id")->s, "fdd.b");
    EXPECT_EQ(slot->find("kind")->s, "floppy");
    EXPECT_EQ(slot->find("index")->i, 1);
    EXPECT_EQ(slot->find("state")->s, "present");
    EXPECT_EQ(slot->find("aliases")->items.front().s, "B");
    EXPECT_EQ(slot->find("medium")->find("format")->s, "trd");
    EXPECT_EQ(slot->find("medium")->find("access")->s, "session");

    const MediaReply bad = Run(Request("info", "Q"));
    const std::string json = bad.ToJson();
    EXPECT_NE(json.find(R"("ok":false,"error":"unknown-slot")"), std::string::npos) << json;
    EXPECT_EQ(bad.HttpStatus(), 404);

    const MediaReply formats = Run(Request("formats", "", {}, {{"kind", "floppy"}}));
    EXPECT_NE(formats.ToJson().find(R"("floppy":["trd")"), std::string::npos) << formats.ToJson();
}

/// targets (media-drop-targets §4.4): the plan as data; a refusal is an answer, not an error
TEST_F(MediaControl_Test, TargetsListWhereAFileCanGo)
{
    ScratchFolder folder("control-targets");
    std::string iso(0x8000 + 2048, '\0');
    iso.replace(0x8001, 5, "CD001");
    const std::string disc = Utf8(folder.File("disc.iso", iso));

    Create("ATM3");
    MediaReply reply = Run(Request("targets", "", disc));
    ASSERT_TRUE(reply.result.Ok()) << reply.result.message;
    StateNode body = reply.ToValue();
    EXPECT_EQ(body.find("file")->find("kinds")->items.front().s, "optical");
    EXPECT_EQ(body.find("file")->find("format")->s, "iso");
    ASSERT_EQ(body.find("targets")->items.size(), 1u);
    const StateNode& target = body.find("targets")->items.front();
    EXPECT_EQ(target.find("action")->s, "insert");
    EXPECT_EQ(target.find("slot")->s, "ide0.slave");
    EXPECT_EQ(target.find("label")->s, "IDE slave (CD-ROM)");
    EXPECT_EQ(target.find("occupiedBy")->kind, StateNode::Kind::Null);
    EXPECT_EQ(body.find("default")->i, 0);
    EXPECT_EQ(body.find("refusal")->kind, StateNode::Kind::Null);
    EXPECT_EQ(reply.slot, "ide0.slave");
    EXPECT_FALSE(_context->pMediaManager->Info("ide0.slave")->present) << "targets inserts nothing";

    EmulatorTestHelper::CleanupEmulator(_emulator);
    _emulator = nullptr;
    Create("PENTAGON");
    reply = Run(Request("targets", "", disc));
    ASSERT_TRUE(reply.result.Ok()) << "a refusal is an answer";
    body = reply.ToValue();
    EXPECT_TRUE(body.find("targets")->items.empty());
    EXPECT_EQ(body.find("default")->kind, StateNode::Kind::Null);
    EXPECT_NE(body.find("refusal")->s.find("no CD-ROM drive"), std::string::npos) << body.find("refusal")->s;

    EXPECT_EQ(Run(Request("targets", "")).result.error, MediaError::BadRequest) << "targets needs a path";
}

TEST_F(MediaControl_Test, CreateProtectAndRescan)
{
    Create("ATM3");
    MediaManager& manager = *_context->pMediaManager;

    MediaReply reply = Run(Request("create", "A"));
    ASSERT_TRUE(reply.result.Ok()) << reply.result.message;
    EXPECT_EQ(reply.body.find("format")->s, "unformatted");
    EXPECT_EQ(reply.body.find("cylinders")->i, 80);

    reply = Run(Request("create", "sd", {}, {{"size", "1000"}}));
    EXPECT_EQ(reply.result.error, MediaError::BadRequest) << "a card size is a multiple of 512";
    reply = Run(Request("create", "sd", {}, {{"size", "1048576"}}));
    ASSERT_TRUE(reply.result.Ok()) << reply.result.message;
    EXPECT_EQ(manager.GetMedium("sd.zc")->Block()->SectorCount(), 2048u);

    reply = Run(Request("protect", "sd", {}, {{"on", "true"}}));
    ASSERT_TRUE(reply.result.Ok());
    EXPECT_TRUE(manager.Info("sd.zc")->writeProtect);

    // Rescan: a file added on the host appears in the folder volume
    ScratchFolder folder("control-rescan");
    folder.File("first.txt", "1");
    reply = Run(Request("insert", "sd", Utf8(folder.Path()), {{"free", "1048576"}, {"discard", ""}}));
    ASSERT_TRUE(reply.result.Ok()) << reply.result.message;
    folder.File("second.txt", "2");
    reply = Run(Request("rescan", "sd"));
    ASSERT_TRUE(reply.result.Ok()) << reply.result.message;
    FatVolumeReader reader;
    ASSERT_TRUE(reader.Open(*manager.GetMedium("sd.zc")->Block()));
    std::vector<uint8_t> data;
    EXPECT_TRUE(reader.ReadFile("/second.txt", data)) << "the new host file is on the volume";
}
