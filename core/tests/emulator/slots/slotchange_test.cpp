// SlotChange (core/src/emulator/slots/slotchange.h): a slot change planned against the instance's slot set and applied
// by a restart of the machine (ZX-bus slots SL-6, docs/inprogress/2026-10-03-zx-bus-slots/tdd.md §2.3, owner decision
// Q6). The machines are created through EmulatorManager with a config override that sets their [SLOTS], as a user's
// INI would; that override is the instance's own, so the restart applies it again before the new slot set.
//
// A restart builds a second machine next to the first (config, ROMs): ~20-40 ms per restart by nature

#include <gtest/gtest.h>

#include <algorithm>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "3rdparty/message-center/messagecenter.h"
#include "_helpers/soundcardscope.h"
#include "_helpers/testpathhelper.h"
#include "base/featuremanager.h"
#include "debugger/ttd/timetravelmanager.h"
#include "emulator/config.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/emulatormanager.h"
#include "emulator/io/storage/memorydisk.h"
#include "emulator/media/mediaformatregistry.h"
#include "emulator/media/mediamanager.h"
#include "emulator/memory/memory.h"
#include "emulator/slots/slotchange.h"
#include "emulator/slots/slotconfig.h"
#include "emulator/slots/slotmanager.h"

using slots::SlotRequest;

namespace
{

using Lines = std::vector<std::pair<std::string, std::string>>;

/// The instance's create-time override: its [SLOTS] (as an INI section would give them) and one marker field the
/// restart must keep
std::function<void(CONFIG&)> SlotsOverride(const Lines& lines)
{
    return [lines](CONFIG& config) {
        SlotConfig slotConfig;
        ParseSlotsSection(lines, slotConfig);
        SlotManager::UseSlots(slotConfig, config);
        config.frameskipmax = 77;
    };
}

std::unique_ptr<Medium> MemoryCard(uint64_t sectors)
{
    MediaSource source;
    source.type = MediaSourceType::Blank;
    return MediaFormatRegistry::WrapBlock(source, AccessMode::Session, "memory", std::make_unique<MemoryDisk>(sectors));
}

/// "slot = card" per fitted slot, in slot order
std::vector<std::string> FittedOf(Emulator& emulator)
{
    std::vector<std::string> fitted;
    for (const SlotManager::Slot& slot : emulator.GetContext()->pSlotManager->Current().entries)
    {
        if (!slot.entry.disabled)
            fitted.push_back(slot.entry.slot + " = " + slot.entry.card);
    }
    return fitted;
}

SlotRequest Plug(const std::string& slot, const std::string& card, bool replace = false)
{
    SlotRequest request;
    request.op = SlotRequest::Op::Plug;
    request.slot = slot;
    request.card = card;
    request.replaceIfIncompatible = replace;
    return request;
}

class SlotChange_Test : public ::testing::Test
{
protected:
    SoundCardScope _everySound;
    std::vector<std::string> _ids;

    void TearDown() override
    {
        SlotManager::SetBuildFaultForTests("");
        for (const std::string& id : _ids)
            EmulatorManager::GetInstance()->RemoveEmulator(id);
        MessageCenter::DisposeDefaultMessageCenter();
    }

    std::shared_ptr<Emulator> Create(const char* model, const Lines& lines)
    {
        std::string error;
        std::shared_ptr<Emulator> emulator = EmulatorManager::GetInstance()->CreateEmulatorWithModel(
            "", model, LoggerLevel::LogError, &error, SlotsOverride(lines));
        EXPECT_NE(emulator, nullptr) << error;
        if (emulator)
            _ids.push_back(emulator->GetId());
        return emulator;
    }

    SlotChangeResult Change(const std::shared_ptr<Emulator>& emulator, const SlotRequest& change)
    {
        SlotChangeRequest request;
        request.emulatorId = emulator->GetId();
        request.change = change;
        SlotChangeResult result = SlotChange::Run(request);
        if (result.emulator)
            _ids.push_back(result.emulator->GetId());
        return result;
    }
};

} // namespace

