// S2 session delta (multi-source phases/c6-provenance-flatten.md §4; flatten-strategies.md S2, DT-9, DT-13): a
// composite's guest writes saved next to its descriptor, restored at the next insert over the same sources only.

#include <gtest/gtest.h>

#include <algorithm>
#include <atomic>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include "_helpers/fatguest.h"
#include "_helpers/scratchfolder.h"
#include "_helpers/sessionspillguard.h"
#include "_helpers/testwaithelper.h"
#include "3rdparty/message-center/messagecenter.h"
#include "emulator/notifications.h"
#include "emulator/platform.h"
#include "emulator/io/storage/fat/fatvolumereader.h"
#include "emulator/io/storage/sessionwritemap.h"
#include "emulator/media/mediamanager.h"
#include "emulator/media/sessiondelta.h"

namespace
{
    class DeltaSlot : public IMediaSlot
    {
    public:
        DeltaSlot()
        {
            _descriptor.id = "sd.zc";
            _descriptor.kind = MediaKind::Block;
            _descriptor.label = "SD card";
            _descriptor.tags = {"sd"};
        }
        const SlotDescriptor& Descriptor() const override { return _descriptor; }
        void Attach(Medium& medium) override { attached = &medium; }
        void Detach() override { attached = nullptr; }
        void SourceChanged(Medium&) override {}
        Medium* attached = nullptr;

    private:
        SlotDescriptor _descriptor;
    };

    bool Has(const std::vector<std::string>& lines, const std::string& text)
    {
        return std::any_of(lines.begin(), lines.end(), [&text](const std::string& l) { return l.find(text) != std::string::npos; });
    }

    std::string Join(const std::vector<std::string>& lines)
    {
        std::string text;
        for (const std::string& l : lines)
            text += l + "\n";
        return text;
    }

    class ComposeDelta_Test : public ::testing::TestWithParam<bool>
    {
    protected:
        SessionSpillGuard _spill{GetParam()};  // Spilled: the guest's writes go through the spill file
        ScratchFolder _folder{"compose-delta"};
        MediaManager _manager{nullptr};
        DeltaSlot _slot;
        std::filesystem::path _descriptor;

        void SetUp() override
        {
            _manager.RegisterSlot(_slot);
            _folder.File("sys/README.TXT", "readme");
            _folder.File("games/ELITE.TRD", std::string(3000, 'e'));
            Descriptor("");
        }
        void TearDown() override
        {
            Eject(Disposition::Discard);
            _manager.UnregisterSlot("sd.zc");
        }

        void Descriptor(const std::string& writes)
        {
            _descriptor = _folder.File("card.ucompose.yaml", "version: 1\ntarget: {free: 1MiB, fixedTime: 1767268800}\n" + writes +
                                                                 "layers:\n  - {name: sys, source: {folder: sys}}\n"
                                                                 "  - {name: games, source: {folder: games}, mount: /GAMES}\n");
        }

        std::filesystem::path DeltaFile() const
        {
            std::filesystem::path p = _descriptor;
            p += ".delta";
            return p;
        }

        MediaResult Insert()
        {
            MediaSource source;
            source.path = FileHelper::FromFsPath(_descriptor);
            return _manager.Insert("sd.zc", source, {});
        }

        MediaResult Eject(Disposition disposition = Disposition::None)
        {
            EjectOptions options;
            options.disposition = disposition;
            return _manager.Eject("sd.zc", options);
        }

        bool Exists(const std::string& path)
        {
            FatVolumeReader reader;
            FatDirEntryInfo entry;
            return reader.Open(*_slot.attached->Block()) && reader.Stat(path, entry);
        }

        void GuestWrites(const std::string& path)
        {
            FatGuest guest(*_slot.attached->Block());
            ASSERT_TRUE(guest.Create(path, std::vector<uint8_t>(700, 'g')));
            _manager.ApplyPending();
        }
    };
}  // namespace

