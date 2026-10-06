// S2 session delta (multi-source phases/c6-provenance-flatten.md §4; flatten-strategies.md S2, DT-9, DT-13): a
// composite's guest writes saved next to its descriptor, restored at the next insert over the same sources only.

#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include "_helpers/fatguest.h"
#include "_helpers/scratchfolder.h"
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

    class ComposeDelta_Test : public ::testing::Test
    {
    protected:
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

TEST_F(ComposeDelta_Test, RoundTrip)
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
}

TEST_F(ComposeDelta_Test, IdMismatchRefusedWithReason)
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

TEST_F(ComposeDelta_Test, TruncatedFileRefused)
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

// DT-9: an explicit strategy wins; flat needs a path; writes.save names the default; commit and write-back are for
// phase C8, and an eject's save falls back to a delta for them (D-8)
TEST_F(ComposeDelta_Test, StrategyFollowsDt9)
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
    EXPECT_EQ(_manager.Save("sd.zc", {}).error, MediaError::NotSupported) << "commit is phase C8";
    SaveOptions delta;
    delta.strategy = "delta";
    EXPECT_TRUE(_manager.Save("sd.zc", delta).Ok());
    std::filesystem::remove(DeltaFile());
    GuestWrites("/MORE.TXT");
    ASSERT_TRUE(Eject(Disposition::Save).Ok()) << "an eject saves a delta instead (D-8)";
    EXPECT_TRUE(std::filesystem::exists(DeltaFile()));
}

TEST_F(ComposeDelta_Test, InlineDescriptorHasNoDeltaFile)
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