/// An allowed plug restarts the instance with the new slot set: a new machine (a new id, the old one gone) built with
/// the card; the instance's own create-time override applies again
TEST_F(SlotChange_Test, PlugRestartsTheMachineWithTheNewSet)
{
    std::shared_ptr<Emulator> old = Create("PENTAGON", {{"ay-socket", "tsfm"}, {"zxbus.1", "gs"}});
    ASSERT_NE(old, nullptr);
    const std::string oldId = old->GetId();

    const SlotChangeResult result = Change(old, Plug("zxbus.next", "zxnetusb"));
    old.reset();
    ASSERT_TRUE(result.Applied()) << SlotChange::StatusName(result.status) << ": " << result.message;
    ASSERT_NE(result.emulator, nullptr);
    EXPECT_NE(result.emulator->GetId(), oldId);
    EXPECT_EQ(result.previousEmulatorId, oldId);
    EXPECT_FALSE(EmulatorManager::GetInstance()->HasEmulator(oldId)) << "the old machine is gone";
    EXPECT_EQ(FittedOf(*result.emulator),
              (std::vector<std::string>{"ay-socket = tsfm", "zxbus.1 = gs", "zxbus.2 = zxnetusb"}));
    EXPECT_TRUE(result.plan.plan.removed.empty());
    EXPECT_EQ(result.emulator->GetContext()->config.frameskipmax, 77) << "the instance's own override applies again";
    EXPECT_EQ(result.emulator->GetContext()->config.sound.gsTypeKind, GSTypeKind::Z80);
}

/// Q1 / R-OP-5: a plug that displaces cards is refused without replaceIfIncompatible, the plan lists every card it
/// would remove (one card pushes out three) and nothing changes; with the flag the machine restarts without them
TEST_F(SlotChange_Test, DisplacementNeedsTheReplaceFlag)
{
    std::shared_ptr<Emulator> old =
        Create("PENTAGON", {{"ay-socket", "tsfm"}, {"zxbus.1", "gs"}, {"zxbus.2", "soundrive"}, {"zxbus.2.mode", "both"}});
    ASSERT_NE(old, nullptr);
    const std::string oldId = old->GetId();

    const SlotChangeResult refused = Change(old, Plug("zxbus.next", "multisound"));
    EXPECT_EQ(refused.status, SlotChangeStatus::Refused) << refused.message;
    EXPECT_NE(refused.message.find("needs replaceIfIncompatible"), std::string::npos) << refused.message;
    std::vector<std::string> removed;
    for (const slots::RemovedCard& card : refused.plan.plan.removed)
        removed.push_back(card.slot + " = " + card.card);
    std::sort(removed.begin(), removed.end());
    EXPECT_EQ(removed, (std::vector<std::string>{"ay-socket = tsfm", "zxbus.1 = gs", "zxbus.2 = soundrive"}));
    EXPECT_TRUE(refused.plan.config.entries.empty()) << "a refused plan carries no new set";
    EXPECT_EQ(refused.emulator, nullptr);
    ASSERT_TRUE(EmulatorManager::GetInstance()->HasEmulator(oldId)) << "refused: the machine stays";
    EXPECT_EQ(FittedOf(*old), (std::vector<std::string>{"ay-socket = tsfm", "zxbus.1 = gs", "zxbus.2 = soundrive"}));

    const SlotChangeResult replaced = Change(old, Plug("zxbus.next", "multisound", true));
    old.reset();
    ASSERT_TRUE(replaced.Applied()) << replaced.message;
    EXPECT_EQ(replaced.plan.plan.removed.size(), 3u);
    EXPECT_EQ(FittedOf(*replaced.emulator), (std::vector<std::string>{"zxbus.3 = multisound"}));
    EXPECT_NE(replaced.emulator->GetContext()->pSlotManager->FindCard("zxbus.3"), nullptr) << "built at start";
    const SlotManager::BuiltIn* ay = replaced.emulator->GetContext()->pSlotManager->Current().FindBuiltIn("ay");
    ASSERT_NE(ay, nullptr);
    EXPECT_EQ(ay->state, "shadowed by zxbus.3") << "the socket holds the board's AY again, under the card";
}

