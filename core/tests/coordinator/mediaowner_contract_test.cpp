/// @file mediaowner_contract_test.cpp
/// @brief The media manager as the owner of the media operations, against the IOpOwner contract (architecture
/// §10.2): the rules come first, a failed Prepare leaves no trace, the session effect lands at the commit point -
/// before the slot sees the change. The manager runs without an emulator; the coordinator's other ports are fakes.

#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <vector>

#include "coordinatorfakes.h"
#include "emulator/coordinator/machinecoordinator.h"
#include "emulator/io/storage/memorydisk.h"
#include "emulator/media/mediaformatregistry.h"
#include "emulator/media/mediamanager.h"

using namespace coordinator;
using namespace coordinator::fakes;
using Calls = std::vector<std::string>;

namespace
{
/// A block slot that writes what happens to it into the shared call log
class LoggingSlot : public IMediaSlot
{
public:
    LoggingSlot(std::string id, CallLog& log) : _log(log)
    {
        _descriptor.id = std::move(id);
        _descriptor.kind = MediaKind::Block;
        _descriptor.label = "logging";
    }
    const SlotDescriptor& Descriptor() const override { return _descriptor; }
    void Attach(Medium& medium) override
    {
        attached = &medium;
        _log.Add("slot:attach");
    }
    void Detach() override
    {
        attached = nullptr;
        _log.Add("slot:detach");
    }
    bool IsBusy() const override { return false; }
    void SetWriteProtectSwitch(bool) override {}

    Medium* attached = nullptr;

private:
    CallLog& _log;
    SlotDescriptor _descriptor;
};

std::unique_ptr<Medium> MemoryMedium(uint64_t sectors)
{
    MediaSource source;
    source.type = MediaSourceType::Blank;
    return MediaFormatRegistry::WrapBlock(source, AccessMode::Session, "memory", std::make_unique<MemoryDisk>(sectors));
}
}  // namespace

class MediaOwnerContract_Test : public ::testing::Test
{
protected:
    void SetUp() override
    {
        coordinator.SetMachine(&thread, &clock);
        coordinator.SetSession(&session);
        for (OpKind kind : {OpKind::MediaInsert, OpKind::MediaEject, OpKind::MediaDiscard, OpKind::DiskCreate})
            ASSERT_TRUE(coordinator.Own(kind, &manager.OpOwner()));
        manager.SetMachineOps(&coordinator);
        manager.RegisterSlot(slot);
    }

    void TearDown() override
    {
        manager.UnregisterSlot("sd.test");
        coordinator.Disown(&manager.OpOwner());
        manager.SetMachineOps(nullptr);
    }

    CallLog calls;
    FakeMachineThread thread{&calls};
    FakeClock clock;
    FakeSession session{&calls};
    MachineCoordinator coordinator;
    MediaManager manager{nullptr};
    LoggingSlot slot{"sd.test", calls};
};

TEST_F(MediaOwnerContract_Test, TheSessionEffectLandsBeforeTheSlotSeesTheMedium)
{
    InsertOptions options;
    options.ttdReason = "disk-load";
    ASSERT_TRUE(manager.Insert("sd.test", MemoryMedium(8), options).Ok());
    EXPECT_EQ(calls.Take(), (Calls{"session:media-changed:disk-load", "slot:attach"}));
    EXPECT_NE(slot.attached, nullptr);
}

/// M3 (Q4): while a recording runs the change waits for the next frame boundary, also on a machine that is not
/// running, and the session sees nothing at the commit (the change is its event where it lands)
TEST_F(MediaOwnerContract_Test, AnInsertWhileRecordingWaitsForTheFrameBoundary)
{
    session.mode = MachineMode::Recording;
    ASSERT_TRUE(manager.Insert("sd.test", MemoryMedium(8)).Ok());
    EXPECT_EQ(slot.attached, nullptr) << "not inside the frame";
    EXPECT_TRUE(calls.Take().empty()) << "no session effect, no park";
    manager.ApplyPending();
    EXPECT_NE(slot.attached, nullptr) << "landed at the boundary";
    EXPECT_EQ(coordinator.Log().Entries().back().status, OpStatus::Done);
}

TEST_F(MediaOwnerContract_Test, EndingTheRecordingStopsItThenChangesTheMedium)
{
    session.mode = MachineMode::Recording;
    InsertOptions end;
    end.endRecording = true;
    ASSERT_TRUE(manager.Insert("sd.test", MemoryMedium(8), end).Ok());
    EXPECT_EQ(calls.Take(),
              (Calls{"park:media-insert", "session:stop-then-media-changed:media-change", "slot:attach", "unpark"}));
}

TEST_F(MediaOwnerContract_Test, AFailedPrepareLeavesNoTraceAndReleasesTheManager)
{
    ASSERT_TRUE(manager.Insert("sd.test", MemoryMedium(16)).Ok());
    const std::vector<uint8_t> data(512, 0x7E);
    ASSERT_TRUE(slot.attached->Block()->WriteSector(3, data.data()));
    manager.ApplyPending();
    calls.Take();

    // A dirty medium in the slot and no disposition: Prepare refuses, nothing moves, the session sees nothing
    Medium* before = slot.attached;
    const MediaResult dirty = manager.Insert("sd.test", MemoryMedium(8));
    EXPECT_EQ(dirty.error, MediaError::Dirty);
    EXPECT_TRUE(calls.Take().empty());
    EXPECT_EQ(slot.attached, before);
    EXPECT_EQ(coordinator.Log().Entries().back().status, OpStatus::Failed);

    // The manager's lock went with the failed operation: the next one runs
    InsertOptions discard;
    discard.disposition = Disposition::Discard;
    ASSERT_TRUE(manager.Insert("sd.test", MemoryMedium(8), discard).Ok());
    EXPECT_EQ(calls.Take(), (Calls{"session:media-changed:media-change", "slot:detach", "slot:attach"}));
}

TEST_F(MediaOwnerContract_Test, EjectAndDiscardMarkTheirCommitPoint)
{
    ASSERT_TRUE(manager.Insert("sd.test", MemoryMedium(16)).Ok());
    const std::vector<uint8_t> data(512, 0x11);
    ASSERT_TRUE(slot.attached->Block()->WriteSector(1, data.data()));
    manager.ApplyPending();
    calls.Take();

    ASSERT_TRUE(manager.Discard("sd.test").Ok());
    EXPECT_EQ(calls.Take(), (Calls{"session:media-changed:media-change"}));
    ASSERT_TRUE(manager.Eject("sd.test").Ok());
    EXPECT_EQ(calls.Take(), (Calls{"session:media-changed:media-change", "slot:detach"}));
    for (const OperationLogEntry& e : coordinator.Log().Entries())
        EXPECT_EQ(e.detail.find("did not mark its commit point"), std::string::npos) << OpKindName(e.kind);
}