TEST_P(ComposeDelta_Test, RoundTrip)
{
    ASSERT_TRUE(Insert().Ok());
    GuestWrites("/NEW.TXT");
    ASSERT_TRUE(_manager.Info("sd.zc")->dirty);
    const size_t changed = _slot.attached->Session()->ChangedSectors();

    SaveOutcome outcome;
    const MediaResult saved = _manager.Save("sd.zc", {}, &outcome);
    ASSERT_TRUE(saved.Ok()) << saved.message;
    EXPECT_EQ(outcome.savedPath, FileHelper::FromFsPath(DeltaFile()));
    EXPECT_TRUE(std::filesystem::exists(DeltaFile()));
    EXPECT_FALSE(_manager.Info("sd.zc")->dirty) << "the writes are in the delta";
    EXPECT_EQ(_slot.attached->Session()->ChangedSectors(), changed) << "and still in the change layer";
    ASSERT_TRUE(Eject().Ok()) << "a clean medium leaves without a disposition";

    const MediaResult again = Insert();
    ASSERT_TRUE(again.Ok()) << again.message;
    EXPECT_TRUE(Has(again.report, "session restored from card.ucompose.yaml.delta: " + std::to_string(changed) + " sector(s)"))
        << Join(again.report);
    EXPECT_TRUE(Exists("/NEW.TXT"));
    EXPECT_FALSE(_manager.Info("sd.zc")->dirty);
    GuestWrites("/MORE.TXT");
    EXPECT_TRUE(_manager.Info("sd.zc")->dirty) << "a later write is unsaved again";
    if (_spill.Active())
        EXPECT_TRUE(_spill.Spilled()) << "the round trip went through a spill file";
}

TEST_P(ComposeDelta_Test, IdMismatchRefusedWithReason)
{
    ASSERT_TRUE(Insert().Ok());
    GuestWrites("/NEW.TXT");
    ASSERT_TRUE(_manager.Save("sd.zc", {}).Ok());
    ASSERT_TRUE(Eject().Ok());

    _folder.File("games/EXOLON.SCL", "a file added on the host");
    const MediaResult again = Insert();
    ASSERT_TRUE(again.Ok()) << again.message;
    EXPECT_TRUE(Has(again.report, "not applied")) << Join(again.report);
    EXPECT_TRUE(Has(again.report, "layer 'games' changed")) << Join(again.report);
    EXPECT_FALSE(Has(again.report, "layer 'sys'")) << Join(again.report);
    EXPECT_FALSE(Exists("/NEW.TXT")) << "the medium starts clean";
    EXPECT_TRUE(std::filesystem::exists(DeltaFile())) << "the file is kept";

    GuestWrites("/OTHER.TXT");
    const MediaResult refused = _manager.Save("sd.zc", {});
    EXPECT_EQ(refused.error, MediaError::Dirty) << refused.message;
    EXPECT_NE(refused.message.find("other sources"), std::string::npos) << refused.message;
    SaveOptions force;
    force.force = true;
    EXPECT_TRUE(_manager.Save("sd.zc", force).Ok());
    ASSERT_TRUE(Eject().Ok());
    const MediaResult third = Insert();
    EXPECT_TRUE(Has(third.report, "session restored")) << Join(third.report);
    EXPECT_TRUE(Exists("/OTHER.TXT"));
}

TEST_P(ComposeDelta_Test, TruncatedFileRefused)
{
    ASSERT_TRUE(Insert().Ok());
    GuestWrites("/NEW.TXT");
    ASSERT_TRUE(_manager.Save("sd.zc", {}).Ok());
    ASSERT_TRUE(Eject().Ok());
    std::filesystem::resize_file(DeltaFile(), std::filesystem::file_size(DeltaFile()) - 20);

    const MediaResult again = Insert();
    ASSERT_TRUE(again.Ok()) << again.message;
    EXPECT_TRUE(Has(again.report, "is damaged")) << Join(again.report);
    EXPECT_FALSE(std::filesystem::exists(DeltaFile()));
    std::filesystem::path bad = DeltaFile();
    bad += ".bad";
    EXPECT_TRUE(std::filesystem::exists(bad)) << "kept for a look";
    EXPECT_FALSE(Exists("/NEW.TXT"));
    EXPECT_FALSE(_manager.Info("sd.zc")->dirty);
}