/// A dry run returns the plan and the new set, changes nothing; SetOptions runs the same plan and restarts with the
/// changed option
TEST_F(SlotChange_Test, DryRunChangesNothingAndOptionsRestartToo)
{
    std::shared_ptr<Emulator> old = Create("PENTAGON", {{"zxbus.1", "gs"}, {"zxbus.1.ram", "128k"}});
    ASSERT_NE(old, nullptr);
    EXPECT_EQ(old->GetContext()->config.sound.gsRamKB, 128u);
    const std::string oldId = old->GetId();

    SlotRequest options;
    options.op = SlotRequest::Op::SetOptions;
    options.slot = "zxbus.1";
    static const slots::SlotPlanner planner;
    ASSERT_TRUE(slots::ParseCardOptions(*planner.FindCard("gs"), "ram=512k", options.options));
    options.dryRun = true;
    const SlotChangeResult dry = Change(old, options);
    EXPECT_EQ(dry.status, SlotChangeStatus::DryRun) << dry.message;
    ASSERT_EQ(dry.plan.config.entries.size(), 1u);
    EXPECT_EQ(dry.plan.config.entries[0].options, "ram=512k");
    EXPECT_EQ(dry.emulator, nullptr);
    EXPECT_TRUE(EmulatorManager::GetInstance()->HasEmulator(oldId));
    EXPECT_EQ(old->GetContext()->config.sound.gsRamKB, 128u);

    options.dryRun = false;
    const SlotChangeResult applied = Change(old, options);
    old.reset();
    ASSERT_TRUE(applied.Applied()) << applied.message;
    EXPECT_EQ(applied.emulator->GetContext()->config.sound.gsRamKB, 512u);
    EXPECT_FALSE(EmulatorManager::GetInstance()->HasEmulator(oldId));
}

/// R-OP-7: no slot change while a user recording runs; the refusal names the session and nothing changes. Allowed
/// again once the recording stopped
TEST_F(SlotChange_Test, RefusedWhileTtdRecords)
{
    std::shared_ptr<Emulator> old = Create("PENTAGON", {{"zxbus.1", "gs"}});
    ASSERT_NE(old, nullptr);
    old->GetFeatureManager()->setFeature(Features::kDebugMode, true);
    old->GetFeatureManager()->setFeature(Features::kTimeTravel, true);
    old->GetContext()->pMemory->UpdateFeatureCache();
    ttd::TimeTravelManager* ttd = old->GetContext()->pTimeTravelManager;
    ASSERT_NE(ttd, nullptr);
    ASSERT_TRUE(ttd->StartRecording());

    SlotRequest dry = Plug("zxbus.next", "zxnetusb");
    dry.dryRun = true;
    const SlotChangeResult refused = Change(old, Plug("zxbus.next", "zxnetusb"));
    EXPECT_EQ(refused.status, SlotChangeStatus::Recording);
    EXPECT_NE(refused.message.find("Cannot change the slot set while TTD is recording session #1"), std::string::npos)
        << refused.message;
    EXPECT_TRUE(refused.plan.recording);
    EXPECT_EQ(refused.emulator, nullptr);
    EXPECT_EQ(Change(old, dry).status, SlotChangeStatus::Recording) << "a dry run says so too";
    EXPECT_EQ(FittedOf(*old), (std::vector<std::string>{"zxbus.1 = gs"}));

    ttd->StopRecording();
    EXPECT_EQ(Change(old, dry).status, SlotChangeStatus::DryRun);
}

/// A card that cannot be built at start: the restarted machine is not created, the old one stays with its slot set
/// and the result carries the reason
TEST_F(SlotChange_Test, FailedStartKeepsTheMachine)
{
    std::shared_ptr<Emulator> old = Create("PENTAGON", {{"zxbus.1", "zxnetusb"}});
    ASSERT_NE(old, nullptr);
    const std::string oldId = old->GetId();
    const size_t machines = EmulatorManager::GetInstance()->GetEmulatorIds().size();

    SlotManager::SetBuildFaultForTests("multisound");
    const SlotChangeResult failed = Change(old, Plug("zxbus.next", "multisound", true));
    EXPECT_EQ(failed.status, SlotChangeStatus::Failed);
    EXPECT_NE(failed.message.find("zxbus.2 = multisound: the card could not be built (the test's build fault)"),
              std::string::npos)
        << failed.message;
    EXPECT_EQ(failed.emulator, nullptr);
    EXPECT_TRUE(EmulatorManager::GetInstance()->HasEmulator(oldId)) << "the previous machine stays";
    EXPECT_EQ(EmulatorManager::GetInstance()->GetEmulatorIds().size(), machines) << "and the new one is gone";
    EXPECT_EQ(FittedOf(*old), (std::vector<std::string>{"zxbus.1 = zxnetusb"}));

    SlotManager::SetBuildFaultForTests("");
    EXPECT_TRUE(Change(old, Plug("zxbus.next", "multisound", true)).Applied());
}