// DT-9: an explicit strategy wins; flat needs a path; writes.save names the default (commit needs a graft); an
// eject's save runs it too, and keeps the writes as a delta when it fails (D-8)
TEST_P(ComposeDelta_Test, StrategyFollowsDt9)
{
    ASSERT_TRUE(Insert().Ok());
    GuestWrites("/NEW.TXT");
    SaveOptions flat;
    flat.strategy = "flat";
    EXPECT_EQ(_manager.Save("sd.zc", flat).error, MediaError::BadRequest) << "flat needs a path";
    SaveOptions odd;
    odd.strategy = "zip";
    EXPECT_EQ(_manager.Save("sd.zc", odd).error, MediaError::BadRequest);
    ASSERT_TRUE(Eject(Disposition::Discard).Ok());

    Descriptor("writes: {save: commit}\n");
    ASSERT_TRUE(Insert().Ok());
    GuestWrites("/NEW.TXT");
    const MediaResult commit = _manager.Save("sd.zc", {});
    EXPECT_EQ(commit.error, MediaError::BadRequest) << "writes.save: commit, and this composite is no graft";
    EXPECT_NE(commit.message.find("graft"), std::string::npos) << commit.message;
    SaveOptions delta;
    delta.strategy = "delta";
    EXPECT_TRUE(_manager.Save("sd.zc", delta).Ok());
    std::filesystem::remove(DeltaFile());
    GuestWrites("/MORE.TXT");
    const MediaResult ejected = Eject(Disposition::Save);
    ASSERT_TRUE(ejected.Ok()) << ejected.message;
    EXPECT_TRUE(Has(ejected.report, "commit failed (")) << Join(ejected.report);
    EXPECT_TRUE(Has(ejected.report, "the writes are kept as a session delta")) << Join(ejected.report);
    EXPECT_TRUE(std::filesystem::exists(DeltaFile()));
}

// D-8: a medium that leaves follows writes.save: discard drops the writes, ask keeps them as a delta (the GUI asks
// before), and the eject's own strategy wins over the descriptor
TEST_P(ComposeDelta_Test, EjectFollowsWritesSave)
{
    Descriptor("writes: {save: discard}\n");
    ASSERT_TRUE(Insert().Ok());
    GuestWrites("/NEW.TXT");
    EXPECT_EQ(_manager.Save("sd.zc", {}).error, MediaError::BadRequest) << "a plain save of discard keeps nothing";
    const MediaResult dropped = Eject(Disposition::Save);
    ASSERT_TRUE(dropped.Ok()) << dropped.message;
    EXPECT_TRUE(Has(dropped.report, "dropped (writes.save: discard)")) << Join(dropped.report);
    EXPECT_FALSE(std::filesystem::exists(DeltaFile()));

    ASSERT_TRUE(Insert().Ok());
    GuestWrites("/NEW.TXT");
    EjectOptions own;
    own.disposition = Disposition::Save;
    own.strategy = "delta";
    ASSERT_TRUE(_manager.Eject("sd.zc", own).Ok());
    EXPECT_TRUE(std::filesystem::exists(DeltaFile())) << "the eject's strategy wins";
    std::filesystem::remove(DeltaFile());

    Descriptor("writes: {save: ask}\n");
    ASSERT_TRUE(Insert().Ok());
    GuestWrites("/NEW.TXT");
    ASSERT_TRUE(Eject(Disposition::Save).Ok());
    EXPECT_TRUE(std::filesystem::exists(DeltaFile())) << "ask without a GUI: a delta";
}

// D-8: strict refuses instead of the delta fallback; the medium and its writes stay
TEST_P(ComposeDelta_Test, StrictEjectRefusesAFailedCommit)
{
    Descriptor("writes: {save: commit}\n");
    ASSERT_TRUE(Insert().Ok());
    GuestWrites("/NEW.TXT");
    EjectOptions strict;
    strict.disposition = Disposition::Save;
    strict.strict = true;
    const MediaResult refused = _manager.Eject("sd.zc", strict);
    EXPECT_EQ(refused.error, MediaError::BadRequest) << refused.message;
    EXPECT_FALSE(std::filesystem::exists(DeltaFile()));
    ASSERT_NE(_slot.attached, nullptr);
    EXPECT_TRUE(_manager.Info("sd.zc")->dirty);
}

// D-8: the manager going away saves each composite by its policy (on when the emulator runs; off in this runner)
TEST_P(ComposeDelta_Test, ReleaseSavesByPolicy)
{
    ASSERT_TRUE(Insert().Ok());
    GuestWrites("/NEW.TXT");
    const std::vector<std::string> lines = _manager.SaveByPolicyOnRelease();
    ASSERT_EQ(lines.size(), 1u) << Join(lines);
    EXPECT_TRUE(Has(lines, "sd.zc: saved by writes.save delta")) << Join(lines);
    EXPECT_TRUE(std::filesystem::exists(DeltaFile()));
    EXPECT_FALSE(_manager.Info("sd.zc")->dirty);
    std::filesystem::remove(DeltaFile());

    // The destructor does it when the switch is on
    {
        DeltaSlot slot;
        MediaManager manager(nullptr);
        manager.RegisterSlot(slot);
        MediaSource source;
        source.path = FileHelper::FromFsPath(_descriptor);
        ASSERT_TRUE(manager.Insert("sd.zc", source, {}).Ok());
        FatGuest guest(*slot.attached->Block());
        ASSERT_TRUE(guest.Create("/MORE.TXT", std::vector<uint8_t>(700, 'g')));
        MediaManager::SetSaveOnRelease(true);
    }
    MediaManager::SetSaveOnRelease(false);
    EXPECT_TRUE(std::filesystem::exists(DeltaFile()));
}

// D-8 for GUIs: NC_MEDIA_DIRTY says what closing would do (onRelease), NC_MEDIA_CLEAN when that is moot; Unsaved()
// is the query a window asks before it closes
TEST_P(ComposeDelta_Test, UnsavedSaysWhatReleaseDoes)
{
    Descriptor("writes: {save: ask}\n");
    ASSERT_TRUE(Insert().Ok());
    EXPECT_TRUE(_manager.Unsaved().empty());

    const std::string source = FileHelper::FromFsPath(_descriptor);
    std::atomic<int> dirty{0};
    std::atomic<int> clean{0};
    std::string dirtyOnRelease;
    std::atomic<uint64_t> dirtyVolume{0};
    MessageCenter& mc = MessageCenter::DefaultMessageCenter();
    auto observe = [&](std::atomic<int>& counter, bool keep) {
        return [&counter, &dirtyOnRelease, &dirtyVolume, &source, keep](int, Message* message) {
            auto* payload = message ? dynamic_cast<MediaSlotPayload*>(message->obj) : nullptr;
            if (!payload || payload->source != source)
                return;
            if (keep)
            {
                dirtyOnRelease = payload->onRelease;
                dirtyVolume = payload->volumeId;
            }
            counter++;
        };
    };
    struct Observing
    {
        MessageCenter& mc;
        uint64_t dirtyId;
        uint64_t cleanId;
        ~Observing()
        {
            mc.RemoveObserverById(NC_MEDIA_DIRTY, dirtyId);
            mc.RemoveObserverById(NC_MEDIA_CLEAN, cleanId);
        }
    } observing{mc, mc.AddObserver(NC_MEDIA_DIRTY, observe(dirty, true)), mc.AddObserver(NC_MEDIA_CLEAN, observe(clean, false))};

    MediaManager::SetSaveOnRelease(true);
    GuestWrites("/NEW.TXT");
    const std::vector<SlotInfo> unsaved = _manager.Unsaved();
    MediaManager::SetSaveOnRelease(false);
    ASSERT_EQ(unsaved.size(), 1u);
    EXPECT_EQ(unsaved[0].onRelease, "ask");
    EXPECT_EQ(_manager.Unsaved().at(0).onRelease, "lost") << "this runner saves nothing on release";
    EXPECT_TRUE(TestWait::ForAtLeast(dirty, 1));
    EXPECT_EQ(dirtyOnRelease, "ask");
    EXPECT_NE(unsaved[0].volumeId, 0u);
    EXPECT_EQ(dirtyVolume.load(), unsaved[0].volumeId) << "the notification and the query name the same volume";

    SaveOptions delta;
    delta.strategy = "delta";
    ASSERT_TRUE(_manager.Save("sd.zc", delta).Ok());
    EXPECT_TRUE(_manager.Unsaved().empty());
    EXPECT_TRUE(TestWait::ForAtLeast(clean, 1));
}