/// R-OP-6: media follow the restart into the slots with the same id (the live medium, its unsaved writes included);
/// a removed card's medium with unsaved writes refuses the change until the request says save or discard; then it is
/// closed and reported
TEST_F(SlotChange_Test, MediaCarriedAcrossTheRestart)
{
    std::shared_ptr<Emulator> old = Create("PENTAGON", {{"zxbus.1", "neogs"}});
    ASSERT_NE(old, nullptr);
    ASSERT_TRUE(old->LoadDisk(TestPathHelper::GetTestDataPath("loaders/trd/EyeAche.trd"), 0));
    MediaManager& before = *old->GetContext()->pMediaManager;
    const Medium* floppy = before.GetMedium("fdd.a");
    ASSERT_NE(floppy, nullptr);
    ASSERT_TRUE(before.HasSlot("sd.ngs"));
    ASSERT_TRUE(before.Insert("sd.ngs", MemoryCard(64)).Ok());
    std::vector<uint8_t> sector(512, 0x5A);
    ASSERT_TRUE(before.GetMedium("sd.ngs")->Block()->WriteSector(0, sector.data()));
    ASSERT_TRUE(before.Info("sd.ngs")->dirty);

    SlotRequest remove;
    remove.op = SlotRequest::Op::Remove;
    remove.slot = "zxbus.1";
    const SlotChangeResult refused = Change(old, remove);
    EXPECT_EQ(refused.status, SlotChangeStatus::Refused);
    EXPECT_NE(refused.message.find("sd.ngs"), std::string::npos) << refused.message;
    ASSERT_EQ(refused.plan.plan.media.size(), 1u);
    EXPECT_TRUE(refused.plan.plan.media[0].dirty);

    remove.mediaDisposition = slots::MediaDisposition::Discard;
    const SlotChangeResult removed = Change(old, remove);
    old.reset();
    ASSERT_TRUE(removed.Applied()) << removed.message;
    MediaManager& after = *removed.emulator->GetContext()->pMediaManager;
    EXPECT_EQ(after.GetMedium("fdd.a"), floppy) << "the live floppy moved, nothing re-read";
    EXPECT_FALSE(after.HasSlot("sd.ngs")) << "no NeoGS, no SD slot";
    EXPECT_EQ(removed.media.closed, std::vector<std::string>{"sd.ngs"}) << "the removed card's medium is reported";
    EXPECT_TRUE(FittedOf(*removed.emulator).empty());
}

/// The new set is checked as the machine's creation would check it (Q8): a request whose result the machine would
/// refuse is refused before anything restarts; an unknown machine is reported
TEST_F(SlotChange_Test, RequestsThatCannotApplyAreRefused)
{
    std::shared_ptr<Emulator> old = Create("PENTAGON", {{"zxbus.1", "gs"}});
    ASSERT_NE(old, nullptr);

    const SlotChangeResult unknown = Change(old, Plug("zxbus.next", "no-such-card"));
    EXPECT_EQ(unknown.status, SlotChangeStatus::Refused);
    EXPECT_NE(unknown.message.find("unknown card"), std::string::npos) << unknown.message;

    SlotRequest remove;
    remove.op = SlotRequest::Op::Remove;
    remove.slot = "zxbus.4";
    EXPECT_EQ(Change(old, remove).status, SlotChangeStatus::Refused);

    SlotChangeRequest nobody;
    nobody.emulatorId = "no-such-emulator";
    nobody.change = Plug("zxbus.next", "zxnetusb");
    EXPECT_EQ(SlotChange::Run(nobody).status, SlotChangeStatus::NoMachine);
}