// DT-16: a rescan over unchanged sources leaves the medium (writes and all); changed sources need a disposition
TEST_P(ComposeDelta_Test, RescanFollowsDt16)
{
    ASSERT_TRUE(Insert().Ok());
    GuestWrites("/NEW.TXT");
    const MediaResult unchanged = _manager.Rescan("sd.zc");
    ASSERT_TRUE(unchanged.Ok()) << unchanged.message;
    EXPECT_TRUE(Has(unchanged.report, "unchanged")) << Join(unchanged.report);
    EXPECT_TRUE(_manager.Info("sd.zc")->dirty);
    EXPECT_TRUE(Exists("/NEW.TXT"));

    _folder.File("sys/HOST.TXT", "host");
    EXPECT_EQ(_manager.Rescan("sd.zc").error, MediaError::Dirty) << "the writes cannot follow a rebuild";
    EXPECT_TRUE(Exists("/NEW.TXT"));

    RescanOptions save;
    save.disposition = Disposition::Save;
    const MediaResult saved = _manager.Rescan("sd.zc", save);
    ASSERT_TRUE(saved.Ok()) << saved.message;
    EXPECT_TRUE(Has(saved.report, "session delta:")) << Join(saved.report);
    EXPECT_TRUE(std::filesystem::exists(DeltaFile()));
    EXPECT_TRUE(Exists("/HOST.TXT"));
    std::filesystem::remove(DeltaFile());

    GuestWrites("/MORE.TXT");
    _folder.File("sys/HOST2.TXT", "host");
    RescanOptions discard;
    discard.disposition = Disposition::Discard;
    ASSERT_TRUE(_manager.Rescan("sd.zc", discard).Ok());
    EXPECT_TRUE(Exists("/HOST2.TXT"));
    EXPECT_FALSE(Exists("/MORE.TXT"));
    EXPECT_FALSE(_manager.Info("sd.zc")->dirty);
}

TEST_P(ComposeDelta_Test, InlineDescriptorHasNoDeltaFile)
{
    MediaSource source;
    source.inlineBody = "{\"version\": 1, \"layers\": [{\"source\": {\"folder\": \"" + (_folder.Path() / "sys").generic_string() + "\"}}]}";
    source.type = MediaSourceType::Composite;
    const MediaResult inserted = _manager.Insert("sd.zc", source, {});
    ASSERT_TRUE(inserted.Ok()) << inserted.message;
    GuestWrites("/NEW.TXT");
    const MediaResult saved = _manager.Save("sd.zc", {});
    EXPECT_EQ(saved.error, MediaError::BadRequest);
    EXPECT_NE(saved.message.find("writes.delta"), std::string::npos) << saved.message;
}

INSTANTIATE_TEST_SUITE_P(Tiers, ComposeDelta_Test, ::testing::Bool(), SessionSpillGuard::TierName);
